#include "chameleon_hardware.h"
#include "chameleon_hardware_schedule.h"
#include "chameleon_audio.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/time.h>

#include "bsp/esp-bsp.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_attr.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

namespace ch_hw {
namespace {
constexpr char TAG[] = "ch_hw";
constexpr char AP_SSID[] = "Juanjuan-Setup";
constexpr char AP_PASSWORD[] = "juanjuan";
constexpr int64_t WEATHER_FRESH_MS = 45LL * 60 * 1000;
constexpr int64_t WEATHER_REFRESH_MS = 15LL * 60 * 1000;
struct City { const char *name; double latitude; double longitude; };
constexpr City CITIES[] = {{"汕头",23.3541,116.6822}, {"北京",39.9042,116.4074},
                           {"上海",31.2304,121.4737}, {"广州",23.1291,113.2644}};
struct NetworkConfig { char ssid[33]{}; char password[65]{}; int city = 0; };
SemaphoreHandle_t status_mutex = nullptr;
SemaphoreHandle_t config_mutex = nullptr;
QueueHandle_t sound_queue = nullptr;
std::atomic<bool> sound_enabled{true};
std::atomic<int> effects_volume{ch_audio::kDefaultEffects}, alarm_volume{ch_audio::kDefaultAlarm};
std::atomic<uint32_t> sound_generation{0}, alarm_sound_generation{0};
struct SoundRequest {
    Sound sound;
    bool mute_confirmation;
    uint32_t generation;
    uint32_t alarm_generation;
};
bool sound_request_current(const SoundRequest &request) {
    return request.generation==sound_generation.load()&&(sound_enabled.load()||request.mute_confirmation)
        &&(request.sound!=Sound::Alarm||request.alarm_generation==alarm_sound_generation.load());
}
// A tap takes 60 ms including its release silence. This accommodates ordinary
// repeated presses and bursts without blocking the display task or throttling
// away accepted input events.
constexpr UBaseType_t SOUND_QUEUE_LENGTH = 32;
Status status;
NetworkConfig network;
std::atomic<bool> started{false}, provision_wanted{false};
std::atomic<bool> config_changed{false}, weather_requested{false}, connected{false};
std::atomic<bool> reconnect_requested{false}, status_refresh_requested{false};
std::atomic<int> requested_brightness{-1};
std::atomic<TaskHandle_t> network_task_handle{nullptr};
std::atomic<TaskHandle_t> hardware_task_handle{nullptr};
std::atomic<void (*)()> ui_wake_callback{nullptr};
QueueHandle_t key_edges=nullptr;
std::atomic<bool> key_edges_lost{false};
i2c_master_dev_handle_t pmu = nullptr;
bool wifi_ready = false, wifi_started = false, ap_active = false;
httpd_handle_t http_server = nullptr;
char setup_token[20]{};
int64_t weather_received_ms = 0;

struct Guard {
    SemaphoreHandle_t mutex;
    explicit Guard(SemaphoreHandle_t value): mutex(value) { if(mutex) xSemaphoreTake(mutex,portMAX_DELAY); }
    ~Guard() { if(mutex) xSemaphoreGive(mutex); }
};
int64_t now_ms() { return esp_timer_get_time()/1000; }
void wake_network_task() {
    if(const auto task=network_task_handle.load()) xTaskNotifyGive(task);
}
void message(const char *value) { Guard lock(status_mutex); snprintf(status.message,sizeof(status.message),"%s",value); }
NetworkConfig net_config() { Guard lock(config_mutex); return network; }
bool valid_epoch(time_t value) { return value >= 1577836800 && value < 4102444800LL; }
void poll_clock() {
    const time_t current = time(nullptr);
    struct tm local{};
    const bool valid = valid_epoch(current) && localtime_r(&current,&local);
    Guard lock(status_mutex);
    status.time_valid = valid;
    status.epoch_seconds = valid ? static_cast<int64_t>(current) : 0;
    if(valid) {
        status.year=local.tm_year+1900; status.month=local.tm_mon+1; status.day=local.tm_mday;
        status.hour=local.tm_hour; status.minute=local.tm_min; status.weekday=local.tm_wday;
        if(status.weather_valid && (current-status.weather_updated_at>45*60 || status.weather_updated_at-current>10*60)) {
            status.weather_valid=false;
            snprintf(status.weather,sizeof(status.weather),"天气已过期");
        }
    }
}
bool read_reg(i2c_master_dev_handle_t dev,uint8_t reg,void *out,size_t count) {
    return dev && i2c_master_transmit_receive(dev,&reg,1,static_cast<uint8_t *>(out),count,60)==ESP_OK;
}
i2c_master_dev_handle_t device_at(uint8_t address) {
    const auto bus=bsp_i2c_get_handle();
    if(!bus || i2c_master_probe(bus,address,60)!=ESP_OK) return nullptr;
    i2c_device_config_t cfg{};
    cfg.dev_addr_length=I2C_ADDR_BIT_LEN_7; cfg.device_address=address; cfg.scl_speed_hz=400000;
    i2c_master_dev_handle_t dev=nullptr;
    if(i2c_master_bus_add_device(bus,&cfg,&dev)!=ESP_OK) return nullptr;
    return dev;
}
void init_pmu() {
    pmu=device_at(0x34);
    uint8_t identity=0;
    if(!read_reg(pmu,0x03,&identity,1) || identity!=0x4A) {
        if(pmu) i2c_master_bus_rm_device(pmu);
        pmu=nullptr;
        ESP_LOGW(TAG,"AXP2101 unavailable; battery remains unknown");
    } else ESP_LOGI(TAG,"AXP2101 IC_TYPE=0x%02x",identity);
    // Read-only power monitoring: preserve manufacturer's charging/regulator settings.
}
void poll_power_status(bool refresh_percentage) {
    uint8_t values[2]{}, percent=0;
    const bool present=read_reg(pmu,0x00,values,2) && (values[0]&0x08);
    bool previous_valid=false;
    { Guard lock(status_mutex); previous_valid=status.battery_valid; }
    // Keep plug / charging changes at five seconds. The percentage transaction
    // is thirty-second work, except for an explicit wake or a newly seen battery.
    const bool read_percentage=present&&(refresh_percentage||!previous_valid);
    const bool valid=present&&(read_percentage?
        read_reg(pmu,0xA4,&percent,1)&&percent<=100:previous_valid);
    Guard lock(status_mutex);
    status.battery_valid=valid;
    status.charging=valid && ((values[1]>>5)&3)==1;
    if(valid&&read_percentage) status.battery_percent=percent;
}
struct Key {
    gpio_num_t pin;
    bool active_high;
    uint8_t index;
    bool interrupt_ready=false;
    detail::DebouncedKey debounce{};
};
Key pwr_key{GPIO_NUM_3,true,0},boot_key{GPIO_NUM_0,false,1};
struct KeyEdge { int64_t at; uint8_t index; bool pressed; };
bool key_level(const Key &key){return (gpio_get_level(key.pin)!=0)==key.active_high;}
void IRAM_ATTR key_edge_isr(void *arg) {
    const auto *key=static_cast<const Key *>(arg);
    const KeyEdge edge{esp_timer_get_time()/1000,key->index,
                      (gpio_get_level(key->pin)!=0)==key->active_high};
    BaseType_t higher=pdFALSE;
    if(xQueueSendFromISR(key_edges,&edge,&higher)!=pdTRUE)key_edges_lost.store(true);
    if(const auto task=hardware_task_handle.load())vTaskNotifyGiveFromISR(task,&higher);
    if(higher==pdTRUE)portYIELD_FROM_ISR();
}
void init_keys(){
    // Official 1.75C schematic: Key2 lowers AXP PWRON; its transistor raises
    // SYS_OUT on GPIO3 while held. BOOT is GPIO0 with an external pull-up.
    // Input mode is essential: GPIO0 is a boot strap and SYS_OUT is PMU linked.
    gpio_config_t cfg{};cfg.pin_bit_mask=1ULL<<GPIO_NUM_3;cfg.mode=GPIO_MODE_INPUT;
    cfg.pull_up_en=GPIO_PULLUP_DISABLE;cfg.pull_down_en=GPIO_PULLDOWN_DISABLE;
    cfg.intr_type=GPIO_INTR_DISABLE;
    if(gpio_config(&cfg)!=ESP_OK){ESP_LOGW(TAG,"PWR input unavailable");pwr_key.pin=GPIO_NUM_NC;}
    cfg.pin_bit_mask=1ULL<<GPIO_NUM_0;cfg.pull_up_en=GPIO_PULLUP_ENABLE;
    if(gpio_config(&cfg)!=ESP_OK){ESP_LOGW(TAG,"BOOT input unavailable");boot_key.pin=GPIO_NUM_NC;}
    for(auto *key:{&pwr_key,&boot_key})if(key->pin!=GPIO_NUM_NC){
        key->debounce.init(key_level(*key),now_ms());
    }
    if(!key_edges)return;
    const esp_err_t result=gpio_install_isr_service(0);
    if(result!=ESP_OK&&result!=ESP_ERR_INVALID_STATE){ESP_LOGW(TAG,"Key IRQ service unavailable; using fallback");return;}
    for(auto *key:{&pwr_key,&boot_key})if(key->pin!=GPIO_NUM_NC){
        if(gpio_isr_handler_add(key->pin,key_edge_isr,key)==ESP_OK &&
           gpio_set_intr_type(key->pin,GPIO_INTR_ANYEDGE)==ESP_OK && gpio_intr_enable(key->pin)==ESP_OK){
            key->interrupt_ready=true;
            key->debounce.observe(key_level(*key),now_ms());
        }else{gpio_intr_disable(key->pin);ESP_LOGW(TAG,"GPIO%d key IRQ unavailable; using fallback",key->pin);}
    }
}
// The pet no longer uses tilt. Leave the IMU uninitialised and do not poll it.
// Power management remains read-only; display requests are served by the UI.
void hardware_task(void *) {
    hardware_task_handle.store(xTaskGetCurrentTaskHandle());
    key_edges=xQueueCreate(32,sizeof(KeyEdge));
    if(!key_edges)ESP_LOGW(TAG,"Key edge queue unavailable; using fallback");
    init_pmu();init_keys();
    int64_t battery_at=0,charging_at=0,clock_at=0;
    while(true) {
        uint32_t presses[2]{};
        KeyEdge edge{};
        while(key_edges&&xQueueReceive(key_edges,&edge,0)==pdTRUE){
            auto &key=edge.index==0?pwr_key:boot_key;
            // Settle the previous level using the next edge's real timestamp,
            // before observing its new level; queued complete taps still count.
            if(key.debounce.settle(edge.at))++presses[key.index];
            key.debounce.observe(edge.pressed,edge.at);
        }
        const int64_t current=now_ms();
        const bool lost=key_edges_lost.exchange(false);
        for(auto *key:{&pwr_key,&boot_key})if(key->pin!=GPIO_NUM_NC){
            if(lost){key->debounce.init(key_level(*key),current);presses[key->index]=0;}
            else{
                if(!key->interrupt_ready)key->debounce.observe(key_level(*key),current);
                if(key->debounce.settle(current))++presses[key->index];
            }
        }
        if(lost)ESP_LOGW(TAG,"Key edge overflow; held keys rearmed after release");
        if(presses[0]||presses[1]){
            {Guard lock(status_mutex);status.pwr_presses+=presses[0];status.boot_presses+=presses[1];}
            if(const auto callback=ui_wake_callback.load())callback();
        }
        const bool refresh=status_refresh_requested.exchange(false);
        if(refresh||current>=charging_at||current>=battery_at){
            const bool percentage=refresh||current>=battery_at;
            poll_power_status(percentage);
            charging_at=current+detail::kChargingPeriodMs;
            if(percentage)battery_at=current+detail::kBatteryPeriodMs;
        }
        if(refresh)clock_at=0;
        if(current>=clock_at) { poll_clock(); clock_at=current+1000; }
        const int64_t wait_at=now_ms();
        uint32_t wait=detail::wait_until(wait_at,clock_at,detail::kChargingPeriodMs);
        wait=detail::wait_until(wait_at,charging_at,wait);
        wait=detail::wait_until(wait_at,battery_at,wait);
        for(const auto *key:{&pwr_key,&boot_key})if(key->pin!=GPIO_NUM_NC){
            if(!key->interrupt_ready)wait=std::min<uint32_t>(wait,20); // Fault fallback only.
            if(key->debounce.deadline())wait=detail::wait_until(wait_at,key->debounce.deadline(),wait);
        }
        ulTaskNotifyTake(pdTRUE,std::max<TickType_t>(1,pdMS_TO_TICKS(wait)));
    }
}

void audio_task(void *) {
    esp_codec_dev_handle_t speaker=nullptr;
    bool opened=false;
    int applied_volume=-1;
    SoundRequest request{Sound::Tap,false,0,0};
    while(true) {
        // Keep the codec open during a burst so every button does not pay its
        // I2C / amplifier startup cost. Shut it down after a quiet second.
        if(xQueueReceive(sound_queue,&request,opened?pdMS_TO_TICKS(1000):portMAX_DELAY)!=pdTRUE) {
            if(opened) esp_codec_dev_close(speaker);
            opened=false;continue;
        }
        if(!sound_request_current(request))continue;
        const Sound selected=request.sound;
        const int desired_volume=selected==Sound::Alarm?alarm_volume.load():effects_volume.load();
        if(desired_volume==0){if(opened)esp_codec_dev_close(speaker);opened=false;continue;}
        if(!speaker) speaker=bsp_audio_codec_speaker_init();
        esp_codec_dev_sample_info_t format{};
        format.sample_rate=22050; format.channel=1; format.bits_per_sample=16;
        if(!speaker || (!opened && esp_codec_dev_open(speaker,&format)!=ESP_CODEC_DEV_OK)) {
            { Guard lock(status_mutex); status.audio_ready=false; }
            ESP_LOGW(TAG,"Speaker unavailable; feedback continues visually");
            continue;
        }
        // Switching alarm/effect levels must also work while the codec stays
        // open for a burst. Keep all I2C volume writes on this task.
        if(!opened || applied_volume!=desired_volume) {
            if(esp_codec_dev_set_out_vol(speaker,desired_volume)!=ESP_CODEC_DEV_OK) {
                esp_codec_dev_close(speaker);opened=false;
                { Guard lock(status_mutex); status.audio_ready=false; }
                ESP_LOGW(TAG,"Speaker volume unavailable");continue;
            }
            applied_volume=desired_volume;
        }
        opened=true;
        { Guard lock(status_mutex); status.audio_ready=true; }
        constexpr int CHUNK=ch_audio::kChunk;
        const int note_samples=selected==Sound::Tap?882:selected==Sound::Alarm?4410:1764;
        int16_t pcm[CHUNK];
        const int notes=selected==Sound::Tap ? 1 : selected==Sound::Alarm ? 3 : 2;
        bool wrote=true;
        for(int note=0; note<notes && wrote && sound_request_current(request); ++note) {
            const float frequency=(selected==Sound::Feed ? 680.0f : selected==Sound::Alarm ? 880.0f : 740.0f)+note*180.0f;
            for(int offset=0;offset<note_samples && wrote && sound_request_current(request);offset+=CHUNK) {
                for(int index=0;index<CHUNK;++index) {
                    const int sample=offset+index;
                    pcm[index]=ch_audio::sample(sample,note_samples,frequency,selected==Sound::Alarm);
                }
                wrote=esp_codec_dev_write(speaker,pcm,sizeof(pcm))==ESP_CODEC_DEV_OK;
            }
        }
        memset(pcm,0,sizeof(pcm));
        const bool released=esp_codec_dev_write(speaker,pcm,sizeof(pcm))==ESP_CODEC_DEV_OK;
        wrote=wrote&&released;
        if(!wrote || !sound_request_current(request) || !sound_enabled.load()) {
            esp_codec_dev_close(speaker);opened=false;
            if(!wrote) { Guard lock(status_mutex); status.audio_ready=false; }
        }
    }
}

void wifi_event(void *,esp_event_base_t base,int32_t id,void *data) {
    if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) {
        auto *event=static_cast<ip_event_got_ip_t *>(data);
        connected=true; weather_requested=true;
        {
            Guard lock(status_mutex);
            status.wifi_connected=true;
            if(!status.provisioning) snprintf(status.ip,sizeof(status.ip),IPSTR,IP2STR(&event->ip_info.ip));
            snprintf(status.message,sizeof(status.message),"Wi-Fi 已连接，正在校时和获取天气");
        }
        ESP_LOGI(TAG,"Wi-Fi connected");
        wake_network_task();
    } else if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        // A connection that was working gets one immediate recovery attempt.
        // Failed attempts keep their existing backoff instead of resetting it
        // on every DISCONNECTED event emitted by a scan / association failure.
        if(connected.exchange(false))reconnect_requested=true;
        {
            Guard lock(status_mutex); status.wifi_connected=false;
            if(!status.provisioning) status.ip[0]=0;
        }
        wake_network_task();
    }
}
bool init_wifi() {
    esp_err_t result=esp_netif_init();
    if(result!=ESP_OK && result!=ESP_ERR_INVALID_STATE) return false;
    result=esp_event_loop_create_default();
    if(result!=ESP_OK && result!=ESP_ERR_INVALID_STATE) return false;
    if(!esp_netif_create_default_wifi_sta() || !esp_netif_create_default_wifi_ap()) return false;
    wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT();
    if(esp_wifi_init(&cfg)!=ESP_OK) return false;
    esp_wifi_set_storage(WIFI_STORAGE_RAM); // Credentials have one controlled NVS owner.
    esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,nullptr);
    esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,nullptr);
    return true;
}
void load_network() {
    nvs_handle_t handle;
    if(nvs_open("ch_net",NVS_READONLY,&handle)!=ESP_OK) return;
    NetworkConfig loaded;
    size_t length=sizeof(loaded.ssid); nvs_get_str(handle,"ssid",loaded.ssid,&length);
    length=sizeof(loaded.password); nvs_get_str(handle,"password",loaded.password,&length);
    int32_t city=0; nvs_get_i32(handle,"city",&city); loaded.city=std::clamp(int(city),0,3);
    nvs_close(handle);
    Guard lock(config_mutex); network=loaded;
}
bool save_network(const NetworkConfig &value) {
    nvs_handle_t handle;
    if(nvs_open("ch_net",NVS_READWRITE,&handle)!=ESP_OK) return false;
    bool success=nvs_set_str(handle,"ssid",value.ssid)==ESP_OK
              && nvs_set_str(handle,"password",value.password)==ESP_OK
              && nvs_set_i32(handle,"city",value.city)==ESP_OK
              && nvs_commit(handle)==ESP_OK;
    nvs_close(handle);
    return success;
}
bool configure_station() {
    const auto cfg=net_config();
    if(!cfg.ssid[0]) return false;
    if(wifi_started) esp_wifi_disconnect();
    if(esp_wifi_set_mode(ap_active ? WIFI_MODE_APSTA : WIFI_MODE_STA)!=ESP_OK) return false;
    wifi_config_t station{};
    memcpy(station.sta.ssid,cfg.ssid,strlen(cfg.ssid));
    memcpy(station.sta.password,cfg.password,strlen(cfg.password));
    station.sta.scan_method=WIFI_ALL_CHANNEL_SCAN;
    station.sta.sort_method=WIFI_CONNECT_AP_BY_SIGNAL;
    station.sta.threshold.authmode=cfg.password[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    station.sta.pmf_cfg.capable=true; station.sta.pmf_cfg.required=false;
    if(esp_wifi_set_config(WIFI_IF_STA,&station)!=ESP_OK) return false;
    if(!wifi_started) {
        if(esp_wifi_start()!=ESP_OK) return false;
        wifi_started=true;
    }
    const auto result=esp_wifi_connect();
    if(result!=ESP_OK)ESP_LOGW(TAG,"Initial station attempt failed: %s",esp_err_to_name(result));
    // A configured station remains retryable even when the first attempt fails.
    return true;
}

constexpr char SETUP_PAGE[] = R"HTML(<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>卷卷的森林设置</title><style>body{font-family:system-ui;background:#eef4e6;color:#24452f;max-width:430px;margin:40px auto;padding:20px}input,select,button{box-sizing:border-box;width:100%;padding:13px;margin:7px 0;border:1px solid #bfd1bc;border-radius:12px;font:inherit;background:white}button{background:#456f43;color:white}small{display:block;line-height:1.7}#note{min-height:3em;white-space:pre-line}</style><h1>卷卷的森林设置</h1><p>连接家里的 2.4 GHz Wi-Fi，让森林随当地时间和天气生活。</p><form id="form"><label>Wi-Fi 名称<input id="ssid" maxlength="32" autocomplete="off"></label><label>Wi-Fi 密码<input id="password" type="password" maxlength="63" autocomplete="new-password"></label><label>天气城市<select id="city"><option value="0">汕头</option><option value="1">北京</option><option value="2">上海</option><option value="3">广州</option></select></label><button>保存 Wi-Fi 和城市</button></form><button id="clock">同步本设备当前时间</button><small>时间采用北京时间。断开所有电源后，需联网校时或再次同步。天气来自 Open-Meteo；无法获取时显示未知。Wi-Fi 密码仅存于开发板本地，不会显示在页面。</small><p id="note">正在连接开发板…</p><script>let token='';const note=document.querySelector('#note');async function send(data){try{const response=await fetch('/api/setup',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({...data,token})});const result=await response.json();note.textContent=result.message;if(!response.ok)throw Error(result.message)}catch(error){note.textContent=error.message}}document.querySelector('#form').onsubmit=event=>{event.preventDefault();send({ssid:document.querySelector('#ssid').value,password:document.querySelector('#password').value,city:Number(document.querySelector('#city').value)})};document.querySelector('#clock').onclick=()=>send({epoch:Math.floor(Date.now()/1000)});fetch('/api/status').then(response=>response.json()).then(value=>{token=value.token;document.querySelector('#city').value=value.city;note.textContent=value.connected?'Wi-Fi 已连接':'可先同步时间，再配置 Wi-Fi';}).catch(()=>note.textContent='无法连接开发板，请重新打开 192.168.4.1');</script></html>)HTML";
esp_err_t page_handler(httpd_req_t *request) {
    httpd_resp_set_type(request,"text/html; charset=utf-8");
    httpd_resp_set_hdr(request,"Cache-Control","no-store");
    return httpd_resp_send(request,SETUP_PAGE,HTTPD_RESP_USE_STRLEN);
}
esp_err_t status_handler(httpd_req_t *request) {
    char json[150]; const auto cfg=net_config();
    snprintf(json,sizeof(json),"{\"token\":\"%s\",\"city\":%d,\"connected\":%s}",setup_token,cfg.city,connected ? "true":"false");
    httpd_resp_set_type(request,"application/json");
    httpd_resp_set_hdr(request,"Cache-Control","no-store");
    return httpd_resp_send(request,json,HTTPD_RESP_USE_STRLEN);
}
esp_err_t setup_response(httpd_req_t *request,bool success,const char *text) {
    if(!success) httpd_resp_set_status(request,"400 Bad Request");
    httpd_resp_set_type(request,"application/json; charset=utf-8");
    httpd_resp_set_hdr(request,"Cache-Control","no-store");
    cJSON *body=cJSON_CreateObject(); cJSON_AddBoolToObject(body,"ok",success); cJSON_AddStringToObject(body,"message",text);
    char *json=cJSON_PrintUnformatted(body);
    const auto result=httpd_resp_send(request,json ? json : "{}",HTTPD_RESP_USE_STRLEN);
    cJSON_free(json); cJSON_Delete(body); return result;
}
esp_err_t setup_handler(httpd_req_t *request) {
    if(request->content_len<2 || request->content_len>1023) return setup_response(request,false,"请求过长或为空");
    char body[1024]; size_t received=0;
    while(received<request->content_len) {
        const int bytes=httpd_req_recv(request,body+received,request->content_len-received);
        if(bytes<=0) return setup_response(request,false,"接收失败，请重试");
        received+=bytes;
    }
    body[received]=0;
    cJSON *json=cJSON_Parse(body);
    if(!json) return setup_response(request,false,"设置格式无效");
    const auto *token=cJSON_GetObjectItemCaseSensitive(json,"token");
    if(!cJSON_IsString(token) || strcmp(token->valuestring,setup_token)!=0) {
        cJSON_Delete(json); return setup_response(request,false,"请刷新设置页后再试");
    }
    bool success=false; const char *result="设置格式无效";
    const auto *epoch=cJSON_GetObjectItemCaseSensitive(json,"epoch");
    const auto *ssid=cJSON_GetObjectItemCaseSensitive(json,"ssid");
    if(cJSON_IsNumber(epoch) && std::isfinite(epoch->valuedouble) && epoch->valuedouble>=1577836800.0 && epoch->valuedouble<4102444800.0) {
        struct timeval value{static_cast<time_t>(epoch->valuedouble),0};
        success=settimeofday(&value,nullptr)==0;
        result=success ? "时间已同步为北京时间" : "时间同步失败";
        poll_clock();
    } else if(cJSON_IsString(ssid)) {
        const auto *password=cJSON_GetObjectItemCaseSensitive(json,"password");
        const auto *city=cJSON_GetObjectItemCaseSensitive(json,"city");
        const size_t ssid_len=strlen(ssid->valuestring);
        const size_t password_len=cJSON_IsString(password) ? strlen(password->valuestring) : 1000;
        if(ssid_len<=32 && (password_len==0 || (password_len>=8 && password_len<=63)) && cJSON_IsNumber(city)
           && city->valuedouble>=0 && city->valuedouble<=3 && city->valuedouble==city->valueint) {
            auto cfg=net_config(); cfg.city=city->valueint;
            // Empty SSID updates the city while preserving an existing Wi-Fi credential.
            if(ssid_len) { snprintf(cfg.ssid,sizeof(cfg.ssid),"%s",ssid->valuestring); snprintf(cfg.password,sizeof(cfg.password),"%s",password->valuestring); }
            success=save_network(cfg);
            if(success) {
                { Guard lock(config_mutex); network=cfg; }
                { Guard lock(status_mutex); status.weather_valid=false; snprintf(status.weather_city,sizeof(status.weather_city),"%s",CITIES[cfg.city].name); }
                config_changed=true; weather_requested=true;
                wake_network_task();
            }
            result=success ? "设置已保存，正在连接。可返回开发板查看状态。" : "保存失败，请重试";
        } else result="请检查名称、密码（留空或 8 至 63 字符）和城市";
    }
    cJSON_Delete(json); return setup_response(request,success,result);
}
bool start_setup_server() {
    if(http_server) return true;
    httpd_config_t cfg=HTTPD_DEFAULT_CONFIG(); cfg.stack_size=6144; cfg.max_uri_handlers=4;
    if(httpd_start(&http_server,&cfg)!=ESP_OK) return false;
    httpd_uri_t page{}; page.uri="/"; page.method=HTTP_GET; page.handler=page_handler;
    httpd_uri_t state{}; state.uri="/api/status"; state.method=HTTP_GET; state.handler=status_handler;
    httpd_uri_t setup{}; setup.uri="/api/setup"; setup.method=HTTP_POST; setup.handler=setup_handler;
    if(httpd_register_uri_handler(http_server,&page)!=ESP_OK
       || httpd_register_uri_handler(http_server,&state)!=ESP_OK
       || httpd_register_uri_handler(http_server,&setup)!=ESP_OK) {
        httpd_stop(http_server); http_server=nullptr;
        return false;
    }
    return true;
}
void toggle_ap(bool enabled) {
    if(enabled==ap_active) return;
    const auto cfg=net_config();
    if(enabled) {
        if(esp_wifi_set_mode(cfg.ssid[0] ? WIFI_MODE_APSTA : WIFI_MODE_AP)!=ESP_OK) { message("热点启动失败"); return; }
        wifi_config_t ap{};
        memcpy(ap.ap.ssid,AP_SSID,sizeof(AP_SSID)); memcpy(ap.ap.password,AP_PASSWORD,sizeof(AP_PASSWORD));
        ap.ap.ssid_len=strlen(AP_SSID); ap.ap.channel=1; ap.ap.max_connection=2; ap.ap.authmode=WIFI_AUTH_WPA2_PSK;
        if(esp_wifi_set_config(WIFI_IF_AP,&ap)!=ESP_OK) { message("热点配置失败"); return; }
        if(!wifi_started) { if(esp_wifi_start()!=ESP_OK) { message("热点启动失败"); return; } wifi_started=true; }
        // Keep the state false until the HTTP service is ready. This makes a
        // failed start retryable on the next loop instead of leaving the UI
        // believing a dead provisioning service is active.
        if(!start_setup_server()) {
            { Guard lock(status_mutex); status.provisioning=false; status.ip[0]=0; }
            message("设置页面启动失败，稍后重试");
            return;
        }
        ap_active=true;
        Guard lock(status_mutex); status.provisioning=true;
        snprintf(status.ip,sizeof(status.ip),"192.168.4.1"); snprintf(status.message,sizeof(status.message),"连接 Juanjuan-Setup，密码 juanjuan");
    } else {
        if(http_server) { httpd_stop(http_server); http_server=nullptr; }
        ap_active=false;
        if(cfg.ssid[0]) esp_wifi_set_mode(WIFI_MODE_STA);
        else if(wifi_started) { esp_wifi_stop(); wifi_started=false; }
        Guard lock(status_mutex); status.provisioning=false; status.ip[0]=0;
        snprintf(status.message,sizeof(status.message),"设置热点已关闭");
    }
}
const char *weather_name(int code) {
    if(code==0) return "晴";
    if(code==1) return "晴间多云";
    if(code==2) return "多云";
    if(code==3) return "阴";
    if(code==45 || code==48) return "雾";
    if(code==51 || code==53 || code==55 || code==56 || code==57 || code==61 || code==63 || code==65 || code==66 || code==67 || (code>=80 && code<=82)) return "雨";
    if(code==71 || code==73 || code==75 || code==77 || code==85 || code==86) return "雪";
    if(code==95 || code==96 || code==99) return "雷雨";
    return nullptr;
}
struct HttpBody { char text[4096]{}; size_t length=0; bool overflow=false; };
esp_err_t weather_event(esp_http_client_event_t *event) {
    auto *body=static_cast<HttpBody *>(event->user_data);
    if(event->event_id==HTTP_EVENT_ON_DATA && event->data_len>0) {
        if(body->length+event->data_len>=sizeof(body->text)) body->overflow=true;
        else { memcpy(body->text+body->length,event->data,event->data_len); body->length+=event->data_len; body->text[body->length]=0; }
    }
    return ESP_OK;
}
bool fetch_weather() {
    const auto cfg=net_config(); const auto &city=CITIES[cfg.city];
    char url[300];
    snprintf(url,sizeof(url),"https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code,is_day&timezone=Asia%%2FShanghai&timeformat=unixtime",city.latitude,city.longitude);
    HttpBody body;
    esp_http_client_config_t request{};
    request.url=url; request.timeout_ms=6000; request.crt_bundle_attach=esp_crt_bundle_attach;
    request.event_handler=weather_event; request.user_data=&body; request.buffer_size=1024; request.buffer_size_tx=512;
    auto client=esp_http_client_init(&request);
    if(!client) return false;
    const esp_err_t result=esp_http_client_perform(client);
    const int response=esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if(result!=ESP_OK || response!=200 || body.overflow) { ESP_LOGW(TAG,"Weather request unavailable: %s HTTP %d",esp_err_to_name(result),response); return false; }
    cJSON *json=cJSON_Parse(body.text);
    const auto *current=cJSON_GetObjectItemCaseSensitive(json,"current");
    const auto *temperature=cJSON_GetObjectItemCaseSensitive(current,"temperature_2m");
    const auto *code=cJSON_GetObjectItemCaseSensitive(current,"weather_code");
    const auto *timestamp=cJSON_GetObjectItemCaseSensitive(current,"time");
    bool valid=cJSON_IsNumber(temperature) && std::isfinite(temperature->valuedouble)
            && temperature->valuedouble>=-60 && temperature->valuedouble<=65 && cJSON_IsNumber(code)
            && code->valuedouble==code->valueint && weather_name(code->valueint) && cJSON_IsNumber(timestamp)
            && std::isfinite(timestamp->valuedouble) && timestamp->valuedouble>=1577836800.0 && timestamp->valuedouble<4102444800.0;
    const time_t wall=time(nullptr);
    if(valid && valid_epoch(wall)) valid=wall-timestamp->valuedouble<=45*60 && timestamp->valuedouble-wall<=10*60;
    // A city change during HTTPS must not label the old location as the newly selected city.
    if(valid) valid=net_config().city==cfg.city;
    if(valid) {
        Guard lock(status_mutex);
        status.temp_c=temperature->valuedouble; status.weather_code=code->valueint;
        status.weather_updated_at=static_cast<int64_t>(timestamp->valuedouble);
        snprintf(status.weather,sizeof(status.weather),"%s",weather_name(code->valueint));
        snprintf(status.weather_city,sizeof(status.weather_city),"%s",city.name);
        status.weather_valid=true; weather_received_ms=now_ms();
    }
    cJSON_Delete(json); return valid;
}
void network_task(void *) {
    load_network(); const auto initial=net_config();
    { Guard lock(status_mutex); snprintf(status.weather_city,sizeof(status.weather_city),"%s",CITIES[initial.city].name); }
    snprintf(setup_token,sizeof(setup_token),"%08lx%08lx",static_cast<unsigned long>(esp_random()),static_cast<unsigned long>(esp_random()));
    wifi_ready=init_wifi();
    if(!wifi_ready) { message("网络服务不可用，宠物可离线生活"); vTaskDelete(nullptr); return; }
    // Publish only a service that will stay alive. Requests made during
    // startup remain in the atomic flags and are consumed by its first loop.
    network_task_handle.store(xTaskGetCurrentTaskHandle());
    bool station_configured=configure_station();
    if(!initial.ssid[0]) message("离线生活；设置中可连接 Wi-Fi 和校时");
    detail::ReconnectBackoff reconnect;
    reconnect.attempted(now_ms());
    detail::WeatherBackoff weather_backoff;
    int64_t next_weather_at=0;
    while(true) {
        const int64_t current=now_ms();
        toggle_ap(provision_wanted.load());
        const bool retry_now=reconnect_requested.exchange(false);
        if(config_changed.exchange(false)) {
            reconnect.reset(current);station_configured=configure_station();reconnect.attempted(now_ms());
        }else if(retry_now)reconnect.reset(current);
        if(connected)reconnect.reset(current);
        else if(station_configured&&current>=reconnect.at){
            const auto result=esp_wifi_connect();
            if(result!=ESP_OK)ESP_LOGW(TAG,"Station retry failed: %s",esp_err_to_name(result));
            reconnect.attempted(now_ms());
        }
        if(connected && !esp_sntp_enabled()) {
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0,"ntp.aliyun.com"); esp_sntp_init();
        }
        // Keep a manual refresh queued while offline; it must be consumed by
        // the first connected iteration rather than lost at disconnect time.
        const bool explicit_weather_request=connected && weather_requested.exchange(false);
        if(connected && (explicit_weather_request || current>=next_weather_at)) {
            if(explicit_weather_request) weather_backoff.reset();
            if(fetch_weather()) {
                weather_backoff.reset();
                if(!ap_active) message("天气已更新；森林随当地作息生活");
                next_weather_at=now_ms()+WEATHER_REFRESH_MS;
            }
            else {
                { Guard lock(status_mutex); status.weather_valid=false; snprintf(status.weather,sizeof(status.weather),"天气暂不可用"); }
                if(!ap_active) message("天气获取失败，稍后重试");
                next_weather_at=now_ms()+weather_backoff.failed_delay_ms();
            }
        }
        if(weather_received_ms && now_ms()-weather_received_ms>WEATHER_FRESH_MS) {
            Guard lock(status_mutex); status.weather_valid=false; snprintf(status.weather,sizeof(status.weather),"天气已过期");
        }
        const int64_t wait_at=now_ms();
        const bool online=connected.load();
        const detail::NetworkSchedule schedule{
            .station_configured=station_configured,
            .connected=online,
            .work_pending=config_changed.load()||reconnect_requested.load()||(online&&weather_requested.load()),
            .provisioning=ap_active,
            .reconnect_at=reconnect.at,
            .weather_at=next_weather_at,
            .weather_expires_at=weather_received_ms ? weather_received_ms+WEATHER_FRESH_MS : 0,
        };
        // Notifications cannot be lost between processing flags and waiting:
        // FreeRTOS retains the count until this take consumes it. Keep all
        // existing deadlines, with a thirty-second offline/online fallback.
        // Setup mode retains its original one-second fallback.
        const TickType_t wait_ticks=std::max<TickType_t>(1,pdMS_TO_TICKS(detail::network_wait_ms(wait_at,schedule)));
        ulTaskNotifyTake(pdTRUE,wait_ticks);
    }
}
} // namespace

