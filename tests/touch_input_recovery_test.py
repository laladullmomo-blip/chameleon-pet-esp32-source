"""Guard the accepted d1d97df touch behavior with the actual CST9217 decoder.

Only ESP-IDF transport, scheduler, and LVGL context access are stubbed. The
read callback, pending-input hook, and controller decoder are extracted from
their real sources. These checks verify software event and cache behavior;
physical sensitivity still requires the user's development board.
"""
import importlib.util
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "managed_components/espressif__esp_lvgl_adapter"
CST = ROOT / "managed_components/waveshare__esp_lcd_touch_cst9217/esp_lcd_touch_cst9217.c"
spec = importlib.util.spec_from_file_location("touch_recovery_adapter", ROOT / "scripts/prepare_standby_adapter.py")
integration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(integration)


def function_source(source, signature):
    """Extract a complete definition; signatures must include the opening brace."""
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        depth += (source[index] == "{") - (source[index] == "}")
        if depth == 0:
            return source[start:index + 1]
    raise ValueError(f"Unclosed C function: {signature}")


PREAMBLE = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
typedef int lv_coord_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_RESPONSE 264
#define GPIO_NUM_NC -1
#define CST9217_DATA_LENGTH 10
#define CST9217_ACK_VALUE 0xAB
#define CST9217_MAX_TOUCH_POINTS 1
#define ESP_LCD_TOUCH_CST9217_DATA_REG 0xD000
#define MAX_TOUCH_POINTS_V9 1
#define LV_USE_GESTURE_RECOGNITION 0
#define ESP_LV_ADAPTER_INPUT_TYPE_TOUCH 1
#define pdTRUE 1
#define pdFALSE 0
#define TAG "touch-test"
#define ESP_LOGV(...)
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
#define pdMS_TO_TICKS(x) (x)
#define vTaskDelay(x) ((void)0)
#define gpio_set_level(x,y) ((void)0)
#define ESP_GOTO_ON_ERROR(expr,label,tag,fmt) do {ret=(expr);if(ret!=ESP_OK)goto label;}while(0)
typedef struct {int x,y;} lv_point_t;
typedef enum {LV_INDEV_STATE_RELEASED,LV_INDEV_STATE_PRESSED} lv_indev_state_t;
typedef enum {LV_INDEV_MODE_TIMER,LV_INDEV_MODE_EVENT} lv_indev_mode_t;
typedef struct {lv_point_t point;lv_indev_state_t state;uint32_t timestamp;} lv_indev_data_t;
typedef struct {uint16_t x,y,strength;uint8_t track_id;} esp_lcd_touch_point_data_t;
struct Touch {struct {int lock;uint8_t points;struct {uint16_t x,y;}coords[1];}data;struct {int rst_gpio_num;}config;};
typedef struct Touch *esp_lcd_touch_handle_t;
typedef struct TouchContext TouchContext;
typedef struct {TouchContext *driver_data;lv_indev_mode_t mode;int wait_until_release;lv_indev_data_t output;} lv_indev_t;
struct TouchContext {
    esp_lcd_touch_handle_t handle;
    bool with_irq;
    int *touch_sem;
    lv_indev_t *indev;
    struct {float x,y;}scale;
    struct {bool enabled;}multi;
    struct {esp_err_t (*custom_touch_read)(esp_lcd_touch_handle_t,esp_lcd_touch_point_data_t *,uint8_t *,uint8_t,void *);void *user_ctx;}cbs;
    lv_point_t last_point;
    lv_indev_state_t last_state;
};
typedef TouchContext esp_lv_adapter_touch_ctx_t;
typedef struct InputNode {int type;lv_indev_t *indev;struct InputNode *next;} esp_lv_adapter_input_node_t;
typedef struct {esp_lv_adapter_input_node_t *input_list;} esp_lv_adapter_context_t;
static struct Touch touch;
static TouchContext context;
static lv_indev_t input;
static esp_lv_adapter_input_node_t node;
static esp_lv_adapter_context_t adapter;
static uint8_t packet[10];
static int transport_error,reads,irq_token,cancellations;
static uint32_t clock_ms;
static uint32_t consumed_sample_time;
static uint32_t chameleon_touch_sample_timestamp(void){return consumed_sample_time;}
static unsigned queued_reports,delivered_reports;
static lv_indev_state_t delivered_states[128];
static uint32_t delivered_times[128];
static esp_err_t cst9217_read_reg(esp_lcd_touch_handle_t tp,int reg,uint8_t *out,size_t len){reads++;memcpy(out,packet,len);return transport_error;}
static void *lv_indev_get_driver_data(lv_indev_t *indev){return indev->driver_data;}
static uint32_t lv_tick_get(void){return clock_ms;}
static uint32_t lv_tick_elaps(uint32_t then){return clock_ms-then;}
static uint32_t lv_tick_diff(uint32_t now,uint32_t then){return now-then;}
static int xSemaphoreTake(int *sem,int wait){if(*sem){*sem=0;return pdTRUE;}return pdFALSE;}
static unsigned uxSemaphoreGetCount(int *sem){return (unsigned)*sem;}
static esp_lv_adapter_context_t *esp_lv_adapter_get_context(void){return &adapter;}
static esp_lv_adapter_touch_ctx_t *get_touch_ctx(lv_indev_t *indev){return indev->driver_data;}
static lv_indev_mode_t lv_indev_get_mode(lv_indev_t *indev){return indev->mode;}
static void lv_indev_set_mode(lv_indev_t *indev,lv_indev_mode_t mode){indev->mode=mode;}
static void lv_indev_wait_release(lv_indev_t *indev){cancellations++;indev->wait_until_release=1;}
static void lvgl_touch_read(lv_indev_t *,lv_indev_data_t *);
static void lv_indev_read(lv_indev_t *indev){
    lvgl_touch_read(indev,&indev->output);
    if(context.cbs.custom_touch_read){
        assert(delivered_reports<128);
        delivered_states[delivered_reports]=indev->output.state;
        delivered_times[delivered_reports++]=indev->output.timestamp;
    }
}
"""


TRANSPORT_SHIM = r"""
static esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t tp){return esp_lcd_touch_cst9217_read_data(tp);}
static esp_err_t esp_lcd_touch_get_data(esp_lcd_touch_handle_t tp,esp_lcd_touch_point_data_t *out,uint8_t *count,uint8_t capacity){
    uint16_t x[1]={0},y[1]={0},strength[1]={0};
    esp_lcd_touch_cst9217_get_xy(tp,x,y,strength,count,capacity);
    if(*count){out[0].x=x[0];out[0].y=y[0];out[0].strength=strength[0];}
    return ESP_OK;
}
"""


SCENARIOS = r"""
static void report(bool pressed,bool valid_ack,int x,int y){
    memset(packet,0,sizeof(packet));packet[0]=pressed?6:0;packet[1]=x>>4;packet[2]=y>>4;packet[3]=(x<<4)|(y&15);packet[5]=pressed?1:0;packet[6]=valid_ack?0xAB:0;
}
static void reset(void){
    memset(&touch,0,sizeof(touch));memset(&context,0,sizeof(context));memset(&input,0,sizeof(input));
    touch.config.rst_gpio_num=-1;context.handle=&touch;context.with_irq=true;context.touch_sem=&irq_token;context.indev=&input;context.scale.x=context.scale.y=1;
    input.driver_data=&context;input.mode=LV_INDEV_MODE_EVENT;node=(esp_lv_adapter_input_node_t){.type=ESP_LV_ADAPTER_INPUT_TYPE_TOUCH,.indev=&input};adapter.input_list=&node;
    transport_error=reads=irq_token=cancellations=0;clock_ms=100;
    consumed_sample_time=queued_reports=delivered_reports=0;
}
static void irq_read(void){irq_token=1;lv_indev_read(&input);}
static void later(unsigned milliseconds){clock_ms+=milliseconds;chameleon_lv_adapter_process_pending_touch();}
static void malformed_reports_preserve_existing_press(void){
    reset();report(true,true,100,200);irq_read();assert(input.output.state==LV_INDEV_STATE_PRESSED);assert(reads==1);
    /* The real CST decoder leaves its validated point cache untouched when
     * ACK is malformed. d1d97df consumes that cache instead of fabricating
     * a release or cancelling the in-progress gesture. */
    for(int i=0;i<20;i++){
        report(true,false,999,888);irq_read();
        assert(input.output.state==LV_INDEV_STATE_PRESSED);
        assert(input.output.point.x==100&&input.output.point.y==200);
        assert(!input.wait_until_release&&cancellations==0);
    }
    const int after=reads;
    for(int i=0;i<100;i++){later(1000);lv_indev_read(&input);}
    assert(reads==after);assert(input.output.state==LV_INDEV_STATE_PRESSED);
    assert(!input.wait_until_release&&cancellations==0);
    report(false,true,0,0);irq_read();assert(input.output.state==LV_INDEV_STATE_RELEASED);assert(!input.wait_until_release);
}
static void invalid_initial_report_does_not_invent_press(void){
    reset();
    for(int i=0;i<20;i++){
        report(true,false,100,200);irq_read();
        assert(input.output.state==LV_INDEV_STATE_RELEASED);
        assert(input.output.point.x==0&&input.output.point.y==0);
        assert(!input.wait_until_release&&cancellations==0);
    }
    transport_error=ESP_FAIL;report(true,true,100,200);irq_read();
    assert(input.output.state==LV_INDEV_STATE_RELEASED);
    transport_error=ESP_OK;report(true,true,115,215);irq_read();
    assert(input.output.state==LV_INDEV_STATE_PRESSED);
    assert(input.output.point.x==115&&input.output.point.y==215);
}
static void no_irq_means_no_hardware_read(void){
    reset();report(true,true,100,200);
    for(int i=0;i<100;i++){later(1000);lv_indev_read(&input);}
    assert(reads==0);assert(input.output.state==LV_INDEV_STATE_RELEASED);
    irq_token=1;chameleon_lv_adapter_process_pending_touch();
    assert(reads==1);assert(irq_token==0);assert(input.output.state==LV_INDEV_STATE_PRESSED);
    report(false,true,0,0);
    for(int i=0;i<100;i++){later(1000);lv_indev_read(&input);}
    assert(reads==1);assert(input.output.state==LV_INDEV_STATE_PRESSED);
    assert(!input.wait_until_release&&cancellations==0);
    irq_token=1;chameleon_lv_adapter_process_pending_touch();
    assert(reads==2);assert(input.output.state==LV_INDEV_STATE_RELEASED);
}
static void irq_updates_valid_coordinates(void){
    reset();report(true,true,100,200);irq_token=1;chameleon_lv_adapter_process_pending_touch();
    assert(reads==1);assert(input.output.point.x==100&&input.output.point.y==200);
    report(true,true,125,250);irq_token=1;chameleon_lv_adapter_process_pending_touch();
    assert(reads==2);assert(input.output.state==LV_INDEV_STATE_PRESSED);
    assert(input.output.point.x==125&&input.output.point.y==250);
    assert(!input.wait_until_release&&cancellations==0);
    report(false,true,0,0);irq_token=1;chameleon_lv_adapter_process_pending_touch();
    assert(reads==3);assert(input.output.state==LV_INDEV_STATE_RELEASED);
    assert(input.output.point.x==125&&input.output.point.y==250);
}
static void transport_fault_does_not_invent_release(void){
    reset();report(true,true,100,200);irq_read();transport_error=ESP_FAIL;
    for(int i=0;i<20;i++){
        irq_read();later(1000);lv_indev_read(&input);
        assert(input.output.state==LV_INDEV_STATE_PRESSED);
        assert(input.output.point.x==100&&input.output.point.y==200);
        assert(!input.wait_until_release&&cancellations==0);
    }
    transport_error=ESP_OK;report(false,true,0,0);irq_read();
    assert(input.output.state==LV_INDEV_STATE_RELEASED);
    assert(!input.wait_until_release&&cancellations==0);
}
static esp_err_t queued_read(esp_lcd_touch_handle_t tp,esp_lcd_touch_point_data_t *out,uint8_t *count,uint8_t max_count,void *unused){
    assert(queued_reports>0&&max_count>0);
    *count=(delivered_reports%2)==0?1:0;
    if(*count){out[0].x=100;out[0].y=200;}
    consumed_sample_time=100+delivered_reports*20;
    if(--queued_reports)irq_token=1;
    return ESP_OK;
}
static void queued_edges_are_drained_with_capture_times(void){
    reset();context.cbs.custom_touch_read=queued_read;queued_reports=2;irq_token=1;
    chameleon_lv_adapter_process_pending_touch();
    assert(reads==0&&delivered_reports==2&&queued_reports==0);
    assert(delivered_states[0]==LV_INDEV_STATE_PRESSED&&delivered_states[1]==LV_INDEV_STATE_RELEASED);
    assert(delivered_times[0]==100&&delivered_times[1]==120);
    reset();context.cbs.custom_touch_read=queued_read;queued_reports=100;irq_token=1;
    chameleon_lv_adapter_process_pending_touch();
    assert(delivered_reports==64&&queued_reports==36&&irq_token==1);
    chameleon_lv_adapter_process_pending_touch();
    assert(delivered_reports==100&&queued_reports==0&&reads==0);
    for(unsigned i=0;i<100;++i){
        assert(delivered_states[i]==(i%2?LV_INDEV_STATE_RELEASED:LV_INDEV_STATE_PRESSED));
        assert(delivered_times[i]==100+i*20);
    }
}
int main(int argc,char **argv){
    assert(argc==2);
    if(!strcmp(argv[1],"malformed-press-cache"))malformed_reports_preserve_existing_press();
    else if(!strcmp(argv[1],"invalid-initial"))invalid_initial_report_does_not_invent_press();
    else if(!strcmp(argv[1],"irq-gate"))no_irq_means_no_hardware_read();
    else if(!strcmp(argv[1],"irq-coordinates"))irq_updates_valid_coordinates();
    else if(!strcmp(argv[1],"transport-press-cache"))transport_fault_does_not_invent_release();
    else if(!strcmp(argv[1],"queued-edges"))queued_edges_are_drained_with_capture_times();
    else return 2;
    printf("%s passed\n",argv[1]);return 0;
}
"""


class TouchRecoveryChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("A C compiler is required to run the generated callback")
        cls.directory = tempfile.TemporaryDirectory(prefix="touch-input-recovery-")
        folder = Path(cls.directory.name)
        generated = folder / "generated"
        integration.prepare(COMPONENT, generated)
        touch_source = (generated / integration.TOUCH).read_text()
        cst_source = CST.read_text()
        source = PREAMBLE
        for signature in (
            "static esp_err_t esp_lcd_touch_cst9217_read_data(esp_lcd_touch_handle_t tp)\n{",
            "static bool esp_lcd_touch_cst9217_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num)\n{",
        ):
            source += "\n" + function_source(cst_source, signature)
        source += TRANSPORT_SHIM
        source += "\n" + function_source(touch_source, "static void lvgl_touch_read(lv_indev_t *indev_drv, lv_indev_data_t *data)\n{")
        source += "\n" + function_source(touch_source, "void chameleon_lv_adapter_process_pending_touch(void)\n{")
        source += SCENARIOS
        harness = folder / "touch_recovery.c"
        harness.write_text(source)
        cls.executable = folder / "touch_recovery"
        result = subprocess.run([compiler, "-std=c11", "-Werror=implicit-function-declaration", str(harness), "-o", str(cls.executable)],capture_output=True,text=True)
        if result.returncode:
            cls.directory.cleanup()
            raise AssertionError(f"Generated touch harness did not compile:\n{result.stderr}")

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def run_scenario(self, name):
        result = subprocess.run([str(self.executable), name], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_bad_ack_preserves_validated_press_until_valid_release(self):
        self.run_scenario("malformed-press-cache")

    def test_invalid_initial_report_does_not_create_a_press(self):
        self.run_scenario("invalid-initial")

    def test_callback_and_pending_hook_do_not_read_without_irq(self):
        self.run_scenario("irq-gate")

    def test_pending_irq_updates_coordinates_and_valid_release(self):
        self.run_scenario("irq-coordinates")

    def test_transport_fault_does_not_fabricate_release_or_cancel(self):
        self.run_scenario("transport-press-cache")

    def test_pending_hook_drains_edges_and_uses_capture_times(self):
        self.run_scenario("queued-edges")


if __name__ == "__main__":
    unittest.main()
