"""Execute the real independent touch buffer and sampler with transport stubs.

The host supplies packets and scheduler events. Product state machines are
compiled from main/, rather than copied into a test implementation. This does
not establish physical controller sensitivity or real-board timing.
"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

BUFFER_SCENARIOS = r"""
#include <cassert>
#include "chameleon_touch_buffer.h"
using ch_touch::Buffer;
using ch_touch::Kind;
using ch_touch::Sample;

int main() {
    Buffer taps;
    for (int i = 0; i != 8; ++i) {
        assert(taps.record(i, i + 10, true, 100 + i * 10));
        assert(taps.record(0, 0, false, 105 + i * 10));
    }
    Sample sample;
    for (int i = 0; i != 8; ++i) {
        assert(taps.pop(sample));
        assert(sample.kind == Kind::Down && sample.x == i && sample.y == i + 10);
        assert(sample.timestamp == static_cast<uint32_t>(100 + i * 10));
        assert(taps.pop(sample));
        assert(sample.kind == Kind::Up && sample.x == i && sample.y == i + 10);
        assert(sample.timestamp == static_cast<uint32_t>(105 + i * 10));
    }
    assert(!taps.pop(sample));

    Buffer drag;
    assert(drag.record(10, 20, true, 1));
    for (int i = 0; i != 1000; ++i) assert(drag.record(30 + i, 40, true, 2 + i));
    assert(drag.size() == 2);  // The first hit coordinate survives coalescing.
    assert(drag.record(0, 0, false, 1002));
    assert(drag.pop(sample) && sample.kind == Kind::Down && sample.x == 10);
    assert(drag.pop(sample) && sample.kind == Kind::Move && sample.x == 1029);
    assert(drag.pop(sample) && sample.kind == Kind::Up && sample.x == 1029);
    assert(!drag.pending());

    Buffer overflow;
    for (std::size_t i = 0; i != Buffer::capacity / 2; ++i) {
        assert(overflow.record(20, 30, true, i * 10 + 1));
        assert(overflow.record(0, 0, false, i * 10 + 2));
    }
    assert(overflow.record(40, 50, true, 999));
    assert(overflow.size() == 1);
    assert(overflow.pop(sample) && sample.kind == Kind::Cancel);
    assert(!overflow.record(41, 51, true, 1000));
    assert(!overflow.record(0, 0, false, 1001));
    assert(overflow.record(60, 70, true, 1002));
    assert(overflow.pop(sample) && sample.kind == Kind::Down && sample.x == 60);

    Buffer error;
    assert(error.record(10, 20, true, 1));
    assert(error.cancel(2));
    assert(error.pop(sample) && sample.kind == Kind::Cancel);
    assert(!error.record(30, 40, true, 3));
    assert(!error.record(0, 0, false, 4));
    assert(error.record(50, 60, true, 5));
    assert(error.pop(sample) && sample.kind == Kind::Down && sample.timestamp == 5);
}
"""


TRANSPORT_STUBS = r"""
#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>
#define IRAM_ATTR
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define GPIO_NUM_NC -1
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_INVALID_RESPONSE 4
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(x) (x)
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x), ++critical_depth)
#define portEXIT_CRITICAL(x) ((void)(x), --critical_depth)
#define portENTER_CRITICAL_ISR(x) ((void)(x), ++critical_depth)
#define portEXIT_CRITICAL_ISR(x) ((void)(x), --critical_depth)
#define portYIELD_FROM_ISR() ((void)0)
using esp_err_t = int;
using TickType_t = uint32_t;
using BaseType_t = int;
using TaskHandle_t = void *;
using portMUX_TYPE = int;
enum lv_indev_state_t { LV_INDEV_STATE_RELEASED, LV_INDEV_STATE_PRESSED };
struct lv_indev_t {
    lv_indev_state_t state=LV_INDEV_STATE_RELEASED;
    bool waiting_release=false;
};
struct esp_lcd_touch_t {
    struct { int int_gpio_num = 10; uint16_t x_max = 466, y_max = 466; void *user_data = nullptr; } config;
};
using esp_lcd_touch_handle_t = esp_lcd_touch_t *;
struct esp_lcd_touch_point_data_t { uint16_t x=0,y=0,strength=0; uint8_t track_id=0; };
using Callback = void (*)(esp_lcd_touch_handle_t);
struct Step { uint32_t expected_wait, advance; bool notified; esp_err_t error; int count, x=0, y=0; };
struct EndRun {};
inline std::vector<Step> steps;
inline std::size_t step_index;
inline Step packet;
inline uint32_t tick_ms;
inline int reads, cache_reads, adapter_notifications, cancellations, deleted_tasks;
inline int critical_depth;
inline esp_err_t registration_error;
inline bool task_creation_ok = true;
inline Callback registered_callback;
inline void (*created_task)(void *);
inline bool handler_removed, irq_disabled;
inline void (*after_read)();
inline void (*after_get_data)();
inline int64_t esp_timer_get_time() { return static_cast<int64_t>(tick_ms) * 1000; }
inline uint32_t ulTaskNotifyTake(BaseType_t, TickType_t wait) {
    if (step_index == steps.size()) throw EndRun{};
    packet = steps[step_index++];
    assert(wait == packet.expected_wait);
    tick_ms += packet.advance;
    return packet.notified ? 1 : 0;
}
inline BaseType_t xTaskCreatePinnedToCore(void (*function)(void *), const char *, uint32_t,
                                         void *, BaseType_t, TaskHandle_t *task, BaseType_t) {
    if (!task_creation_ok) return pdFALSE;
    created_task = function; *task = reinterpret_cast<void *>(1); return pdPASS;
}
inline void xTaskNotifyGive(TaskHandle_t task) { assert(task != nullptr && critical_depth > 0); }
inline void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *woken) {
    assert(task != nullptr && critical_depth > 0); *woken = pdTRUE;
}
inline void vTaskDelete(TaskHandle_t) {
    assert(handler_removed && irq_disabled && critical_depth == 0); ++deleted_tasks;
}
inline esp_err_t gpio_intr_disable(int) { irq_disabled = true; return ESP_OK; }
inline esp_err_t gpio_intr_enable(int) { irq_disabled = false; return ESP_OK; }
inline esp_err_t esp_lcd_touch_register_interrupt_callback_with_data(esp_lcd_touch_handle_t touch,
                                                                     Callback callback, void *data) {
    touch->config.user_data=data; registered_callback=callback; return registration_error;
}
inline esp_err_t esp_lcd_touch_register_interrupt_callback(esp_lcd_touch_handle_t, Callback callback) {
    registered_callback=callback; handler_removed = callback == nullptr; return ESP_OK;
}
inline esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t) {
    ++reads; if (after_read) after_read(); return packet.error;
}
inline esp_err_t esp_lcd_touch_get_data(esp_lcd_touch_handle_t, esp_lcd_touch_point_data_t *point,
                                       uint8_t *count, uint8_t) {
    ++cache_reads; *count=packet.count; point->x=packet.x; point->y=packet.y;
    if (after_get_data) after_get_data();
    return ESP_OK;
}
inline bool esp_lv_adapter_touch_notify_interrupt(lv_indev_t *) { ++adapter_notifications; return true; }
inline lv_indev_state_t lv_indev_get_state(const lv_indev_t *input) { return input->state; }
inline void lv_indev_wait_release(lv_indev_t *input) {
    ++cancellations; input->waiting_release=true;
}
"""

SAMPLER_SCENARIOS = r"""
#include "chameleon_touch.cpp"