void start() {
    if(started.exchange(true)) return;
    status_mutex=xSemaphoreCreateMutex(); config_mutex=xSemaphoreCreateMutex();
    if(!status_mutex || !config_mutex) { ESP_LOGE(TAG,"Hardware mutex allocation failed"); return; }
    setenv("TZ","CST-8",1); tzset();
    // No external RTC is present in the published 1.75C schematic. Cold-boot time
    // remains unknown until NTP or the explicit setup-page clock synchronization.
    poll_clock();
    if(xTaskCreate(hardware_task,"ch_status",3072,nullptr,3,nullptr)!=pdPASS) message("设备状态服务启动失败");
    if(xTaskCreate(network_task,"ch_network",12288,nullptr,2,nullptr)!=pdPASS) message("网络服务启动失败");
    sound_queue=xQueueCreate(SOUND_QUEUE_LENGTH,sizeof(SoundRequest));
    if(sound_queue && xTaskCreate(audio_task,"ch_sounds",4096,nullptr,2,nullptr)!=pdPASS) {
        vQueueDelete(sound_queue); sound_queue=nullptr;
        ESP_LOGW(TAG,"Audio worker unavailable");
    }
}
Status snapshot() { Guard lock(status_mutex); return status; }
void set_ui_wake_callback(void (*callback)()) { ui_wake_callback.store(callback); }
void request_status_refresh() {
    status_refresh_requested=true;
    if(const auto task=hardware_task_handle.load())xTaskNotifyGive(task);
}
void set_brightness(int percent) { requested_brightness=percent<=0 ? 0 : std::clamp(percent,10,100); }
void apply_display_requests() {
    // Called while the UI owns the LVGL lock, so panel commands cannot overlap
    // another task's draw. Key polling never waits for a display mutex.
    const int brightness=requested_brightness.exchange(-1);
    if(brightness<0) return;
    if(bsp_display_brightness_set(brightness)!=ESP_OK) {
        // Preserve a newer request, including a pending screen-off or wake-up.
        int empty_request=-1;
        requested_brightness.compare_exchange_strong(empty_request,brightness);
    }
}
void set_provisioning(bool enabled) { provision_wanted=enabled; wake_network_task(); }
bool set_clock(int year,int month,int day,int hour,int minute) {
    if(year<2020 || year>2099 || month<1 || month>12 || day<1 || day>31 || hour<0 || hour>23 || minute<0 || minute>59) return false;
    struct tm local{}; local.tm_year=year-1900; local.tm_mon=month-1; local.tm_mday=day; local.tm_hour=hour; local.tm_min=minute; local.tm_isdst=-1;
    const time_t epoch=mktime(&local);
    if(!valid_epoch(epoch) || local.tm_year!=year-1900 || local.tm_mon!=month-1 || local.tm_mday!=day) return false;
    struct timeval value{epoch,0};
    if(settimeofday(&value,nullptr)!=0) return false;
    poll_clock(); return true;
}
void refresh_weather() { weather_requested=true;reconnect_requested=true;wake_network_task(); }
void set_sound_enabled(bool enabled,bool final_tap) {
    sound_enabled.store(enabled);
    const uint32_t generation=sound_generation.fetch_add(1)+1;
    if(!sound_queue)return;
    // Preference changes cancel both waiting and already selected requests.
    // Checking the generation between chunks also catches a rapid off-on
    // pair; a boolean alone could allow the old sound to resume.
    xQueueReset(sound_queue);
    if(!enabled && final_tap){
        const SoundRequest confirmation{Sound::Tap,true,generation,0};
        if(xQueueSend(sound_queue,&confirmation,0)!=pdTRUE)ESP_LOGW(TAG,"Mute confirmation backlog full");
    }
}
void play_sound(Sound sound) {
    if(!sound_enabled.load() || !sound_queue)return;
    const SoundRequest request{sound,false,sound_generation.load(),alarm_sound_generation.load()};
    if(xQueueSend(sound_queue,&request,0)!=pdTRUE)ESP_LOGW(TAG,"Audio feedback backlog full");
}
void set_sound_volumes(int effects,int alarm) {
    effects_volume.store(ch_audio::volume(effects));
    alarm_volume.store(ch_audio::volume(alarm));
}
void cancel_alarm_sound() { alarm_sound_generation.fetch_add(1); }
} // namespace ch_hw