static esp_lcd_touch_t touch;
static lv_indev_t input;
static void reset_runtime() {
    runtime.buffer = {}; runtime.discard_until_release=false;
    runtime.touch = nullptr; runtime.input = nullptr;
    runtime.task = nullptr; runtime.initialized.store(false); runtime.ready.store(false);
    runtime.screen_off.store(false); runtime.screen_generation.store(0);
    runtime.consumed_timestamp.store(0); runtime.consumed = {};
    input={};
    steps.clear(); step_index=0; tick_ms=100;
    reads=cache_reads=adapter_notifications=cancellations=deleted_tasks=critical_depth=0;
    registration_error=ESP_OK; task_creation_ok=true; handler_removed=irq_disabled=false;
    after_read=after_get_data=nullptr;
}
static void run_sampler() {
    assert(chameleon_touch_start(&touch, &input) == ESP_OK);
    try { created_task(nullptr); } catch (const EndRun &) {}
}
static uint8_t consume(uint32_t timestamp) {
    esp_lcd_touch_point_data_t point{}; uint8_t count=99;
    assert(chameleon_touch_read(&touch, &point, &count, 1, nullptr) == ESP_OK);
    assert(chameleon_touch_sample_timestamp() == timestamp);
    // The real LVGL reader publishes data.state, then a released pointer
    // clears wait_until_release before processing the next press.
    input.state=count ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    if (!count) input.waiting_release=false;
    return count;
}
static bool record_current_report(int16_t x, int16_t y, bool pressed, uint32_t timestamp) {
    return record_report(x,y,pressed,timestamp,runtime.screen_generation.load());
}
int main() {
    reset_runtime();
    runtime.screen_off.store(true);
    steps = {{portMAX_DELAY,0,true,ESP_OK,0},
             {portMAX_DELAY,10,true,ESP_OK,1,20,30},
             {portMAX_DELAY,5000,true,ESP_OK,0}};
    run_sampler();
    assert(reads==0 && cache_reads==0 && adapter_notifications==0);
    assert(consume(0)==0); // Dark touches never enter the consumer.

    reset_runtime();
    steps = {{portMAX_DELAY,0,true,ESP_OK,0}, {0,0,false,ESP_OK,1,20,30},
             {5,5,false,ESP_OK,0}, {5,5,false,ESP_OK,1,40,50}, {5,5,false,ESP_OK,0}};
    run_sampler();
    // Both complete short taps survive blocked consumption and retain times.
    assert(reads==4 && cache_reads==4);
    assert(consume(100)==1 && consume(105)==0);
    assert(consume(110)==1 && consume(115)==0);

    reset_runtime();
    assert(chameleon_touch_start(&touch,&input)==ESP_OK);
    record_current_report(20,30,true,100);
    assert(consume(100)==1);
    chameleon_touch_set_screen_off(true);
    assert(irq_disabled && cancellations==1 && consume(0)==0);
    const auto notifications=adapter_notifications;
    registered_callback(&touch);
    record_current_report(90,90,true,110);
    assert(adapter_notifications==notifications && consume(0)==0);
    chameleon_touch_set_screen_off(false);
    assert(!irq_disabled);
    // A finger held while the physical button wakes must first lift.
    record_current_report(90,90,true,120);assert(consume(0)==0);
    record_current_report(90,90,false,125);assert(consume(0)==0);
    record_current_report(50,60,true,130);record_current_report(50,60,false,135);
    assert(consume(130)==1 && consume(135)==0);

    reset_runtime();
    steps = {{portMAX_DELAY,0,true,ESP_OK,0}, {0,0,false,ESP_OK,1,20,30}};
    for(int i=0;i!=201;++i)steps.push_back({5,5,false,ESP_ERR_INVALID_RESPONSE,1,900,900});
    steps.push_back({5,5,false,ESP_OK,0});
    steps.push_back({5,5,false,ESP_OK,1,50,60});
    steps.push_back({5,5,false,ESP_OK,0});
    run_sampler();
    assert(cache_reads==4 && consume(1100)==0 && cancellations==1);
    assert(consume(1115)==1 && consume(1120)==0);

    reset_runtime();
    task_creation_ok=false;
    assert(chameleon_touch_start(&touch,&input)==ESP_ERR_NO_MEM);
    assert(!runtime.initialized.load() && !runtime.task && runtime.touch==nullptr);
    task_creation_ok=true; registration_error=ESP_ERR_INVALID_STATE;
    void *original=reinterpret_cast<void *>(77); touch.config.user_data=original;
    assert(chameleon_touch_start(&touch,&input)==ESP_ERR_INVALID_STATE);
    assert(handler_removed && irq_disabled && deleted_tasks==1);
    assert(touch.config.user_data==original && !runtime.task && !runtime.initialized.load());
    registration_error=ESP_OK;
    assert(chameleon_touch_start(&touch,&input)==ESP_OK);
    assert(chameleon_touch_start(&touch,&input)==ESP_ERR_INVALID_STATE);
    registered_callback(&touch); // ISR notification uses a valid task only.
    assert(critical_depth == 0);
    chameleon_touch_set_screen_off(true);
    assert(critical_depth == 0);
    publish_sampler(nullptr);
    registered_callback(&touch); // An in-flight ISR after revocation is inert.
    assert(critical_depth == 0);
}
"""


TRANSITION_SCENARIOS = SAMPLER_SCENARIOS.split("int main() {", 1)[0] + r"""
static void cycle_display_during_cached_report() {
    after_get_data=nullptr;
    chameleon_touch_set_screen_off(true);
    chameleon_touch_set_screen_off(false);
    assert(runtime.screen_generation.load()==2 && runtime.discard_until_release);
}
static void cycle_display_during_stalled_read() {
    if (reads!=2) return;
    after_read=nullptr;
    chameleon_touch_set_screen_off(true);
    chameleon_touch_set_screen_off(false);
}
int main() {
    // A release read before off/on must not authorise a finger already held
    // on the newly lit screen. Only its real release allows the next tap.
    reset_runtime();
    steps={{portMAX_DELAY,0,true,ESP_OK,0}, {0,0,false,ESP_OK,0},
           {0,5,false,ESP_OK,1,70,80}, {5,5,false,ESP_OK,1,70,80},
           {5,5,false,ESP_OK,0}, {5,5,false,ESP_OK,1,90,100}, {5,5,false,ESP_OK,0}};
    after_get_data=cycle_display_during_cached_report;
    run_sampler();
    assert(reads==6 && adapter_notifications==3 && cancellations==0);
    assert(consume(120)==1 && consume(125)==0 && !runtime.buffer.pending());

    // A stale pressed report is discarded as well. No old Down is replayed.
    reset_runtime();
    steps={{portMAX_DELAY,0,true,ESP_OK,0}, {0,0,false,ESP_OK,1,20,30},
           {0,5,false,ESP_OK,0}, {5,5,false,ESP_OK,1,50,60}, {5,5,false,ESP_OK,0}};
    after_get_data=cycle_display_during_cached_report;
    run_sampler();
    assert(adapter_notifications==3 && consume(110)==1 && consume(115)==0);

    // An I2C error spanning the transition cannot enqueue a cancellation in
    // the new screen generation or discard its next healthy physical tap.
    reset_runtime();
    steps={{portMAX_DELAY,0,true,ESP_OK,0}, {0,0,false,ESP_OK,1,20,30},
           {5,1001,false,ESP_ERR_INVALID_RESPONSE,1,900,900},
           {0,5,false,ESP_OK,1,50,60}, {5,5,false,ESP_OK,0},
           {5,5,false,ESP_OK,1,70,80}, {5,5,false,ESP_OK,0}};
    after_read=cycle_display_during_stalled_read;
    run_sampler();
    assert(cancellations==0 && adapter_notifications==4);
    assert(consume(1116)==1 && consume(1121)==0 && !runtime.buffer.pending());

    // A transition after the post-I2C guard but before publication is caught
    // by the generation check under the same lock used by screen changes.
    reset_runtime();
    assert(chameleon_touch_start(&touch,&input)==ESP_OK);
    const auto generation=runtime.screen_generation.load();
    chameleon_touch_set_screen_off(true);
    assert(!cancel_report(100,generation) && !runtime.buffer.pending());
    chameleon_touch_set_screen_off(false);
    const auto notifications=adapter_notifications;
    assert(!record_report(0,0,false,100,generation));
    assert(!cancel_report(100,generation));
    assert(runtime.discard_until_release && !runtime.buffer.pending()
        && adapter_notifications==notifications && consume(0)==0);
    assert(record_current_report(20,30,true,110) && consume(0)==0);
    assert(record_current_report(20,30,false,115) && consume(0)==0);
    assert(record_current_report(40,50,true,120));
    assert(record_current_report(40,50,false,125));
    assert(consume(120)==1 && consume(125)==0);
    chameleon_touch_set_screen_off(false); // Repeated state does not create an epoch.
    assert(runtime.screen_generation.load()==2 && !irq_disabled);

    // An idle LVGL pointer has no gesture to cancel. Physical-button wake
    // followed by its first normal tap is immediately available to LVGL.
    reset_runtime();
    assert(chameleon_touch_start(&touch,&input)==ESP_OK);
    chameleon_touch_set_screen_off(true);
    assert(cancellations==0 && !input.waiting_release);
    chameleon_touch_set_screen_off(false);
    assert(record_current_report(0,0,false,100) && consume(0)==0);
    assert(record_current_report(20,30,true,105));
    assert(consume(105)==1 && !input.waiting_release);
    assert(record_current_report(20,30,false,110) && consume(110)==0);

    // A consumed active press is cancelled on off. If it lifts while dark,
    // the first valid release after button wake proactively notifies LVGL,
    // so it does not wait until swallowing the user's next complete tap.
    reset_runtime();
    assert(chameleon_touch_start(&touch,&input)==ESP_OK);
    assert(record_current_report(20,30,true,100) && consume(100)==1);
    chameleon_touch_set_screen_off(true);
    assert(cancellations==1 && input.waiting_release);
    assert(!record_current_report(20,30,false,105) && input.waiting_release);
    chameleon_touch_set_screen_off(false);
    const auto before_release=adapter_notifications;
    assert(record_current_report(20,30,false,110));
    assert(adapter_notifications==before_release+1 && consume(0)==0 && !input.waiting_release);
    assert(record_current_report(40,50,true,115));
    assert(consume(115)==1 && !input.waiting_release);
    assert(record_current_report(40,50,false,120) && consume(120)==0);

    // A finger still held at physical-button wake cannot invoke a control.
    // Only the real lift notifies the reader and releases both gates.
    reset_runtime();
    assert(chameleon_touch_start(&touch,&input)==ESP_OK);
    assert(record_current_report(20,30,true,100) && consume(100)==1);
    chameleon_touch_set_screen_off(true);
    chameleon_touch_set_screen_off(false);
    const auto before_held=adapter_notifications;
    assert(record_current_report(20,30,true,105));
    assert(runtime.discard_until_release && input.waiting_release
        && adapter_notifications==before_held && !runtime.buffer.pending());
    assert(record_current_report(20,30,false,110));
    assert(adapter_notifications==before_held+1 && consume(0)==0 && !input.waiting_release);
    assert(record_current_report(40,50,true,115));
    assert(consume(115)==1 && !input.waiting_release);
    assert(record_current_report(40,50,false,120) && consume(120)==0);
}
"""


class TouchSamplerChecks(unittest.TestCase):
    def compile_and_run(self, source, extra_headers=None):
        compiler = shutil.which("c++") or shutil.which("clang++")
        if not compiler:
            self.skipTest("C++ compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="chameleon-touch-sampler-") as folder:
            tmp = Path(folder)
            for name, content in (extra_headers or {}).items():
                target = tmp / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(content)
            cpp = tmp / "check.cpp"
            cpp.write_text(source)
            executable = tmp / "check"
            result = subprocess.run(
                [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(tmp),
                 "-I", str(ROOT / "main"), str(cpp), "-o", str(executable)],
                text=True, capture_output=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_real_fifo_preserves_edges_and_cancels_overflow(self):
        self.compile_and_run(BUFFER_SCENARIOS)

    def test_real_sampler_short_taps_dark_gate_and_startup(self):
        headers = {name: '#include "transport_stubs.h"\n' for name in (
            "esp_err.h", "esp_lcd_touch.h", "lvgl.h", "esp_lv_adapter.h",
            "esp_log.h", "esp_timer.h", "freertos/FreeRTOS.h", "freertos/task.h",
            "driver/gpio.h",
        )}
        headers["transport_stubs.h"] = TRANSPORT_STUBS
        self.compile_and_run(SAMPLER_SCENARIOS, headers)

    def test_inflight_screen_generation_preserves_held_finger_and_cancellation_gate(self):
        headers = {name: '#include "transport_stubs.h"\n' for name in (
            "esp_err.h", "esp_lcd_touch.h", "lvgl.h", "esp_lv_adapter.h",
            "esp_log.h", "esp_timer.h", "freertos/FreeRTOS.h", "freertos/task.h",
            "driver/gpio.h",
        )}
        headers["transport_stubs.h"] = TRANSPORT_STUBS
        self.compile_and_run(TRANSITION_SCENARIOS, headers)


if __name__ == "__main__":
    unittest.main()
