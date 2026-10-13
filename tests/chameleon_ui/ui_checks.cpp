#include "lvgl.h"
#include "src/misc/lv_timer_private.h"
#include "chameleon_ui.h"
#include "chameleon_hardware.h"
#include "chameleon_display.h"
#include "chameleon_adapter.h"
#include "chameleon_power.h"
#include "chameleon_assets.h"
#include "chameleon_mood_color.h"
#include "chameleon_state.h"
#include "chameleon_games.h"
#include "chameleon_alarm.h"
#include "chameleon_audio.h"
#include "chameleon_forest.h"
#include "chameleon_touch_buffer.h"
#include "nvs.h"
#include "esp_timer.h"
#include "esp_err.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cstddef>
#include <cmath>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

static uint64_t clock_ms = 0;
static std::map<std::string, std::vector<uint8_t>> blobs;
static std::map<std::string,uint32_t> integer_values;
static int nvs_writes = 0, pet_nvs_writes=0, brightness = -1;
static bool fail_audio_write=false,fail_forest_write=false;
static unsigned step_clock_on_read=0;
static unsigned alarm_sound_cancellations=0;
static uint32_t adapter_max_wait=15;
static bool frequency_screen_off=false;
static bool touch_screen_off=false;
static unsigned adapter_notifications=0,status_refresh_requests=0;
void chameleon_lv_adapter_set_max_wait(uint32_t value){adapter_max_wait=value;}
void chameleon_lv_adapter_notify(){++adapter_notifications;}
void chameleon_power_set_screen_off(bool value){frequency_screen_off=value;}
extern "C" void chameleon_touch_set_screen_off(bool value){touch_screen_off=value;}
int64_t esp_timer_get_time() {
    // One test can model the real clock advancing between tick's timestamp
    // and touch(), without replacing the app's clock or key handlers.
    if(step_clock_on_read&&!--step_clock_on_read)++clock_ms;
    return static_cast<int64_t>(clock_ms)*1000;
}
uint32_t esp_random() { return 0xC0FFEEu; }

esp_err_t nvs_open(const char *, int, nvs_handle_t *handle) { *handle = 1; return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t, const char *key, void *data, size_t *size) {
    auto it = blobs.find(key);
    if (it == blobs.end()) return ESP_ERR_NVS_NOT_FOUND;
    if (!data) { *size = it->second.size(); return ESP_OK; }
    if (*size < it->second.size()) { *size = it->second.size(); return ESP_ERR_NVS_INVALID_LENGTH; }
    std::memcpy(data, it->second.data(), it->second.size()); *size = it->second.size(); return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t, const char *key, const void *data, size_t size) {
    if(fail_audio_write&&std::strcmp(key,"levels")==0)return ESP_FAIL;
    if(fail_forest_write&&std::strcmp(key,"forest")==0)return ESP_FAIL;
    const auto *bytes = static_cast<const uint8_t *>(data);
    blobs[key] = std::vector<uint8_t>(bytes, bytes + size); ++nvs_writes;if(std::strcmp(key,"state")==0)++pet_nvs_writes;return ESP_OK;
}
esp_err_t nvs_get_u32(nvs_handle_t,const char *key,uint32_t *value){auto it=integer_values.find(key);if(it==integer_values.end())return ESP_ERR_NVS_NOT_FOUND;*value=it->second;return ESP_OK;}
esp_err_t nvs_set_u32(nvs_handle_t,const char *key,uint32_t value){integer_values[key]=value;++nvs_writes;return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t) { return ESP_OK; }
esp_err_t nvs_erase_key(nvs_handle_t, const char *) { assert(false && "pet recovery must never erase NVS"); return ESP_FAIL; }

namespace ch_hw {
static Status status;
static int pending_brightness=-1;
static bool sound_enabled=true;
static std::vector<Sound> sound_requests;
static std::vector<std::pair<bool,bool>> sound_preferences;
static int effects_level=0,alarm_level=0;
static std::vector<int> played_levels;
static void (*ui_wake_callback)()=nullptr;
void set_ui_wake_callback(void (*callback)()){ui_wake_callback=callback;}
void request_status_refresh(){++status_refresh_requests;}
Status snapshot() { return status; }
void start() {}
void set_brightness(int value) { pending_brightness=value; }
void apply_display_requests() { if(pending_brightness>=0) { brightness=pending_brightness;pending_brightness=-1; } }
void set_provisioning(bool enabled) { status.provisioning = enabled; }
bool set_clock(int, int, int, int, int) { return true; }
void refresh_weather() {}
void set_sound_enabled(bool enabled,bool final_tap) {
    sound_enabled=enabled;sound_preferences.emplace_back(enabled,final_tap);
    if(!enabled&&final_tap)sound_requests.push_back(Sound::Tap);
}
void set_sound_volumes(int effects,int alarm) {effects_level=effects;alarm_level=alarm;}
void play_sound(Sound value) { if(sound_enabled){sound_requests.push_back(value);played_levels.push_back(value==Sound::Alarm?alarm_level:effects_level);} }
void cancel_alarm_sound() { ++alarm_sound_cancellations; }
}

// A separately frozen fixture matches the shipped v1 ABI, including its album.
struct LegacyState {
    int health=82,fullness=70,energy=65,mood=90,cleanliness=80;
    int coins=30,inventory[7]={3,3,2,4,1,2,1};
    bool sleeping=false;
    ch::SleepReason sleep_reason=ch::SleepReason::None;
    ch::Scene scene=ch::Scene::Forest,awake_scene=ch::Scene::Forest;
    bool auto_life=true,auto_world=true,sound=true;
    int brightness=80,idle_minutes=1;
    char name[40]="卷卷";
    uint32_t meals=0,strokes=0;
};
struct LegacySave { uint32_t magic=0x43484d31,version=1; LegacyState state; uint32_t count=0,serial=0,ids[20]{},crc=0; };
// Frozen shipped v2 fixture: do not derive its size from the growing State.
struct LegacyStateV2 {
    int health=82,fullness=70,energy=65,mood=90,cleanliness=80,coins=30;
    int inventory[7]={3,3,2,4,1,2,1};
    bool sleeping=false;ch::SleepReason sleep_reason=ch::SleepReason::None;
    ch::Scene scene=ch::Scene::Forest,awake_scene=ch::Scene::Forest;
    bool auto_life=true,auto_world=true,sound=true;int brightness=80,idle_minutes=1;
    char name[40]="卷卷";uint32_t meals=0,strokes=0;
    bool scene_locked=false,sick=false;ch::SicknessCause sickness_cause=ch::SicknessCause::None;
    uint32_t treatment_ms=0;uint8_t poop_count=0;
    uint32_t digestion_ms[3]{},neglect_ms[3]{},care_total=0,day_key=0;
    uint8_t daily_mask=0;bool daily_claimed=false;uint32_t daily_claims=0;
};
struct LegacySaveV2 { uint32_t magic=0x43484d31,version=2;LegacyStateV2 state;uint32_t count=0,serial=0,ids[20]{},crc=0; };
// Freeze V3 separately: deriving it from State would hide a broken migration
// when V4 appends affinity and the 64-bit companionship clock.
struct LegacyStateV3 {
    int health=82,fullness=70,energy=65,mood=90,cleanliness=80,coins=30;
    int inventory[7]={3,3,2,4,1,2,1};
    bool sleeping=false;ch::SleepReason sleep_reason=ch::SleepReason::None;
    ch::Scene scene=ch::Scene::Forest,awake_scene=ch::Scene::Forest;
    bool auto_life=true,auto_world=true,sound=true;int brightness=80,idle_minutes=1;
    char name[40]="卷卷";uint32_t meals=0,strokes=0;
    bool scene_locked=false,sick=false;ch::SicknessCause sickness_cause=ch::SicknessCause::None;
    uint32_t treatment_ms=0;uint8_t poop_count=0;
    uint32_t digestion_ms[3]{},neglect_ms[3]{},care_total=0,day_key=0;
    uint8_t daily_mask=0;bool daily_claimed=false;uint32_t daily_claims=0;
    uint32_t growth_ms=0;ch::GrowthSpeed growth_speed=ch::GrowthSpeed::Normal;
};
struct LegacySaveV3 {uint32_t magic=0x43484d31,version=3;LegacyStateV3 state;uint32_t count=0,serial=0,ids[20]{},crc=0;};
// Freeze the complete V4 layout independently: removing the gallery must
// migrate the pet rather than accepting a malformed old state as V5.
struct LegacyStateV4 {
    LegacyStateV3 care;
    uint16_t affinity=0;uint32_t affection_cooldown_ms=0;uint64_t companion_ms=0;
    uint8_t anniversary_claimed=0,encounter_claimed=0;uint32_t encounter_total=0;
    uint8_t photo_receipt=0;uint32_t photo_ids[2]{};
};
struct LegacySaveV4 {uint32_t magic=0x43484d31,version=4;LegacyStateV4 state;uint32_t count=0,serial=0,ids[20]{},crc=0;};
struct Save {uint32_t magic=0x43484d31,version=5;ch::State state;uint32_t crc=0;};
static_assert(sizeof(LegacyStateV4)==216&&sizeof(LegacySaveV4)==320&&offsetof(LegacySaveV4,crc)==312);
static_assert(sizeof(Save)==232&&offsetof(Save,crc)==224,"V5 stores the pet without an album index");
static_assert(sizeof(LegacyState)==116 && sizeof(LegacySave)==216);
static_assert(sizeof(LegacyStateV2)==168 && sizeof(LegacySaveV2)==268 && offsetof(LegacySaveV2,crc)==264);
static_assert(sizeof(LegacyStateV3)==176 && sizeof(LegacySaveV3)==276 && offsetof(LegacySaveV3,crc)==272);
static uint32_t crc32(const void *data,size_t size) {
    uint32_t crc=0xffffffff;auto *bytes=static_cast<const uint8_t *>(data);
    while(size--){crc^=*bytes++;for(int i=0;i<8;++i)crc=(crc>>1)^((crc&1)?0xedb88320:0);}
    return ~crc;
}
template<class T> static void seed(T &data) {
    data.crc=crc32(&data,offsetof(T,crc));
    const auto *bytes=reinterpret_cast<const uint8_t *>(&data);
    blobs["state"]=std::vector<uint8_t>(bytes,bytes+sizeof(data));
}
static Save saved() {
    assert(blobs["state"].size()==sizeof(Save));Save result{};
    std::memcpy(&result,blobs["state"].data(),sizeof(result));
    assert(result.version==5 && result.crc==crc32(&result,offsetof(Save,crc)));
    return result;
}
static void verified_day(int day=1) {
    auto &status=ch_hw::status;status.time_valid=true;status.year=2026;status.month=10;status.day=day;status.hour=12;status.minute=34;
}

// A frozen alarm ABI is independent of the production Scheduler types and
// the pet's V5 blob. Corruption of either must never overwrite the other.
struct AlarmFixture {
    uint32_t magic=0x43484131,version=1;
    uint8_t enabled=1,hour=8,minute=0,reserved=0;
    int32_t last_trigger_day=-1;
    uint32_t crc=0;
};
static_assert(sizeof(AlarmFixture)==20&&offsetof(AlarmFixture,crc)==16);
static void seed_alarm(AlarmFixture record={}) {
    record.crc=crc32(&record,offsetof(AlarmFixture,crc));
    const auto *bytes=reinterpret_cast<const uint8_t *>(&record);
    blobs["alarm"]={bytes,bytes+sizeof(record)};
}
static AlarmFixture saved_alarm() {
    assert(blobs.count("alarm")&&blobs["alarm"].size()==20);AlarmFixture record{};
    std::memcpy(&record,blobs["alarm"].data(),sizeof(record));
    assert(record.magic==0x43484131&&record.version==1&&record.crc==crc32(&record,offsetof(AlarmFixture,crc)));
    return record;
}
static bool wall_fixture=false;
static int64_t wall_epoch_start=0;
static uint64_t wall_monotonic_start=0;
struct AudioFixture {
    uint32_t magic=0x43484155,version=1;
    uint8_t effects=45,alarm=85;
    uint16_t reserved=0;
    uint32_t crc=0;
};
struct ForestFixture {uint32_t magic=0x43484631,version=1;ch_forest::State state;uint32_t crc=0;};
static_assert(sizeof(ForestFixture)==64&&offsetof(ForestFixture,crc)==60);
static void seed_forest(ch_forest::State state){
    ForestFixture record{};record.state=state;record.crc=crc32(&record,60);
    const auto *p=reinterpret_cast<const uint8_t *>(&record);blobs["forest"]={p,p+64};
}
static ForestFixture saved_forest(){
    assert(blobs.at("forest").size()==64);ForestFixture record{};std::memcpy(&record,blobs.at("forest").data(),64);
    assert(record.magic==0x43484631&&record.version==1&&record.crc==crc32(&record,60)&&ch_forest::valid_state(record.state));return record;
}
static unsigned forest_materials(const ch_forest::State &s){unsigned total=0;for(auto v:s.materials)total+=v;return total;}
static_assert(sizeof(AudioFixture)==16&&offsetof(AudioFixture,crc)==12);
static AudioFixture saved_audio() {
    assert(blobs.at("levels").size()==16);AudioFixture record{};
    std::memcpy(&record,blobs.at("levels").data(),16);
    assert(record.magic==0x43484155&&record.version==1&&record.reserved==0);
    assert(record.crc==crc32(&record,12));return record;
}
// UTC 2026-10-05 16:00 is Beijing 2026-10-06 00:00. An epoch source must
// advance even during display-off 1 Hz ticks; hour/minute alone cannot test it.
static constexpr int64_t kAlarmDateEpoch=1791216000;
static int64_t alarm_epoch(int day,int hour,int minute,int second=0) {
    return kAlarmDateEpoch+static_cast<int64_t>(day)*86400+hour*3600+minute*60+second;
}
static void update_wall_fixture() {
    if(!wall_fixture)return;
    auto &status=ch_hw::status;
    status.epoch_seconds=wall_epoch_start+static_cast<int64_t>((clock_ms-wall_monotonic_start)/1000);
    const auto local=static_cast<std::time_t>(status.epoch_seconds+8*3600);
    const auto *calendar=std::gmtime(&local);assert(calendar);
    status.year=calendar->tm_year+1900;status.month=calendar->tm_mon+1;status.day=calendar->tm_mday;
    status.weekday=calendar->tm_wday;status.hour=calendar->tm_hour;status.minute=calendar->tm_min;
}
static void wall_time(int64_t epoch,bool valid=true) {
    wall_fixture=true;wall_epoch_start=epoch;wall_monotonic_start=clock_ms;
    ch_hw::status.time_valid=valid;update_wall_fixture();
}
static unsigned alarm_notes() {
    return static_cast<unsigned>(std::count(ch_hw::sound_requests.begin(),ch_hw::sound_requests.end(),ch_hw::Sound::Alarm));
}

static std::vector<uint8_t> pixels(466 * 466 * 4);
static std::filesystem::path diagnostic_output;
static unsigned flush_calls = 0, partial_flush_calls = 0;
static lv_area_t last_flush_area{};
static void align_display_area(lv_event_t *event) {
    auto *area=static_cast<lv_area_t *>(lv_event_get_param(event));
    chameleon_ui_expand_dirty_area(area);
    // Match main.cpp's CO5300 window alignment, including signed clipping.
    area->x1=LV_MAX(0,area->x1)&~1;area->y1=LV_MAX(0,area->y1)&~1;
    area->x2=LV_MIN(465,area->x2|1);area->y2=LV_MIN(465,area->y2|1);
}
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data) {
    const int width = lv_area_get_width(area);
    const auto format = lv_display_get_color_format(display);
    const auto *buffer = lv_display_get_buf_active(display);
    const uint32_t stride = buffer->header.stride;
    ++flush_calls;
    last_flush_area=*area;
    if(width != 466 || lv_area_get_height(area) != 466) ++partial_flush_calls;
    for (int y = area->y1; y <= area->y2; ++y) {
        const auto *row = data + (y - area->y1) * stride;
        auto *target = &pixels[(y * 466 + area->x1) * 4];
        if(format == LV_COLOR_FORMAT_ARGB8888) std::memcpy(target, row, width * 4);
        else {
            assert(format == LV_COLOR_FORMAT_RGB565);
            for(int x = 0; x < width; ++x) {
                uint16_t color; std::memcpy(&color, row + x * 2, sizeof(color));
                const unsigned r = (color >> 11) & 31, g = (color >> 5) & 63, b = color & 31;
                target[x * 4] = (b << 3) | (b >> 2);
                target[x * 4 + 1] = (g << 2) | (g >> 4);
                target[x * 4 + 2] = (r << 3) | (r >> 2);
                target[x * 4 + 3] = 255;
            }
        }
    }
    lv_display_flush_ready(display);
}
static void advance(unsigned ms) {
    for (unsigned t = 0; t < ms; t += 20) {
        clock_ms += 20; update_wall_fixture();lv_tick_inc(20);chameleon_display_process_pending_input();lv_timer_handler();
    }
}
static lv_obj_t *find_text(lv_obj_t *root, const char *text, bool prefix=false) {
    if(lv_obj_has_flag(root,LV_OBJ_FLAG_HIDDEN))return nullptr;
    if (lv_obj_check_type(root, &lv_label_class)) {
        const char *actual=lv_label_get_text(root);
        if(prefix?std::strncmp(actual,text,std::strlen(text))==0:std::strcmp(actual,text)==0)return root;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        if (auto *found = find_text(lv_obj_get_child(root, i), text,prefix)) return found;
    return nullptr;
}
static bool has(const char *text,bool prefix=false){return find_text(lv_screen_active(),text,prefix)!=nullptr;}
static void click_without_advance(const char *text,bool prefix=false) {
    auto *label = find_text(lv_screen_active(), text,prefix);
    if(!label)std::fprintf(stderr,"Missing UI control: %s\n",text);
    assert(label && "expected UI label was not rendered");
    auto *button = lv_obj_get_parent(label);
    lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
}
static void click(const char *text,bool prefix=false) {
    click_without_advance(text,prefix);
    advance(100);
}
static void assert_simple_home(const char *name="卷卷") {
    assert(has("主菜单") && "the simplified home must retain its menu entrance");
    assert(!has(name) && !has("场景") && "home must omit the nickname and scene shortcut");
}
static void assert_main_menu() {
    assert(!has("小小日常")&&"the menu heading must be removed");
    for(const char *name:{"状态","喂食","玩耍","卫生","背包","陪伴","设置","图鉴","关于"})assert(has(name));
    assert(has("场景")&&has("首页"));
    assert(!has("相机")&&!has("相册")&&!has("拍摄")&&"deleted gallery controls must not remain in the menu");
}
static void open_scenes_from_menu() {
    assert_simple_home();click("主菜单");
    assert_main_menu();
    click("场景");assert(has("自由换场景"));
}
static const void *asset_source(lv_obj_t *object){return ch_mood::source_for(lv_image_get_src(object));}
static lv_obj_t *find_source(lv_obj_t *root,const void *source) {
    if(lv_obj_check_type(root,&lv_image_class)&&asset_source(root)==source)return root;
    for(uint32_t i=0;i<lv_obj_get_child_count(root);++i)if(auto *found=find_source(lv_obj_get_child(root,i),source))return found;
    return nullptr;
}
static int source_stage(const void *source) {
    for(int stage=0;stage<CH_STAGE_COUNT;++stage)
        for(int pose=0;pose<CH_ANIM_COUNT;++pose)
            for(int frame=0;frame<ch_stages[stage].animations[pose].count;++frame)
                if(source==ch_stages[stage].animations[pose].frames[frame])return stage;
    return -1;
}
static lv_obj_t *frame_object(ch_anim_t pose){for(int stage=0;stage<CH_STAGE_COUNT;++stage){const auto &a=ch_stages[stage].animations[pose];for(int i=0;i<a.count;++i)if(auto *found=find_source(lv_screen_active(),a.frames[i]))return found;}return nullptr;}
static void collect_pet_objects(lv_obj_t *root,std::vector<lv_obj_t *> &objects) {
    if(lv_obj_check_type(root,&lv_image_class)) {
        const void *source = asset_source(root);
        if(source_stage(source)>=0) objects.push_back(root);
    }
    for(uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        collect_pet_objects(lv_obj_get_child(root,i),objects);
}
static lv_obj_t *assert_pet_layout(const char *context,bool widget=false) {
    lv_refr_now(nullptr);
    std::vector<lv_obj_t *> objects; collect_pet_objects(lv_screen_active(),objects);
    assert(objects.size() == 1 && "a screen must contain exactly one pet image");
    auto *pet = objects.front();
    const int x = lv_obj_get_x(pet), y = lv_obj_get_y(pet);
    const int scale_x = lv_image_get_scale_x(pet), scale_y = lv_image_get_scale_y(pet);
    const int stage=source_stage(asset_source(pet));assert(stage>=0);
    const int size=widget?ch_stages[stage].widget_size:ch_stages[stage].pet_size;
    const int expected_x=(466-size+2)/4*2,expected_scale=size*2;
    if(!lv_obj_is_visible(pet) || x != expected_x || y <= 0 || scale_x != expected_scale || scale_y != expected_scale)
        std::fprintf(stderr,"Pet layout before first refresh (%s): x=%d y=%d scale=%d/%d visible=%d\n",
                     context,x,y,scale_x,scale_y,lv_obj_is_visible(pet));
    assert(lv_obj_is_visible(pet) && x == expected_x && y > 0 && scale_x == expected_scale && scale_y == expected_scale &&
           "the pet must be positioned and scaled before any refresh, never at native size in the corner");
    return pet;
}
static lv_obj_t *control(const char *text) {
    auto *label=find_text(lv_screen_active(),text);assert(label);
    auto *button=lv_obj_get_parent(label);assert(lv_obj_check_type(button,&lv_button_class));return button;
}
static lv_point_t pointer_point{};
static bool pointer_down=false;
static unsigned pointer_reads=0;
static void pointer_read(lv_indev_t *,lv_indev_data_t *data) {
    data->point=pointer_point;data->state=pointer_down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
    data->continue_reading=false;++pointer_reads;
}
static void advance_pointer(unsigned ms) {
    for(unsigned t=0;t<ms;t+=10){clock_ms+=10;update_wall_fixture();lv_tick_inc(10);lv_timer_handler();}
}
static void pointer_tap(int x,int y) {
    assert((x-233)*(x-233)+(y-233)*(y-233)<233*233&&"tested touch points must remain on the physical round screen");
    pointer_point={x,y};const auto reads=pointer_reads;
    pointer_down=true;advance_pointer(40);pointer_down=false;advance_pointer(80);
    assert(pointer_reads>=reads+10&&"the actual LVGL read timer must service pointer presses and releases");
}
static void pointer_button(const char *text,int outside=0,bool inner_arc=false) {
    lv_refr_now(nullptr);lv_area_t area;lv_obj_get_coords(control(text),&area);
    const int x=outside?(inner_arc&&area.x1<100?area.x2+outside:area.x1-outside):(area.x1+area.x2)/2;
    const int y=(area.y1+area.y2)/2;
    if(outside)assert(x<area.x1||x>area.x2);
    const auto sounds=ch_hw::sound_requests.size();pointer_tap(x,y);
    assert(ch_hw::sound_requests.size()==sounds+1&&ch_hw::sound_requests.back()==ch_hw::Sound::Tap&&"an expanded hit area must invoke exactly one real button action and its feedback");
}
// Capture hardware samples while the LVGL worker is occupied by a display
// update. EVENT mode consumes one sample per explicit read, regardless of
// continue_reading, so the actual replay loop must service every queued edge.
static ch_touch::Buffer queued_samples;
static lv_indev_t *queued_input=nullptr;
static lv_point_t queued_point{};
static lv_indev_state_t queued_state=LV_INDEV_STATE_RELEASED;
static unsigned queued_reads=0;
static void queued_pointer_read(lv_indev_t *input,lv_indev_data_t *data) {
    ch_touch::Sample sample{};
    if(queued_samples.pop(sample)) {
        queued_point={sample.x,sample.y};
        queued_state=sample.kind==ch_touch::Kind::Down||sample.kind==ch_touch::Kind::Move?
            LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
        data->timestamp=sample.timestamp;
        if(sample.kind==ch_touch::Kind::Cancel)lv_indev_wait_release(input);
        ++queued_reads;
    }
    data->point=queued_point;data->state=queued_state;
    data->continue_reading=queued_samples.pending();
}
static void queued_elapsed(unsigned ms) {clock_ms+=ms;lv_tick_inc(ms);}
static void queued_drain() {
    while(queued_samples.pending())lv_indev_read(queued_input);
    advance(80);
}
static lv_point_t queued_button_center(const char *text) {
    lv_refr_now(nullptr);lv_area_t area;lv_obj_get_coords(control(text),&area);
    return {(area.x1+area.x2)/2,(area.y1+area.y2)/2};
}
static void queued_capture_tap(lv_point_t point,unsigned hold_ms,bool move=false) {
    assert(hold_ms>=10);
    assert((point.x-233)*(point.x-233)+(point.y-233)*(point.y-233)<233*233);
    queued_samples.record(point.x,point.y,true,lv_tick_get());
    if(move) {
        const unsigned part=hold_ms/3;
        queued_elapsed(part);queued_samples.record(point.x+3,point.y-3,true,lv_tick_get());
        queued_elapsed(part);queued_samples.record(point.x-3,point.y+3,true,lv_tick_get());
        queued_elapsed(hold_ms-2*part);
    } else queued_elapsed(hold_ms);
    queued_samples.record(point.x,point.y,false,lv_tick_get());
}
static void queued_button(const char *text,unsigned hold_ms,bool move=false) {
    const auto sounds=ch_hw::sound_requests.size();const auto reads=queued_reads;
    queued_capture_tap(queued_button_center(text),hold_ms,move);
    assert(queued_reads==reads&&"capturing a short tap must not run LVGL on the hardware producer");
    queued_elapsed(150); // No LVGL reads during this simulated display stall.
    queued_drain();
    assert(ch_hw::sound_requests.size()==sounds+1&&ch_hw::sound_requests.back()==ch_hw::Sound::Tap&&
           "a complete captured light tap invokes exactly one real button action and feedback");
}
static void collect_sized(lv_obj_t *root,int width,int height,bool clickable,std::vector<lv_obj_t *> &found) {
    if(lv_obj_is_visible(root)&&lv_obj_get_width(root)==width&&lv_obj_get_height(root)==height
       &&(!clickable||lv_obj_has_flag(root,LV_OBJ_FLAG_CLICKABLE)))found.push_back(root);
    for(uint32_t i=0;i<lv_obj_get_child_count(root);++i)collect_sized(lv_obj_get_child(root,i),width,height,clickable,found);
}
static lv_obj_t *unique_sized(int width,int height,bool clickable=true) {
    lv_refr_now(nullptr);std::vector<lv_obj_t *> found;collect_sized(lv_screen_active(),width,height,clickable,found);
    assert(found.size()==1&&"a game target must be a real uniquely identifiable visible object");return found.front();
}
static lv_point_t object_center(lv_obj_t *object) {
    lv_refr_now(nullptr);lv_area_t area;lv_obj_get_coords(object,&area);return {(area.x1+area.x2)/2,(area.y1+area.y2)/2};
}
static void pointer_object(lv_obj_t *object) {const auto p=object_center(object);pointer_tap(p.x,p.y);}
static lv_point_t hunt_point() {return object_center(unique_sized(60,48));}
static void hunt_hit() {const auto p=hunt_point();pointer_tap(p.x,p.y);}
static bool visible_insect_child(lv_obj_t *card) {
    for(uint32_t i=0;i<lv_obj_get_child_count(card);++i){auto *child=lv_obj_get_child(card,i);
        if(lv_obj_is_visible(child)&&lv_obj_get_width(child)==23&&lv_obj_get_height(child)==17)return true;
    }return false;
}
static std::vector<lv_obj_t *> leaf_cards() {
    lv_refr_now(nullptr);std::vector<lv_obj_t *> cards;collect_sized(lv_screen_active(),82,44,true,cards);
    assert(cards.size()==3);return cards;
}
static lv_obj_t *revealed_leaf() {
    auto cards=leaf_cards();lv_obj_t *answer=nullptr;
    for(auto *card:cards)if(visible_insect_child(card)){assert(!answer);answer=card;}
    assert(answer&&"the reveal must visibly show one real insect under its leaf");return answer;
}
static void wait_leaf_input(int stage) {
    const auto until=clock_ms+3000;
    while(lv_obj_has_state(leaf_cards().front(),LV_STATE_DISABLED)&&clock_ms<until)advance(20);
    for(auto *card:leaf_cards())assert(!lv_obj_has_state(card,LV_STATE_DISABLED)&&!visible_insect_child(card));
    (void)stage;
}
static void assert_running_game_layout(int kind);
static std::vector<int> watch_mimic_demo(int expected) {
    std::vector<int> sequence;int last=-1;const auto until=clock_ms+6500;
    while(lv_obj_has_state(control("眨眼"),LV_STATE_DISABLED)&&clock_ms<until){
        assert_running_game_layout(2);
        int pose=-1;if(frame_object(CH_BLINK))pose=0;else if(frame_object(CH_WAVE))pose=1;else if(frame_object(CH_STRETCH))pose=2;
        if(pose>=0&&pose!=last)sequence.push_back(pose);last=pose;advance(20);
    }
    assert(!lv_obj_has_state(control("眨眼"),LV_STATE_DISABLED));
    assert(static_cast<int>(sequence.size())==expected&&"memorise the actual shown pet poses rather than deriving answers from the game engine");
    return sequence;
}
static void mimic_answer(int option) {const char *names[]={"眨眼","挥爪","伸懒腰"};pointer_button(names[option]);}
static lv_obj_t *active_ball_goal() {
    lv_refr_now(nullptr);std::vector<lv_obj_t *> goals;collect_sized(lv_screen_active(),56,56,false,goals);assert(goals.size()==3);
    lv_obj_t *active=nullptr;
    for(auto *goal:goals)if(lv_color_eq(lv_obj_get_style_border_color(goal,LV_PART_MAIN),lv_color_hex(0x24ed66))){assert(!active);active=goal;}
    assert(active&&"one goal must be visibly highlighted for the drag target");return active;
}
static void drag_ball_to(lv_point_t destination) {
    const auto source=object_center(unique_sized(52,52));const auto reads=pointer_reads;
    pointer_point=source;pointer_down=true;advance_pointer(40);
    for(int step=1;step<=8;++step){pointer_point={source.x+(destination.x-source.x)*step/8,source.y+(destination.y-source.y)*step/8};advance_pointer(30);}
    pointer_down=false;advance_pointer(80);
    assert(pointer_reads>=reads+30&&"dragging must use actual LVGL press, move and release samples");
}
static void count_draw(lv_event_t *event) { ++*static_cast<unsigned *>(lv_event_get_user_data(event)); }
static void assert_inside_circle(const lv_area_t &area) {
    for(int x:{area.x1,area.x2})for(int y:{area.y1,area.y2})
        assert((x-233)*(x-233)+(y-233)*(y-233)<233*233&&"a control or text must fit the actual round display");
}
static void assert_controls_separate(const std::vector<lv_obj_t *> &controls) {
    lv_refr_now(nullptr);
    for(unsigned i=0;i<controls.size();++i)for(unsigned j=i+1;j<controls.size();++j){
        lv_area_t a,b;lv_obj_get_coords(controls[i],&a);lv_obj_get_coords(controls[j],&b);
        assert((a.x2<b.x1||b.x2<a.x1||a.y2<b.y1||b.y2<a.y1)&&"game selection, its hint and home must never cover one another");
    }
}
static lv_obj_t *text_container(lv_obj_t *text) {
    assert(text&&lv_obj_is_visible(text));auto *parent=lv_obj_get_parent(text);
    return parent==lv_screen_active()?text:parent;
}
static lv_obj_t *lobby_hint() {
    auto *hint=find_text(lv_screen_active(),"选",true);assert(hint&&"the lobby needs a visible one-line play instruction");
    assert(lv_obj_get_height(hint)<=22);auto *panel=text_container(hint);assert(lv_obj_get_height(panel)<=28);return panel;
}
static void assert_pet_uncovered(lv_obj_t *pet,const std::vector<lv_obj_t *> &controls) {
    lv_refr_now(nullptr);
    const auto *image=static_cast<const lv_image_dsc_t *>(asset_source(pet));
    assert(image->header.cf==LV_COLOR_FORMAT_RGB565A8);
    const auto *alpha=image->data+image->header.stride*image->header.h;
    const float scale=lv_image_get_scale_x(pet)/256.f;
    lv_area_t position;lv_obj_get_coords(pet,&position);
    for(auto *control:controls){
        if(!lv_obj_is_visible(control))continue;
        lv_area_t area;lv_obj_get_coords(control,&area);
        for(unsigned y=0;y<image->header.h;++y)for(unsigned x=0;x<image->header.w;++x){
            if(!alpha[y*(image->header.stride/2)+x])continue;
            const float px=position.x1+(x+.5f)*scale,py=position.y1+(y+.5f)*scale;
            if(px>=area.x1&&px<=area.x2&&py>=area.y1&&py<=area.y2){
                std::fprintf(stderr,"Pet pixel %.1f,%.1f covered by UI %d,%d..%d,%d\n",px,py,area.x1,area.y1,area.x2,area.y2);
                const int stage=source_stage(image);
                for(int pose=0;pose<CH_ANIM_COUNT;++pose)for(int frame=0;frame<ch_stages[stage].animations[pose].count;++frame)
                    if(image==ch_stages[stage].animations[pose].frames[frame])std::fprintf(stderr,"Covered stage=%d pose=%d frame=%d clock=%llu\n",stage,pose,frame,static_cast<unsigned long long>(clock_ms));
                assert(false&&"controls must clear actual opaque pet pixels, not just the image rectangle");
            }
        }
    }
}
static void assert_running_game_layout(int kind) {
    const char *names[]={"捕虫挑战","叶下寻虫","动作模仿","彩球入圈"};
    auto *title=text_container(find_text(lv_screen_active(),names[kind]));
    auto *score=text_container(find_text(lv_screen_active(),"命中",true));
    std::vector<lv_obj_t *> hint_panels;collect_sized(lv_screen_active(),260,26,false,hint_panels);collect_sized(lv_screen_active(),260,24,false,hint_panels);
    assert(hint_panels.size()==1);auto *hint=hint_panels.front();assert(lv_obj_get_child_count(hint)==1);
    auto *hint_text=lv_obj_get_child(hint,0);assert(lv_obj_is_visible(hint_text)&&lv_obj_get_height(hint_text)<=22);
    assert(std::strlen(lv_label_get_text(hint_text))>0&&"every game phase must paint a legible instruction");
    std::vector<lv_obj_t *> controls={title,score,hint,control("结束")};
    if(kind==1){auto cards=leaf_cards();controls.insert(controls.end(),cards.begin(),cards.end());}
    if(kind==2)for(const char *name:{"眨眼","挥爪","伸懒腰"})controls.push_back(control(name));
    if(kind==3){std::vector<lv_obj_t *> goals;collect_sized(lv_screen_active(),56,56,false,goals);assert(goals.size()==3);controls.insert(controls.end(),goals.begin(),goals.end());}
    for(auto *object:controls){lv_area_t area;lv_obj_get_coords(object,&area);assert_inside_circle(area);}
    assert_controls_separate(controls);assert_pet_uncovered(assert_pet_layout("readable running game"),controls);
}
static void collect_text(lv_obj_t *root,const char *text,std::vector<lv_obj_t *> &objects) {
    if(lv_obj_check_type(root,&lv_label_class)&&!std::strcmp(lv_label_get_text(root),text))objects.push_back(root);
    for(uint32_t i=0;i<lv_obj_get_child_count(root);++i)collect_text(lv_obj_get_child(root,i),text,objects);
}
static void assert_incremental_matches_full(const char *context);
static bool alarm_visible() { return find_text(lv_layer_top(),"闹钟响了")!=nullptr; }
static lv_obj_t *alarm_control(const char *text) {
    auto *label=find_text(lv_layer_top(),text);assert(label);auto *button=lv_obj_get_parent(label);
    assert(lv_obj_check_type(button,&lv_button_class));return button;
}
static void alarm_pointer(const char *text) { pointer_object(alarm_control(text)); }
static void alarm_adjust(const char *text,bool hour) {
    std::vector<lv_obj_t *> labels;collect_text(lv_screen_active(),text,labels);assert(labels.size()==2);
    auto *choice=lv_obj_get_parent(labels[0]);
    for(auto *label:labels){auto *button=lv_obj_get_parent(label);if((lv_obj_get_x(button)<233)==hour)choice=button;}
    assert((lv_obj_get_x(choice)<233)==hour);pointer_object(choice);
}
static void alarm_settings() {
    pointer_button("主菜单");pointer_button("设置");pointer_button("闹钟");
}
static void audio_settings() {pointer_button("主菜单");pointer_button("设置");pointer_button("音量调节");}
static void audio_adjust(const char *text,bool alarm) {
    lv_refr_now(nullptr);std::vector<lv_obj_t *> labels;collect_text(lv_screen_active(),text,labels);assert(labels.size()==2);
    std::sort(labels.begin(),labels.end(),[](auto *a,auto *b){return lv_obj_get_y(lv_obj_get_parent(a))<lv_obj_get_y(lv_obj_get_parent(b));});
    pointer_object(lv_obj_get_parent(labels[alarm?1:0]));
}
static void assert_audio_layout() {
    lv_refr_now(nullptr);std::vector<lv_obj_t *> controls;
    for(const char *text:{"减小","增大"}){std::vector<lv_obj_t *> labels;collect_text(lv_screen_active(),text,labels);assert(labels.size()==2);for(auto *o:labels)controls.push_back(lv_obj_get_parent(o));}
    for(const char *text:{"试听闹钟","返回设置"})controls.push_back(control(text));
    for(unsigned i=0;i<lv_obj_get_child_count(lv_screen_active());++i){auto *o=lv_obj_get_child(lv_screen_active(),i);if(lv_obj_check_type(o,&lv_label_class)&&lv_obj_is_visible(o))controls.push_back(o);}
    for(auto *o:controls){lv_area_t area;lv_obj_get_coords(o,&area);assert_inside_circle(area);}
    assert_controls_separate(controls);assert_incremental_matches_full("volume controls remain clear in partial rendering");
}
static void assert_alarm_page_layout() {
    lv_refr_now(nullptr);std::vector<lv_obj_t *> controls;
    for(const char *text:{"+","-"}){std::vector<lv_obj_t *> labels;collect_text(lv_screen_active(),text,labels);assert(labels.size()==2);for(auto *label:labels)controls.push_back(lv_obj_get_parent(label));}
    auto *toggle=find_text(lv_screen_active(),"每日闹钟",true);assert(toggle);controls.push_back(lv_obj_get_parent(toggle));
    controls.push_back(control("保存闹钟"));controls.push_back(control("返回"));
    controls.push_back(control(find_text(lv_screen_active(),"取消稍后")?"取消稍后":"网络校时"));
    auto *now=find_text(lv_screen_active(),ch_hw::status.time_valid?"现在 ":"当前时间未校准",true);assert(now);controls.push_back(now);
    for(const char *text:{"闹钟","先联网或手动校时","北京时间 · 每天一次","已保存","闹钟保存不可用"})
        if(auto *label=find_text(lv_screen_active(),text,true))controls.push_back(label);
    // Include both number labels, the colon and note by their actual roles;
    // duplicate hour/minute digits are intentionally not text-selected.
    for(uint32_t i=0;i<lv_obj_get_child_count(lv_screen_active());++i){auto *child=lv_obj_get_child(lv_screen_active(),i);
        if(!lv_obj_check_type(child,&lv_label_class)||!lv_obj_is_visible(child))continue;
        if(lv_obj_get_y(child)==178)controls.push_back(child);
    }
    for(auto *object:controls){lv_area_t area;lv_obj_get_coords(object,&area);assert_inside_circle(area);}
    assert_controls_separate(controls);
    assert(lv_obj_get_width(toggle)==240&&lv_obj_get_height(toggle)<=22);
    assert_incremental_matches_full("alarm settings use the complete round display");
}
static void assert_alarm_modal_layout() {
    assert(alarm_visible());lv_refr_now(nullptr);
    auto *title=find_text(lv_layer_top(),"闹钟响了");auto *card=lv_obj_get_parent(title);lv_area_t area;lv_obj_get_coords(card,&area);assert_inside_circle(area);
    std::vector<lv_obj_t *> content;
    for(uint32_t i=0;i<lv_obj_get_child_count(card);++i){auto *child=lv_obj_get_child(card,i);if(lv_obj_is_visible(child))content.push_back(child);}
    assert(content.size()==5);
    for(auto *child:content){lv_obj_get_coords(child,&area);assert_inside_circle(area);
        if(lv_obj_check_type(child,&lv_button_class)){auto *label=lv_obj_get_child(child,0);assert(lv_obj_get_height(label)<=22&&lv_obj_get_width(label)==220);}
    }
    assert_controls_separate(content);assert_incremental_matches_full("top-layer alarm modal draws a complete round-screen reminder");
}
static void wait_alarm(unsigned timeout=6000) {
    const auto deadline=clock_ms+timeout;while(!alarm_visible()&&clock_ms<deadline)advance(20);
    assert(alarm_visible()&&"the official UI must ring on a real UTC wall-clock boundary");
}
static lv_obj_t *find_any_prefix(lv_obj_t *root,const char *text) {
    if(lv_obj_check_type(root,&lv_label_class)&&!std::strncmp(lv_label_get_text(root),text,std::strlen(text)))return root;
    for(uint32_t i=0;i<lv_obj_get_child_count(root);++i)if(auto *found=find_any_prefix(lv_obj_get_child(root,i),text))return found;
    return nullptr;
}
static void assert_home_arc() {
    auto *pet=assert_pet_layout("round home");
    std::vector<lv_obj_t *> controls;
    for(const char *name:{"喂食",has("叫醒")?"叫醒":"睡觉","玩耍","卫生"}){
        auto *button=control(name);controls.push_back(button);lv_area_t area;lv_obj_get_coords(button,&area);
        assert_inside_circle(area);assert(lv_obj_get_width(button)==60&&lv_obj_get_height(button)==44);
    }
    assert(lv_obj_get_x(controls[0])<100&&lv_obj_get_x(controls[1])<100&&lv_obj_get_x(controls[2])>366&&lv_obj_get_x(controls[3])>366);
    auto *menu=control("主菜单");assert(lv_obj_get_x(menu)+lv_obj_get_width(menu)/2==233&&lv_obj_get_y(menu)==40);
    assert_pet_uncovered(pet,controls);
    std::vector<lv_obj_t *> stats;collect_text(lv_screen_active(),"100",stats);assert(stats.size()==4);
    for(auto *stat:stats){
        auto *card=lv_obj_get_parent(stat);assert(lv_obj_get_y(card)==388&&lv_obj_get_width(card)==244&&lv_obj_get_height(card)==52);
        lv_area_t area;lv_obj_get_coords(stat,&area);lv_point_t text_size{};
        lv_text_get_size(&text_size,"100",lv_obj_get_style_text_font(stat,LV_PART_MAIN),0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
        const int center=(area.x1+area.x2)/2;area.x1=center-text_size.x/2;area.x2=area.x1+text_size.x-1;
        assert_inside_circle(area);
    }
}
static void assert_catalog_clears_side_controls(int stage) {
    lv_area_t controls[4];int i=0;for(const char *name:{"喂食","睡觉","玩耍","卫生"})lv_obj_get_coords(control(name),&controls[i++]);
    const auto &resources=ch_stages[stage];const float scale=resources.pet_size/128.f;
    for(int pose=0;pose<CH_ANIM_COUNT;++pose){
        const auto &animation=resources.animations[pose];
        for(int frame=0;frame<animation.count;++frame){
            const auto *image=animation.frames[frame];const auto *alpha=image->data+image->header.stride*image->header.h;
            for(int base:{280,296,315})for(int drift:{-24,0,24}){
                if(pose==CH_SLEEP&&drift!=0)continue;
                const int left=static_cast<int>(std::round((233-resources.pet_size*.5+drift)/2)*2);
                const int top=static_cast<int>(std::round(base-resources.pet_size*animation.foot_y[pose==CH_SLEEP?0:frame]));
                for(unsigned y=0;y<128;++y)for(unsigned x=0;x<128;++x){
                    if(!alpha[y*(image->header.stride/2)+x])continue;
                    const float px=left+(x+.5f)*scale,py=top+(y+.5f)*scale;
                    for(const auto &area:controls)if(px>=area.x1&&px<=area.x2&&py>=area.y1&&py<=area.y2){
                        std::fprintf(stderr,"Catalog overlap: stage%d pose%d frame%d base%d drift%d body %.1f,%.1f\n",stage,pose,frame,base,drift,px,py);
                        assert(false&&"all real pose frames must clear side controls even at autonomous walk extremes");
                    }
                }
            }
        }
    }
}
static void collect_droppings(lv_obj_t *root,std::vector<lv_obj_t *> &objects) {
    if(lv_obj_check_type(root,&lv_image_class)&&asset_source(root)==ch_poop&&lv_obj_is_visible(root))objects.push_back(root);
    for(uint32_t i=0;i<lv_obj_get_child_count(root);++i)collect_droppings(lv_obj_get_child(root,i),objects);
}
static void assert_droppings(unsigned count) {
    lv_refr_now(nullptr);
    std::vector<lv_obj_t *> objects;collect_droppings(lv_screen_active(),objects);assert(objects.size()==count);
    int scene=-1;for(int index=0;index<CH_BACKGROUND_COUNT;++index)if(find_source(lv_screen_active(),ch_backgrounds[index]))scene=index;
    assert(scene>=0);
    const bool branch=scene==static_cast<int>(ch::Scene::Forest)||scene==static_cast<int>(ch::Scene::Night);
    const int branch_x[]={84,330,358},ground_x[]={104,289,323};
    const int day_feet[]={273,273,277},night_feet[]={284,288,291};
    const auto *alpha=ch_poop->data+ch_poop->header.stride*ch_poop->header.h;
    int foot=-1;for(int y=0;y<32;++y)for(int x=0;x<32;++x)if(alpha[y*32+x])foot=y+1;
    assert(foot==28&&"the branch position uses the real opaque foot, not the bottom of the image canvas");
    for(size_t index=0;index<objects.size();++index){
        auto *image=objects[index];assert(ch_poop->header.w==32&&ch_poop->header.h==32&&lv_image_get_scale_x(image)==256&&lv_image_get_scale_y(image)==256);
        const int expected_x=branch?branch_x[index]:ground_x[index];
        const int expected_foot=scene==static_cast<int>(ch::Scene::Night)?night_feet[index]:day_feet[index];
        assert(lv_obj_get_x(image)==expected_x&&lv_obj_get_y(image)==(branch?expected_foot-foot:316));
        assert(lv_obj_get_y(image)+ch_poop->header.h*lv_image_get_scale_y(image)/256<350&&"physical droppings must clear the care prompt");
        lv_area_t position;lv_obj_get_coords(image,&position);
        assert_inside_circle({position.x1+3,position.y1+7,position.x1+27,position.y1+27});
        if(branch){
            // Read the native RGB565 background beneath the opaque foot. Its
            // contact pixels must be the branch's warm wood / green moss,
            // rather than the blue forest visible below a floating pile.
            const auto *background=ch_backgrounds[scene];int supported=0,contacts=0;
            for(int x=0;x<32;++x)if(alpha[(foot-1)*32+x]){
                ++contacts;uint16_t color=0;const int px=position.x1+x,py=position.y1+foot;
                std::memcpy(&color,background->data+py*background->header.stride+px*2,sizeof(color));
                const int red=((color>>11)&31)*255/31,green=((color>>5)&63)*255/63,blue=(color&31)*255/31;
                if(red>=blue&&green>=blue)++supported;
            }
            assert(contacts>=12&&supported*4>=contacts*3&&"droppings must touch the real day/night branch contour");
        }
    }
}
static void assert_droppings_clear_subject_and_home_controls(){
    auto *pet=assert_pet_layout("perched droppings leave the pet visible");lv_refr_now(nullptr);
    std::vector<lv_obj_t *> objects;collect_droppings(lv_screen_active(),objects);
    const auto *image=static_cast<const lv_image_dsc_t *>(asset_source(pet));
    const auto *body=image->data+image->header.stride*image->header.h;
    const auto *poop=ch_poop->data+ch_poop->header.stride*ch_poop->header.h;
    const float scale=lv_image_get_scale_x(pet)/256.f;lv_area_t pet_area;lv_obj_get_coords(pet,&pet_area);
    std::vector<lv_area_t> controls;
    for(const char *name:{"喂食","睡觉","玩耍","卫生"}){lv_area_t area;lv_obj_get_coords(control(name),&area);controls.push_back(area);}
    for(auto *dropping:objects){
        lv_area_t area;lv_obj_get_coords(dropping,&area);
        for(int y=0;y<32;++y)for(int x=0;x<32;++x)if(poop[y*32+x]){
            const float px=area.x1+x+.5f,py=area.y1+y+.5f;
            for(const auto &button:controls)assert(!(px>=button.x1&&px<=button.x2&&py>=button.y1&&py<=button.y2));
            const int sx=static_cast<int>(std::floor((px-pet_area.x1)/scale)),sy=static_cast<int>(std::floor((py-pet_area.y1)/scale));
            if(sx>=0&&sx<128&&sy>=0&&sy<128)assert(!body[sy*(image->header.stride/2)+sx]&&"a pile must not cover actual opaque pet pixels");
        }
    }
}
static void assert_incremental_matches_full(const char *context) {
    lv_refr_now(nullptr);
    const auto incremental = pixels;
    const auto incremental_area=last_flush_area;
    // A complete repaint is the reference for dirty-region updates at the
    // exact same clock and model state; no timer is advanced here.
    lv_obj_invalidate(lv_screen_active()); lv_refr_now(nullptr);
    if(incremental != pixels) {
        size_t first = 0; while(first < pixels.size() && incremental[first] == pixels[first]) ++first;
        std::fprintf(stderr,"Dirty-region residue (%s): clock=%llu first different pixel at %zu,%zu channel=%zu values=%u/%u last flush=%d,%d..%d,%d\n",
                     context,static_cast<unsigned long long>(clock_ms),(first / 4) % 466,(first / 4) / 466,
                     first%4,incremental[first],pixels[first],incremental_area.x1,incremental_area.y1,incremental_area.x2,incremental_area.y2);
        const auto dump=[&](const std::vector<uint8_t> &frame,const char *name){
            FILE *file=std::fopen((diagnostic_output/name).string().c_str(),"wb");assert(file);
            std::fprintf(file,"P6\n466 466\n255\n");
            for(size_t i=0;i<frame.size();i+=4){const uint8_t rgb[]={frame[i+2],frame[i+1],frame[i]};std::fwrite(rgb,1,3,file);}
            std::fclose(file);
        };
        dump(incremental,"pet-incremental-failure.ppm");dump(pixels,"pet-full-failure.ppm");
    }
    assert(incremental == pixels && "incremental animation redraw must equal a complete repaint");
}
static bool is_pet_heart(lv_obj_t *object) {
    if(lv_obj_get_width(object)!=18||lv_obj_get_height(object)!=16)return false;
    int pink=0;
    for(uint32_t i=0;i<lv_obj_get_child_count(object);++i){
        const auto color=lv_obj_get_style_bg_color(lv_obj_get_child(object,i),LV_PART_MAIN);
        if(lv_color_eq(color,lv_color_hex(0xf48db2))||lv_color_eq(color,lv_color_hex(0xc95683))||lv_color_eq(color,lv_color_hex(0xffcadb)))++pink;
    }
    return pink>0;
}
static void collect_pet_hearts(lv_obj_t *root,std::vector<lv_obj_t *> &hearts) {
    if(is_pet_heart(root))hearts.push_back(root);
    for(uint32_t i=0;i<lv_obj_get_child_count(root);++i)collect_pet_hearts(lv_obj_get_child(root,i),hearts);
}
static lv_obj_t *assert_heart_hidden() {
    lv_refr_now(nullptr);std::vector<lv_obj_t *> hearts;collect_pet_hearts(lv_screen_active(),hearts);
    assert(hearts.size()==1&&"each page world owns exactly one reusable heart, including hidden pages");
    assert(!lv_obj_is_visible(hearts.front())&&"non-petting and growth screens must not retain a visible heart");
    return hearts.front();
}
static lv_obj_t *assert_pet_heart(int stage) {
    auto *pet=assert_pet_layout("pink heart follows real petting form");
    assert(frame_object(CH_PET)==pet&&source_stage(asset_source(pet))==stage);
    std::vector<lv_obj_t *> hearts;collect_pet_hearts(lv_screen_active(),hearts);
    assert(hearts.size()==1&&lv_obj_is_visible(hearts.front())&&"petting needs exactly one visible pink heart in every actual frame");
    auto *heart=hearts.front();assert(lv_obj_get_parent(heart)==lv_obj_get_parent(pet));
    assert(lv_obj_get_style_bg_opa(heart,LV_PART_MAIN)==0&&!lv_obj_has_flag(heart,LV_OBJ_FLAG_CLICKABLE));
    lv_area_t heart_area,pet_area;lv_obj_get_coords(heart,&heart_area);lv_obj_get_coords(pet,&pet_area);
    assert_inside_circle(heart_area);assert(heart_area.y1>=114&&"the heart must stay below the time and menu strip");
    const auto *image=static_cast<const lv_image_dsc_t *>(asset_source(pet));
    const auto *alpha=image->data+image->header.stride*image->header.h;
    const float scale=lv_image_get_scale_x(pet)/256.f;
    int forehead=466,opaque_columns=0;
    // Independently measure the actual opaque head pixels under the heart's
    // horizontal span. Empty canvas margins do not count as the forehead.
    for(unsigned x=image->header.w/2;x<image->header.w;++x){
        const float left=pet_area.x1+x*scale,right=left+scale;
        if(right<=heart_area.x1||left>heart_area.x2)continue;
        for(unsigned y=0;y<image->header.h;++y)if(alpha[y*(image->header.stride/2)+x]){
            forehead=std::min(forehead,pet_area.y1+static_cast<int>(std::lround(y*scale)));++opaque_columns;break;
        }
    }
    assert(opaque_columns>0&&forehead-heart_area.y2-1>=8&&"pink heart needs eight clear pixels above the real forehead");
    assert_pet_uncovered(pet,{heart});
    return heart;
}
static uint16_t rgb565(uint32_t rgb) {
    return static_cast<uint16_t>((((rgb>>16)&255)>>3)<<11|(((rgb>>8)&255)>>2)<<5|(rgb&255)>>3);
}
static void assert_snapshot_has_heart(int stage) {
    auto *heart=assert_pet_heart(stage);auto *world=lv_obj_get_parent(heart);
    // Host-only snapshots verify the painted heart without a product camera.
    auto *snapshot=lv_snapshot_take(world,LV_COLOR_FORMAT_RGB565);assert(snapshot);
    assert(snapshot->header.w==466&&snapshot->header.h==466&&snapshot->header.stride>=932);
    lv_area_t area;lv_obj_get_coords(heart,&area);int pink_pixels=0;
    for(int y=area.y1;y<=area.y2;++y)for(int x=area.x1;x<=area.x2;++x){
        uint16_t color;std::memcpy(&color,snapshot->data+y*snapshot->header.stride+x*2,2);
        if(color==rgb565(0xf48db2)||color==rgb565(0xc95683)||color==rgb565(0xffcadb))++pink_pixels;
    }
    lv_draw_buf_destroy(snapshot);
    assert(pink_pixels>=100&&"a host snapshot must contain actual pink heart pixels, not only an invisible object");
}
static void check_pet_sequence(int stage) {
    bool seen[6]{};
    const auto &animation=ch_stages[stage].animations[CH_PET];
    assert(animation.count == 6);
    for(int step = 0; step < 90; ++step) {
        advance(20);
        auto *pet = assert_pet_layout("continuous pet frames");
        const void *source = asset_source(pet);
        assert(source_stage(source)==stage&&"every frame must remain in the actual grown form");
        if(frame_object(CH_PET)==pet){auto *subtitle=find_any_prefix(lv_screen_active(),"未校时 ·");assert(subtitle&&lv_obj_has_flag(subtitle,LV_OBJ_FLAG_HIDDEN)==(stage!=CH_STAGE_BABY));}
        for(int i = 0; i < 6; ++i) if(source == animation.frames[i]) seen[i] = true;
        assert_incremental_matches_full("continuous pet frames");
    }
    for(bool frame : seen) assert(frame && "all six actual pet frames must be displayed");
}
static void save_ppm(const std::filesystem::path &dir, const char *name) {
    lv_refr_now(nullptr);
    FILE *file = std::fopen((dir / name).string().c_str(), "wb"); assert(file);
    std::fprintf(file, "P6\n466 466\n255\n");
    for (size_t i = 0; i < pixels.size(); i += 4) {
        const uint8_t rgb[] = {pixels[i + 2], pixels[i + 1], pixels[i]}; std::fwrite(rgb, 1, 3, file);
    }
    std::fclose(file);
}
static void care_page(){click("主菜单");click("图鉴");click("今日照顾与徽章");}
static void pwr(){++ch_hw::status.pwr_presses;if(ch_hw::ui_wake_callback)ch_hw::ui_wake_callback();advance(120);}
static void boot(){++ch_hw::status.boot_presses;if(ch_hw::ui_wake_callback)ch_hw::ui_wake_callback();advance(120);}
static void sound_click(const char *text,bool prefix=false,bool delay=true) {
    const auto before=ch_hw::sound_requests.size();
    if(delay)click(text,prefix);else click_without_advance(text,prefix);
    assert(ch_hw::sound_requests.size()==before+1&&"each enabled button event needs exactly one feedback sound");
    assert(ch_hw::sound_requests.back()==ch_hw::Sound::Tap&&"button feedback is the short tap, without a duplicate action chime");
}
static void play_one(){click("玩耍");click("捕虫挑战");hunt_hit();click("结束");advance(1700);}
static lv_timer_t *ui_timer=nullptr;
static void start_ui() {
    // A real reboot cannot leave the previous application's timer running.
    if(ui_timer)lv_timer_delete(ui_timer);
    chameleon_ui_start(true);
    ch_hw::set_ui_wake_callback(chameleon_display_notify_input);
    ui_timer=lv_timer_get_next(nullptr); // Startup creates its 40 ms timer last.
    assert(ui_timer);
}
static void forest_bag(){pointer_button("主菜单");pointer_button("背包");}
static void forest_workshop(){forest_bag();pointer_button("森林\n工坊");}
static void forest_wishes(){forest_bag();pointer_button("小愿望");}
static void forest_pick(const char *text,unsigned index=0){
    std::vector<lv_obj_t *> matches;collect_text(lv_screen_active(),text,matches);assert(index<matches.size());
    std::sort(matches.begin(),matches.end(),[](auto *a,auto *b){lv_area_t aa,bb;lv_obj_get_coords(a,&aa);lv_obj_get_coords(b,&bb);return aa.y1<bb.y1;});
    pointer_object(lv_obj_get_parent(matches[index]));
}
static void collect_forest_controls(lv_obj_t *root,std::vector<lv_obj_t *> &controls){
    if(!lv_obj_is_visible(root))return;
    if(lv_obj_check_type(root,&lv_button_class)){controls.push_back(root);return;}
    if(lv_obj_check_type(root,&lv_label_class)&&*lv_label_get_text(root)){controls.push_back(root);return;}
    for(uint32_t i=0;i<lv_obj_get_child_count(root);++i)collect_forest_controls(lv_obj_get_child(root,i),controls);
}
static void assert_forest_layout(){
    lv_refr_now(nullptr);std::vector<lv_obj_t *> controls;collect_forest_controls(lv_screen_active(),controls);
    for(auto *object:controls){lv_area_t area;lv_obj_get_coords(object,&area);assert_inside_circle(area);}
    assert_controls_separate(controls);assert_incremental_matches_full("forest pages use clear labels and stable partial frames");
}
static void assert_world_first_frame(ch::Scene scene,int opacity,uint32_t tint,bool stable_sky) {
    // Check the actual page that LVGL paints, before the application timer
    // can repair an uninitialised scene or shade.
    lv_refr_now(nullptr);
    auto *world=lv_obj_get_child(lv_screen_active(),0);
    assert(world&&lv_obj_is_visible(world));
    auto *background=lv_obj_get_child(world,0),*shade=lv_obj_get_child(world,1);
    assert(lv_image_get_src(background)==ch_backgrounds[static_cast<int>(scene)]&&"a selected scene must appear on the very first page frame");
    if(lv_obj_get_style_bg_opa(shade,LV_PART_MAIN)!=opacity)std::fprintf(stderr,"Lighting mismatch scene=%d hour=%d weather=%d expected=%d actual=%d\n",static_cast<int>(scene),ch_hw::status.hour,ch_hw::status.weather_code,opacity,lv_obj_get_style_bg_opa(shade,LV_PART_MAIN));
    assert(lv_obj_get_style_bg_opa(shade,LV_PART_MAIN)==opacity&&"first frame must already include day/night and weather lighting");
    assert(lv_color_eq(lv_obj_get_style_bg_color(shade,LV_PART_MAIN),lv_color_hex(tint)));
    assert_incremental_matches_full("new scene page is correct in the real partial RGB565 render path");
    if(stable_sky) {
        const auto first=pixels;advance(40);lv_refr_now(nullptr);
        // This exposed upper-right scenery contains neither the sprite nor
        // page controls. Clear/cloudy skies have no moving weather particles.
        for(int y=60;y<110;++y)for(int x=350;x<420;++x)
            assert(std::memcmp(&first[(y*466+x)*4],&pixels[(y*466+x)*4],4)==0&&"page opening must not flash bright scenery before the next tick");
    }
}
static void growth_page(){click("主菜单");click("图鉴");click("成长");assert(has("成长记录")&&has("7天少年 · 14天成年"));}
static void assert_stage(int stage,const char *context) {
    auto *pet=assert_pet_layout(context);
    assert(source_stage(asset_source(pet))==stage&&"growth must switch to the complete actual frame catalog");
}
static void assert_capture_mouth(int stage) {
    // A timer may have changed style positions after LVGL's last refresh.
    // Compare the displayed mouth and line after layout at the same clock.
    lv_refr_now(nullptr);
    auto *pet=frame_object(CH_EAT);assert(pet&&source_stage(asset_source(pet))==stage);
    const auto &animation=ch_stages[stage].animations[CH_EAT];int frame=-1;
    for(int i=0;i<animation.count;++i)if(asset_source(pet)==animation.frames[i])frame=i;
    assert(frame>=0&&animation.mouth_x[frame]>=0&&animation.mouth_y[frame]>=0&&"the capture frame must have an open mouth anchor");
    lv_obj_t *line=nullptr;auto *world=lv_obj_get_parent(pet);
    for(uint32_t i=0;i<lv_obj_get_child_count(world);++i){auto *child=lv_obj_get_child(world,i);if(lv_obj_check_type(child,&lv_line_class)&&lv_obj_is_visible(child))line=child;}
    assert(line&&lv_line_get_point_count(line)==2);
    const auto *points=lv_line_get_points(line);const int size=ch_stages[stage].pet_size;
    if(std::abs(points[0].y-(lv_obj_get_y(pet)+size*animation.mouth_y[frame]))>=.51)
        std::fprintf(stderr,"Capture anchor: stage%d frame%d pet=%d,%d size=%d expected=%.3f,%.3f actual=%d,%d clock=%llu\n",stage,frame,lv_obj_get_x(pet),lv_obj_get_y(pet),size,lv_obj_get_x(pet)+size*animation.mouth_x[frame],lv_obj_get_y(pet)+size*animation.mouth_y[frame],points[0].x,points[0].y,static_cast<unsigned long long>(clock_ms));
    // LVGL stores line vertices on whole display pixels; allow the expected
    // normalized anchor to round to the nearest pixel at each stage scale.
    assert(std::abs(points[0].x-(lv_obj_get_x(pet)+size*animation.mouth_x[frame]))<.51);
    assert(std::abs(points[0].y-(lv_obj_get_y(pet)+size*animation.mouth_y[frame]))<.51);
    assert(points[1].x>points[0].x&&"the tongue must extend from the actual grown form's open mouth");
}
static lv_obj_t *capture_insect(int stage) {
    lv_refr_now(nullptr);
    auto *pet=frame_object(CH_EAT);assert(pet&&source_stage(asset_source(pet))==stage);
    auto *world=lv_obj_get_parent(pet);
    for(uint32_t i=0;i<lv_obj_get_child_count(world);++i){auto *child=lv_obj_get_child(world,i);
        if(lv_obj_get_width(child)==23&&lv_obj_get_height(child)==17&&lv_obj_is_visible(child)){
            assert(lv_obj_get_child_count(child)>0&&"a visible capture insect must contain real pixels, not an empty transparent box");
            return child;
        }
    }
    return nullptr;
}
static double capture_length(int stage) {
    assert_capture_mouth(stage);auto *world=lv_obj_get_parent(frame_object(CH_EAT));
    for(uint32_t i=0;i<lv_obj_get_child_count(world);++i){auto *line=lv_obj_get_child(world,i);if(lv_obj_check_type(line,&lv_line_class)&&lv_obj_is_visible(line)){
        const auto *p=lv_line_get_points(line);return std::hypot(p[1].x-p[0].x,p[1].y-p[0].y);
    }}assert(false);return 0;
}
static void assert_growth_moment(int previous,int target) {
    lv_refr_now(nullptr);
    assert(has(target==CH_STAGE_TEEN?"长成少年啦！":"长成成年啦！")&&has("回到森林"));
    std::vector<lv_obj_t *> forms;collect_pet_objects(lv_screen_active(),forms);int seen=0;
    for(auto *form:forms)if(lv_obj_is_visible(form)){
        assert(lv_image_get_scale_x(form)==256&&lv_image_get_scale_y(form)==256);
        const int stage=source_stage(asset_source(form));assert(stage==previous||stage==target);seen|=1<<stage;
        lv_area_t area;lv_obj_get_coords(form,&area);assert_inside_circle(area);
    }
    assert(seen==((1<<previous)|(1<<target))&&"growth must show both real forms on its dedicated screen");
    assert_incremental_matches_full("dedicated growth celebration");
}
static void bond_page() {
    click("主菜单");click("状态");click("亲密度");
    assert(has("亲密与陪伴")&&has("21天纪念",true)&&has("30天纪念",true));
}
static std::vector<std::pair<int,int>> paper_positions() {
    std::vector<std::pair<int,int>> positions;auto *world=lv_obj_get_child(lv_screen_active(),0);
    for(unsigned i=0;i<lv_obj_get_child_count(world);++i){auto *child=lv_obj_get_child(world,i);
        if((lv_obj_get_width(child)==3&&lv_obj_get_height(child)==9)||(lv_obj_get_width(child)==8&&lv_obj_get_height(child)==3))
            positions.emplace_back(lv_obj_get_x(child),lv_obj_get_y(child));
    }
    assert(positions.size()==24);return positions;
}
static void assert_anniversary_moment(int day) {
    lv_refr_now(nullptr);
    char title[48];std::snprintf(title,sizeof(title),"陪伴%d天啦！",day);
    assert(has(title)&&has("回到森林")&&"each adult milestone has its own dedicated celebration");
    auto *heading=find_text(lv_screen_active(),title);lv_area_t heading_area;
    lv_obj_get_coords(heading,&heading_area);assert_inside_circle(heading_area);
    std::vector<lv_obj_t *> forms;collect_pet_objects(lv_screen_active(),forms);
    int visible=0;lv_obj_t *adult=nullptr;
    for(auto *form:forms)if(lv_obj_is_visible(form)){
        ++visible;adult=form;
        assert(source_stage(asset_source(form))==CH_STAGE_ADULT&&"anniversary must show the real adult artwork");
        assert(lv_image_get_scale_x(form)==lv_image_get_scale_y(form)&&"celebration must not stretch a pose");
        lv_area_t area;lv_obj_get_coords(form,&area);assert_inside_circle(area);
    }
    assert(visible==1&&adult&&"anniversary must not duplicate the pet in a corner");
    const auto *source=asset_source(adult);bool approved=false;
    for(const auto pose:{CH_HAPPY,CH_IDLE})for(int i=0;i<ch_stages[CH_STAGE_ADULT].animations[pose].count;++i)
        approved|=source==ch_stages[CH_STAGE_ADULT].animations[pose].frames[i];
    assert(approved&&"anniversary reuses the approved happy/idle adult frames");
    int paper=0,visible_paper=0;const uint32_t colors[]={0xef6f8b,0xefbc47,0x78bc72,0x62bfc4,0x8e84cb,0xf08e55};
    auto *party_world=lv_obj_get_parent(adult);
    for(unsigned i=0;i<lv_obj_get_child_count(party_world);++i){
        auto *child=lv_obj_get_child(party_world,i);
        bool colored=false;for(auto color:colors)colored|=lv_color_eq(lv_obj_get_style_bg_color(child,LV_PART_MAIN),lv_color_hex(color));
        if(colored&&((lv_obj_get_width(child)==3&&lv_obj_get_height(child)==9)||(lv_obj_get_width(child)==8&&lv_obj_get_height(child)==3))){
            ++paper;lv_area_t area;lv_obj_get_coords(child,&area);const int x=(area.x1+area.x2)/2,y=(area.y1+area.y2)/2;
            if(lv_obj_is_visible(child)&&(x-233)*(x-233)+(y-233)*(y-233)<233*233)++visible_paper;
        }
    }
    assert(paper==24&&visible_paper>12&&"all 24 paper strips must participate in the loop while a majority remains in the round display");
    assert_incremental_matches_full("adult anniversary celebration");
}
static void check_stage_sleep(int stage) {
    click("睡觉");auto *pet=assert_pet_layout("grown sleep first frame");
    assert(source_stage(asset_source(pet))==stage&&frame_object(CH_SLEEP)==pet);
    const int x=lv_obj_get_x(pet),y=lv_obj_get_y(pet),scale=lv_image_get_scale_x(pet);
    for(int i=0;i<8;++i){advance(500);pet=assert_pet_layout("grown sleep breathing");assert(source_stage(asset_source(pet))==stage&&frame_object(CH_SLEEP)==pet);assert(lv_obj_get_x(pet)==x&&lv_obj_get_y(pet)==y&&lv_image_get_scale_x(pet)==scale);assert_incremental_matches_full("grown sleep breathing");}
    int marks=0;auto *world=lv_obj_get_parent(pet);
    for(uint32_t i=0;i<lv_obj_get_child_count(world);++i){auto *child=lv_obj_get_child(world,i);if(lv_obj_get_width(child)==21&&lv_obj_get_height(child)==21&&lv_obj_is_visible(child))++marks;}
    assert(marks==4&&saved().state.sleeping);
}

int main(int argc, char **argv) {
    assert(argc==3);
    const std::filesystem::path output(argv[1]);const std::string mode(argv[2]);std::filesystem::create_directories(output);
    diagnostic_output=output;
    // Existing-save regressions model subsequent boots. Only fresh-baby has
    // no marker and must discard the old care state exactly once.
    if(mode!="fresh-baby")integer_values["fresh_baby"]=1;
    Save initial{};initial.state.auto_life=false;initial.state.auto_world=false;initial.state.idle_minutes=0;
    if(mode.rfind("alarm-",0)==0) {
        initial.state.brightness=57;initial.state.mood=60;initial.state.growth_ms=123456;
        initial.state.companion_ms=456789;initial.state.affinity=23;initial.state.coins=91;
        initial.state.day_key=20261006;initial.state.inventory[0]=8;
        if(mode=="alarm-config")wall_time(alarm_epoch(0,7,59,50),false);
        else {wall_time(alarm_epoch(0,7,59,56));seed_alarm();}
        if(mode=="alarm-dark-muted"){initial.state.sound=false;ch_hw::status.time_valid=false;}
        if(mode=="alarm-invalid")blobs["alarm"][offsetof(AlarmFixture,crc)]^=1;
        blobs["network-fixture"]={1,2,3};
    }
    else if(mode.rfind("forest-",0)==0) {
        verified_day(7);initial.state.meals=11;initial.state.strokes=19;initial.state.encounter_total=4;
        initial.state.growth_ms=123456;initial.state.affinity=37;initial.state.coins=123;
        initial.state.inventory[0]=8;initial.state.mood=60;blobs["network-fixture"]={1,2,3};seed_alarm();
        ch_forest::Model fixture;fixture.initialize(4,11,19);fixture.sync_date(20261007);
        if(mode=="forest-wishes") {fixture.state.wishes[0].kind=1;fixture.state.wishes[0].target=0;fixture.state.wishes[1].kind=0;fixture.state.wishes[1].target=0;}
        if(mode=="forest-game") {fixture.state.wishes[0].kind=2;fixture.state.wishes[0].target=0;fixture.issue();}
        if(mode=="forest-autonomy")initial.state.auto_life=true;
        if(mode=="forest-workshop"||mode=="forest-scenes")for(auto &v:fixture.state.materials)v=20;
        if(mode!="forest-install")seed_forest(fixture.state);
        if(mode=="forest-invalid")blobs["forest"][60]^=1;
    }
    else if(mode=="fresh-baby") {LegacySaveV2 old;old.state.health=30;old.state.sick=true;old.state.sickness_cause=ch::SicknessCause::Dirt;old.state.coins=999;old.state.inventory[0]=99;old.state.meals=35;old.state.care_total=100;old.state.sleeping=true;old.state.sleep_reason=ch::SleepReason::Manual;old.count=2;old.serial=42;old.ids[0]=7;old.ids[1]=42;seed(old);blobs["network-fixture"]={1,2,3};}
    else if(mode=="sick") {initial.state.health=30;initial.state.fullness=60;initial.state.energy=70;initial.state.sick=true;initial.state.sickness_cause=ch::SicknessCause::Hunger;}
    else if(mode=="hungry")initial.state.fullness=8;
    else if(mode=="sleep") {initial.state.sleeping=true;initial.state.sleep_reason=ch::SleepReason::Manual;}
    else if(mode=="cleanup") {initial.state.poop_count=2;initial.state.cleanliness=45;}
    else if(mode=="poop-perch") {initial.state.poop_count=3;initial.state.growth_ms=ch::kGrowthAdultMs;initial.state.mood=60;}
    else if(mode=="daily") {initial.state.poop_count=1;initial.state.care_total=9;verified_day();}
    else if(mode.rfind("audio-",0)==0) {
        blobs["network-fixture"]={1,2,3};seed_alarm();
        if(mode=="audio-invalid") {AudioFixture record{};record.crc=crc32(&record,12)^1;const auto *p=reinterpret_cast<const uint8_t *>(&record);blobs["levels"]={p,p+16};}
    }
    else if(mode=="buttons-sound") {verified_day();initial.state.poop_count=1;initial.state.brightness=57;}
    else if(mode=="keys") {verified_day();initial.state.brightness=57;ch_hw::status.battery_valid=true;ch_hw::status.battery_percent=85;}
    else if(mode=="power-touch"||mode=="queued-touch") {verified_day();initial.state.brightness=57;initial.state.idle_minutes=1;initial.state.mood=60;ch_hw::status.battery_valid=true;ch_hw::status.battery_percent=85;}
    else if(mode=="standby") {verified_day();initial.state.brightness=57;initial.state.digestion_ms[0]=120000;}
    else if(mode=="standby-menu") {verified_day();initial.state.auto_life=true;initial.state.auto_world=true;initial.state.energy=17;}
    else if(mode=="weather-city") {verified_day();ch_hw::status.weather_valid=true;ch_hw::status.temp_c=26;std::strcpy(ch_hw::status.weather_city,"汕头");}
    else if(mode=="manualscene") {verified_day();ch_hw::status.imu_ready=true;ch_hw::status.tilt_x=70;ch_hw::status.tilt_y=-80;}
    else if(mode=="world-first-frame") {verified_day();ch_hw::status.hour=20;ch_hw::status.weather_valid=true;ch_hw::status.weather_code=3;initial.state.auto_world=true;initial.state.scene=ch::Scene::Clearing;}
    else if(mode=="care-info") {verified_day();initial.state.cleanliness=100;}
    else if(mode=="rain-shelter") {verified_day();ch_hw::status.weather_valid=true;ch_hw::status.weather_code=61;initial.state.auto_world=true;initial.state.auto_life=true;}
    else if(mode=="bond-ui") {
        initial.state.affinity=49;initial.state.companion_ms=2ull*ch::kCompanionDayMs+3ull*60*60*1000;
    }
    else if(mode=="encounter-dawn"||mode=="encounter-night"||mode=="encounter-rainbow"||mode=="encounter-defer") {
        verified_day();initial.state.auto_world=true;initial.state.auto_life=false;
        ch_hw::status.weather_valid=true;ch_hw::status.weather_code=mode=="encounter-rainbow"?61:0;
        ch_hw::status.hour=mode=="encounter-night"?20:mode=="encounter-rainbow"?12:7;
        if(mode=="encounter-defer"){initial.state.sleeping=true;initial.state.sleep_reason=ch::SleepReason::Manual;}
    }
    else if(mode=="anniversary-21"||mode=="anniversary-30"||mode=="anniversary-sleep"||mode=="anniversary-dark"||mode=="anniversary-feeding"||mode=="anniversary-game"||mode=="anniversary-backlog") {
        initial.state.growth_ms=ch::kGrowthAdultMs;
        initial.state.companion_ms=(mode=="anniversary-30"||mode=="anniversary-backlog"?30ull:21ull)*ch::kCompanionDayMs-500;
        if(mode=="anniversary-30")initial.state.anniversary_claimed=1;
    }
    else if(mode=="invalidsave") {seed(initial);blobs["state"][offsetof(Save,crc)]^=1;}
    else if(mode=="growth-teen"||mode=="growth-dark"||mode=="growth-feeding"||mode=="growth-sleep")initial.state.growth_ms=ch::kGrowthTeenMs-500;
    else if(mode=="growth-adult")initial.state.growth_ms=ch::kGrowthAdultMs-500;
    else if(mode=="growth-paused"){initial.state.growth_ms=ch::kGrowthTeenMs-500;initial.state.sick=true;initial.state.sickness_cause=ch::SicknessCause::Hunger;}
    else if(mode=="round-baby"||mode=="round-teen"||mode=="round-adult") {
        initial.state.health=initial.state.fullness=initial.state.energy=initial.state.mood=initial.state.cleanliness=100;
        initial.state.poop_count=3;
        initial.state.growth_ms=mode=="round-baby"?1000:mode=="round-teen"?ch::kGrowthTeenMs+1000:ch::kGrowthAdultMs;
    }
    else if(mode=="game-clock"){verified_day();initial.state.mood=60;}
    else if(mode=="games-baby"||mode=="games-teen"||mode=="games-adult"){verified_day();initial.state.mood=60;initial.state.growth_ms=mode=="games-baby"?1000:mode=="games-teen"?ch::kGrowthTeenMs+1000:ch::kGrowthAdultMs;}
    else if(mode=="capture-baby"||mode=="capture-teen"||mode=="capture-adult")initial.state.growth_ms=mode=="capture-baby"?1000:mode=="capture-teen"?ch::kGrowthTeenMs+1000:ch::kGrowthAdultMs;
    else if(mode=="colors-baby"||mode=="colors-teen"||mode=="colors-adult"){initial.state.mood=60;initial.state.growth_ms=mode=="colors-baby"?1000:mode=="colors-teen"?ch::kGrowthTeenMs+1000:ch::kGrowthAdultMs;}
    else if(mode=="pet-heart-baby"||mode=="pet-heart-teen"||mode=="pet-heart-adult")initial.state.growth_ms=mode=="pet-heart-baby"?1000:mode=="pet-heart-teen"?ch::kGrowthTeenMs+1000:ch::kGrowthAdultMs;
    else if(mode=="sick-stage-baby"||mode=="sick-stage-teen"||mode=="sick-stage-adult"){
        initial.state.growth_ms=mode=="sick-stage-baby"?1000:mode=="sick-stage-teen"?ch::kGrowthTeenMs+1000:ch::kGrowthAdultMs;
        initial.state.sick=true;initial.state.sickness_cause=ch::SicknessCause::Hunger;initial.state.health=30;initial.state.fullness=60;initial.state.energy=70;
    }
    else if(mode=="migration-v2") {
        LegacySaveV2 old;old.state.auto_life=false;old.state.auto_world=false;old.state.idle_minutes=0;
        old.state.health=60;old.state.fullness=74;old.state.cleanliness=48;old.state.coins=123;
        old.state.sick=true;old.state.sickness_cause=ch::SicknessCause::Dirt;old.state.treatment_ms=12345;
        old.state.poop_count=2;old.state.digestion_ms[0]=6543;old.state.neglect_ms[1]=98765;
        old.state.care_total=35;old.state.day_key=20261001;old.state.daily_mask=3;old.state.daily_claimed=true;old.state.daily_claims=4;
        old.count=2;old.serial=42;old.ids[0]=7;old.ids[1]=42;seed(old);
    }
    else if(mode=="migration-v3") {
        LegacySaveV3 old;old.state.auto_life=false;old.state.auto_world=false;old.state.idle_minutes=0;
        old.state.health=73;old.state.fullness=68;old.state.energy=58;old.state.mood=61;old.state.cleanliness=49;
        old.state.coins=234;old.state.inventory[0]=8;old.state.meals=17;old.state.strokes=41;
        old.state.sick=true;old.state.sickness_cause=ch::SicknessCause::Dirt;old.state.treatment_ms=12345;
        old.state.poop_count=2;old.state.digestion_ms[0]=6543;old.state.neglect_ms[1]=98765;
        old.state.care_total=55;old.state.day_key=20261001;old.state.daily_mask=3;old.state.daily_claimed=true;old.state.daily_claims=4;
        old.state.growth_ms=ch::kGrowthAdultMs;old.state.scene=ch::Scene::Meadow;old.state.awake_scene=ch::Scene::Meadow;old.state.scene_locked=true;
        old.count=2;old.serial=42;old.ids[0]=7;old.ids[1]=42;seed(old);
    }
    else if(mode=="migration-v4") {
        LegacySaveV4 old;auto &care=old.state.care;
        care.auto_life=false;care.auto_world=false;care.idle_minutes=0;
        care.health=73;care.fullness=68;care.energy=58;care.mood=61;care.cleanliness=49;
        care.coins=234;care.inventory[0]=8;care.meals=17;care.strokes=41;
        care.sick=true;care.sickness_cause=ch::SicknessCause::Dirt;care.treatment_ms=12345;
        care.poop_count=2;care.digestion_ms[0]=6543;care.neglect_ms[1]=98765;
        care.care_total=55;care.day_key=20261001;care.daily_mask=3;care.daily_claimed=true;care.daily_claims=4;
        care.growth_ms=ch::kGrowthAdultMs;care.scene=ch::Scene::Meadow;care.awake_scene=ch::Scene::Meadow;care.scene_locked=true;
        old.state.affinity=67;old.state.affection_cooldown_ms=30000;old.state.companion_ms=23ull*ch::kCompanionDayMs+123456;
        old.state.anniversary_claimed=1;old.state.encounter_claimed=5;old.state.encounter_total=9;
        old.state.photo_receipt=1;old.state.photo_ids[0]=42;
        old.count=2;old.serial=42;old.ids[0]=7;old.ids[1]=42;seed(old);
    }
    else if(mode=="migration") {
        LegacySave old{};old.state.health=73;old.state.fullness=62;old.state.energy=57;old.state.mood=44;old.state.cleanliness=51;
        old.state.coins=345;old.state.inventory[0]=9;old.state.inventory[4]=5;
        old.state.sleeping=true;old.state.sleep_reason=ch::SleepReason::Manual;
        old.state.scene=ch::Scene::MeadowNight;old.state.awake_scene=ch::Scene::Meadow;
        old.state.auto_life=false;old.state.auto_world=false;old.state.sound=false;old.state.brightness=57;old.state.idle_minutes=0;
        std::strcpy(old.state.name,"小卷");old.state.meals=12;old.state.strokes=34;
        old.count=2;old.serial=42;old.ids[0]=7;old.ids[1]=42;seed(old);
    } else {assert(mode=="flow" || mode=="pet" || mode=="pet-partial" || mode=="pet-te-buffer");}
    if(mode!="invalidsave"&&mode!="migration"&&mode!="migration-v2"&&mode!="migration-v3"&&mode!="migration-v4"&&mode!="fresh-baby")seed(initial);
    const auto original_blob=blobs["state"];
    lv_init(); auto *display = lv_display_create(466, 466);
    const bool growth_transition=mode=="growth-teen"||mode=="growth-adult";
    const bool round_layout=mode=="round-baby"||mode=="round-teen"||mode=="round-adult";
    const bool capture_check=mode=="capture-baby"||mode=="capture-teen"||mode=="capture-adult";
    const bool games_check=mode=="games-baby"||mode=="games-teen"||mode=="games-adult";
    const bool color_check=mode=="colors-baby"||mode=="colors-teen"||mode=="colors-adult";
    const bool heart_check=mode=="pet-heart-baby"||mode=="pet-heart-teen"||mode=="pet-heart-adult";
    const bool sick_stage_check=mode=="sick-stage-baby"||mode=="sick-stage-teen"||mode=="sick-stage-adult";
    const bool new_features=mode=="bond-ui"||mode.rfind("encounter-",0)==0||mode.rfind("anniversary-",0)==0;
    const bool te_buffer = mode == "pet-te-buffer"||growth_transition||mode=="growth-paused"||round_layout||capture_check||color_check||heart_check||sick_stage_check||mode=="growth-dark"||mode=="growth-feeding"||mode=="growth-sleep"||mode=="poop-perch"||mode=="power-touch"||mode=="queued-touch"||mode=="world-first-frame"||mode=="care-info"||mode=="rain-shelter"||new_features||games_check||mode=="game-clock"||mode.rfind("alarm-",0)==0||mode.rfind("audio-",0)==0||mode.rfind("forest-",0)==0;
    const bool partial = mode == "pet-partial" || te_buffer;
    lv_display_set_color_format(display, partial ? LV_COLOR_FORMAT_RGB565 : LV_COLOR_FORMAT_ARGB8888);
    static uint8_t draw_buffer[466 * 466 * 4];
    static uint8_t partial_buffer_a[466 * 50 * 2], partial_buffer_b[466 * 50 * 2];
    // The TE path uses one full-height RGB565 buffer, with partial dirty
    // regions; buffer capacity must never implicitly force FULL rendering.
    static uint8_t te_draw_buffer[466 * 466 * 2];
    if(te_buffer) lv_display_set_buffers(display,te_draw_buffer,nullptr,sizeof(te_draw_buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    else if(partial) lv_display_set_buffers(display,partial_buffer_a,partial_buffer_b,sizeof(partial_buffer_a),LV_DISPLAY_RENDER_MODE_PARTIAL);
    else lv_display_set_buffers(display, draw_buffer, nullptr, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_FULL);
    if(partial) assert(lv_display_get_render_mode(display) == LV_DISPLAY_RENDER_MODE_PARTIAL);
    if(te_buffer) lv_display_add_event_cb(display,align_display_area,LV_EVENT_INVALIDATE_AREA,nullptr);
    lv_display_set_flush_cb(display, flush);
    if(mode=="power-touch"||mode=="standby"||mode=="queued-touch"||capture_check||round_layout||games_check||mode=="daily"||mode=="buttons-sound"||mode=="anniversary-game"||mode=="game-clock"||mode.rfind("alarm-",0)==0||mode.rfind("audio-",0)==0||mode.rfind("forest-",0)==0){
        auto *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_display(input,display);lv_indev_set_read_cb(input,pointer_read);
        auto *read_timer=lv_indev_get_read_timer(input);assert(read_timer);lv_timer_set_period(read_timer,10);
        if(mode=="standby"||mode=="queued-touch")lv_indev_set_mode(input,LV_INDEV_MODE_EVENT);
        if(mode=="queued-touch"){queued_input=input;lv_indev_set_read_cb(input,queued_pointer_read);}
    }
    chameleon_display_bind(display);
    start_ui(); if(mode!="world-first-frame")advance(240);

    if(mode.rfind("forest-",0)==0) {
        const auto original_pet=saved();const auto original_alarm=blobs["alarm"],original_network=blobs["network-fixture"];
        if(mode=="forest-install"){
            auto record=saved_forest();assert(record.state.encounters==4&&record.state.meals==11&&record.state.strokes==19&&forest_materials(record.state)==0);
            auto updated=saved();updated.state.encounter_total=5;seed(updated);start_ui();advance(120);record=saved_forest();assert(record.state.encounters==5&&forest_materials(record.state)==1);
            start_ui();advance(120);assert(forest_materials(saved_forest().state)==1&&"a persisted encounter receipt is paid at most once");
            forest_workshop();assert_forest_layout();assert(saved().state.coins==original_pet.state.coins&&saved().state.affinity==original_pet.state.affinity);
        }else if(mode=="forest-wishes"){
            forest_wishes();assert_forest_layout();assert(has("想被你轻轻摸摸头"));forest_pick("听听愿望");
            assert(saved_forest().state.wishes[0].status==1&&has("摸摸 0/3 · 可以跳过"));assert_forest_layout();
            advance(200);save_ppm(output,"forest-wishes.ppm");forest_pick("去完成");advance(1800);pointer_tap(233,246);advance(1800);pointer_tap(233,246);advance(1800);
            assert(saved_forest().state.wishes[0].status==2&&forest_materials(saved_forest().state)==2);
            forest_wishes();forest_pick("听听愿望");forest_pick("去完成");pointer_button("蟋蟀 ×8");
            assert(saved_forest().state.wishes[1].status==1);advance(ch::kFeedDurationMs+400);
            const auto finished=saved_forest();assert(finished.state.wishes[1].status==2&&finished.state.wishes_complete==2&&forest_materials(finished.state)==4);
            start_ui();advance(200);assert(saved_forest().state.wishes_complete==2&&forest_materials(saved_forest().state)==4);
            forest_wishes();assert_forest_layout();assert(has("已完成"));
            assert(saved().state.growth_ms>=original_pet.state.growth_ms&&saved().state.meals==12&&saved().state.strokes==22);
        }else if(mode=="forest-autonomy"){
            pwr();advance(180000);assert(saved_forest().state.wishes[0].status==0&&"screen off must not create wishes");
            pwr();pointer_button("主菜单");advance(180000);assert(saved_forest().state.wishes[0].status==0);
            pointer_button("首页");advance(125000);assert(saved_forest().state.wishes[0].status==1);
            if(!has("卷卷有小愿望"))advance(12000);assert(has("卷卷有小愿望"));
            std::vector<lv_obj_t *> pets;collect_pet_objects(lv_screen_active(),pets);assert(pets.size()==1);assert_pet_uncovered(pets.front(),{control("卷卷有小愿望")});
            pointer_button("卷卷有小愿望");const auto before=saved();forest_pick("暂时不做");
            const auto after=saved();assert(after.state.health==before.state.health&&after.state.energy==before.state.energy&&after.state.mood==before.state.mood);
            pointer_button("首页");advance(610000);assert(saved_forest().state.wishes[1].status==1);
            forest_wishes();forest_pick("暂时不做",1);assert_forest_layout();advance(620000);assert(saved_forest().state.wishes[0].status==3&&saved_forest().state.wishes[1].status==3);
        }else if(mode=="forest-workshop"){
            forest_workshop();assert_forest_layout();save_ppm(output,"forest-materials.ppm");pointer_button("林间采集 0/3");
            assert(saved_forest().state.materials[2]==22&&saved_forest().state.forage_used==1&&saved_forest().state.season_seen==4);
            auto *gather=lv_obj_get_parent(find_text(lv_screen_active(),"下次采集",true));pointer_object(gather);assert(saved_forest().state.forage_used==1);
            pointer_button("制作");assert_forest_layout();save_ppm(output,"forest-recipes.ppm");
            for(int i=0;i<3;++i){forest_pick("制作",1);assert(saved_forest().state.crafted_mask==((1u<<(i+1))-1)&&saved_forest().state.equipped==i+1);assert_forest_layout();}
            const auto made=saved_forest();assert(made.state.materials[0]==15&&made.state.materials[1]==15&&made.state.materials[2]==19&&made.state.materials[3]==17);
            forest_pick("收起");assert(saved_forest().state.equipped==0);forest_pick("摆放",0);assert(saved_forest().state.equipped==1&&forest_materials(saved_forest().state)==forest_materials(made.state));
            pointer_button("首页");start_ui();advance(120);assert(saved_forest().state.crafted_mask==7&&saved_forest().state.equipped==1);
            assert(saved().state.energy==original_pet.state.energy&&saved().state.coins==original_pet.state.coins);
        }else if(mode=="forest-calendar"){
            forest_workshop();pointer_button("季节与节日");assert(has("国庆小庆典"));assert_forest_layout();save_ppm(output,"forest-calendar.ppm");
            pointer_button("领取节日礼物");assert(saved_forest().state.holiday_mask==8&&forest_materials(saved_forest().state)==6);assert_forest_layout();
            start_ui();advance(120);forest_workshop();pointer_button("季节与节日");assert(has("今年已领取"));assert(lv_obj_has_state(control("今年已领取"),LV_STATE_DISABLED));
            verified_day(6);advance(6000);assert(has("日期待校准")&&forest_materials(saved_forest().state)==6);
            ch_hw::status.year=2027;ch_hw::status.month=1;ch_hw::status.day=1;advance(120);assert(has("元旦快乐")&&has("领取节日礼物"));pointer_button("领取节日礼物");assert(forest_materials(saved_forest().state)==12);
            const int months[]={3,6,10,12},days[]={12,1,7,1};
            for(int i=0;i<4;++i){ch_hw::status.month=months[i];ch_hw::status.day=days[i];advance(62000);pointer_button("返回背包");pointer_button("森林\n工坊");
                auto *gather=lv_obj_get_parent(find_text(lv_screen_active(),"林间采集",true));pointer_object(gather);pointer_button("季节与节日");assert_forest_layout();save_ppm(output,("forest-season-"+std::to_string(i)+".ppm").c_str());}
            assert(saved_forest().state.season_seen==15);
        }else if(mode=="forest-invalid"){
            const auto broken=blobs["forest"];assert(has("森林记录不可用，原数据已保留"));forest_workshop();pointer_button("林间采集 0/3");assert(has("森林记录不可用，原数据保留")&&blobs["forest"]==broken);
            pointer_button("首页");pointer_button("睡觉");advance(6000);assert(saved().state.sleeping&&blobs["forest"]==broken);start_ui();advance(120);assert(blobs["forest"]==broken);
        }else if(mode=="forest-save-failure"){
            forest_workshop();const auto before=blobs["forest"];fail_forest_write=true;pointer_button("林间采集 0/3");assert(has("森林记录保存失败")&&blobs["forest"]==before);
            pointer_button("制作");assert(lv_obj_has_state(control("制作"),LV_STATE_DISABLED)==false); // The tab remains usable.
            fail_forest_write=false;start_ui();advance(120);forest_workshop();pointer_button("林间采集 0/3");assert(saved_forest().state.forage_used==1&&forest_materials(saved_forest().state)==2);
        }else if(mode=="forest-game"){
            forest_wishes();assert(has("玩满一局，并命中一次"));assert_forest_layout();forest_pick("去完成");hunt_hit();pointer_button("结束");assert(saved_forest().state.wishes[0].status==1&&forest_materials(saved_forest().state)==0);
            advance(1800);pointer_button("玩耍");pointer_button("捕虫挑战");hunt_hit();advance(21000);
            assert(saved_forest().state.wishes[0].status==2&&forest_materials(saved_forest().state)==2);start_ui();advance(120);assert(forest_materials(saved_forest().state)==2);
        }else if(mode=="forest-scenes"){
            const char *scenes[]={"蘑菇森林","林间草地","夜间森林","蓝天蘑菇草地","月夜蘑菇草地"};
            for(int stage=0;stage<3;++stage){
                auto pet_fixture=initial;pet_fixture.state.growth_ms=stage==0?123456:stage==1?ch::kGrowthTeenMs:ch::kGrowthAdultMs;pet_fixture.state.auto_world=true;
                seed(pet_fixture);start_ui();advance(120);
                for(int scene=0;scene<5;++scene){
                    auto state=saved_forest().state;state.crafted_mask=7;state.equipped=scene%3+1;seed_forest(state);start_ui();advance(120);
                    pointer_button("主菜单");pointer_button("场景");pointer_object(lv_obj_get_parent(find_text(lv_screen_active(),scenes[scene],true)));
                    auto *pet=assert_pet_layout("crafted decoration across all three stages and five scenes");const auto *source=asset_source(pet);assert(source_stage(source)==stage);
                    auto *world=lv_obj_get_child(lv_screen_active(),0);assert(lv_obj_get_index(lv_obj_get_child(world,20))<lv_obj_get_index(pet));
                    assert_incremental_matches_full("crafted pixel decoration on first frame");save_ppm(output,("forest-decor-"+std::to_string(stage)+"-"+std::to_string(scene)+".ppm").c_str());
                    pointer_button("主菜单");pointer_button("设置");pointer_button("昼夜天气   关");pointer_button("首页");advance(600);
                    assert_incremental_matches_full("season and holiday pixel layers across all scenes");
                }
            }
        }
        assert(blobs["alarm"]==original_alarm&&blobs["network-fixture"]==original_network&&"forest gameplay must retain alarm and Wi-Fi records");
        assert(saved().state.growth_ms>=original_pet.state.growth_ms&&saved().version==5);
    } else if(mode.rfind("audio-",0)==0) {
        const auto pet=blobs["state"],network=blobs["network-fixture"],alarm=blobs["alarm"];
        assert(ch_hw::effects_level==45&&ch_hw::alarm_level==85);audio_settings();assert_audio_layout();
        if(mode=="audio-config") {
            assert(has("45%")&&has("85%"));save_ppm(output,"audio-default.ppm");
            auto count=ch_hw::sound_requests.size();audio_adjust("增大",false);
            assert(ch_hw::sound_requests.size()==count+1&&ch_hw::played_levels.back()==50);
            assert(saved_audio().effects==50&&saved_audio().alarm==85);
            audio_adjust("减小",true);assert(saved_audio().effects==50&&saved_audio().alarm==80);
            count=ch_hw::sound_requests.size();pointer_object(control("试听闹钟"));
            assert(ch_hw::sound_requests.size()==count+1&&ch_hw::sound_requests.back()==ch_hw::Sound::Alarm&&ch_hw::played_levels.back()==80);
            const auto cancellations=alarm_sound_cancellations;pointer_object(control("试听闹钟"));assert(alarm_sound_cancellations==cancellations+1);
            for(int i=0;i<24;++i)audio_adjust("增大",true);
            assert(saved_audio().alarm==100&&ch_hw::alarm_level==100);
            for(int i=0;i<24;++i)audio_adjust("减小",true);
            assert(saved_audio().alarm==0&&ch_hw::alarm_level==0);
            count=ch_hw::sound_requests.size();pointer_object(control("试听闹钟"));assert(ch_hw::sound_requests.size()==count&&has("闹钟音量为0%，请调高"));
            audio_adjust("增大",true);assert(has("5%"));start_ui();advance(120);
            assert(ch_hw::effects_level==50&&ch_hw::alarm_level==5);audio_settings();assert(has("50%")&&has("5%"));assert_audio_layout();
            for(int i=0;i<12;++i)audio_adjust("减小",false);
            assert(ch_hw::effects_level==0&&ch_hw::alarm_level==5&&saved_audio().effects==0);
            count=ch_hw::sound_requests.size();pointer_object(control("试听闹钟"));assert(ch_hw::sound_requests.size()==count+1&&ch_hw::played_levels.back()==5);
            audio_adjust("增大",false);assert(ch_hw::effects_level==5&&ch_hw::alarm_level==5);
            pointer_button("返回设置");pointer_button("声音   开");
            const auto muted=ch_hw::sound_requests.size();pointer_object(control("音量调节"));pointer_object(control("试听闹钟"));assert(ch_hw::sound_requests.size()==muted&&has("请先开启声音"));
            assert_audio_layout();save_ppm(output,"audio-muted.ppm");
            click("返回设置");click("首页");click("主菜单");click("关于");assert(has("版本 0.4.7"));
        } else if(mode=="audio-invalid") {
            const auto invalid=blobs["levels"];assert(has("音量未保存，原数据保留"));audio_adjust("增大",false);
            assert(blobs["levels"]==invalid&&has("音量未保存，原数据保留"));
            start_ui();advance(120);assert(ch_hw::effects_level==45&&ch_hw::alarm_level==85);
        } else {
            assert(mode=="audio-save-failure");audio_adjust("增大",false);const auto valid=blobs["levels"];fail_audio_write=true;
            audio_adjust("减小",true);assert(blobs["levels"]==valid&&has("音量未保存，原数据保留"));
            fail_audio_write=false;start_ui();advance(120);assert(ch_hw::effects_level==50&&ch_hw::alarm_level==85);
        }
        Save previous{};std::memcpy(&previous,pet.data(),pet.size());const auto current=saved();
        assert(current.state.growth_ms>=previous.state.growth_ms&&current.state.companion_ms>=previous.state.companion_ms&&current.state.coins==previous.state.coins);
        assert(blobs["state"].size()==pet.size()&&blobs["network-fixture"]==network&&blobs["alarm"]==alarm);
    } else if(mode=="alarm-config") {
        assert(!blobs.count("alarm"));const auto pet_before=blobs["state"];
        alarm_settings();assert(has("当前时间未校准")&&has("每日闹钟   关"));assert_alarm_page_layout();save_ppm(output,"alarm-config-default.ppm");
        alarm_adjust("+",true);alarm_adjust("+",false);pointer_button("每日闹钟   关");
        assert(!blobs.count("alarm")&&"draft edits must not overwrite a saved alarm before Save");
        pointer_button("保存闹钟");assert(has("已保存，校准时间后提醒"));
        auto configured=saved_alarm();assert(configured.enabled==1&&configured.hour==9&&configured.minute==1&&configured.last_trigger_day==-1);
        assert(blobs["state"]==pet_before&&blobs["network-fixture"]==std::vector<uint8_t>({1,2,3}));
        assert_alarm_page_layout();save_ppm(output,"alarm-config-saved.ppm");
        start_ui();advance(120);alarm_settings();assert(has("每日闹钟   开"));assert(saved_alarm().hour==9&&saved_alarm().minute==1);
        alarm_adjust("+",false);pointer_button("返回");pointer_button("闹钟");
        assert(saved_alarm().minute==1&&"leaving an unsaved draft must preserve its last committed configuration");
        assert_alarm_page_layout();
        wall_time(alarm_epoch(0,10,15,58));advance(500);assert(has("现在 10:15"));
        advance(1600);assert(has("现在 10:16")&&!alarm_visible()&&"the alarm page must update from the live clock even without an info label");
    } else if(mode=="alarm-dark-muted") {
        // Cross the real boundary while calibration is invalid. Receiving a
        // first valid clock after it must not replay that missed reminder.
        advance(7000);assert(!alarm_visible()&&alarm_notes()==0&&saved_alarm().last_trigger_day==-1);
        ch_hw::status.time_valid=true;advance(2000);assert(!alarm_visible()&&alarm_notes()==0);
        wall_time(alarm_epoch(1,7,59,56));start_ui();advance(120);
        pointer_object(control("主菜单"));pointer_object(control("状态"));assert(has("照顾与状态"));
        pwr();assert(brightness==0&&ui_timer->period==1000);const auto status_reads=status_refresh_requests;wait_alarm();
        assert(brightness==57&&ui_timer->period==40&&adapter_max_wait==15&&!touch_screen_off&&!frequency_screen_off);
        assert(status_refresh_requests==status_reads+1&&has("照顾与状态")&&!has("短按侧键唤醒"));
        assert(find_text(lv_layer_top(),"声音已关闭 · 屏幕提醒")&&alarm_notes()==0&&ch_hw::sound_requests.empty());
        assert_alarm_modal_layout();save_ppm(output,"alarm-dark-muted.ppm");
        const auto before=saved();pointer_tap(233,425);assert(alarm_visible()&&has("照顾与状态"));
        assert(saved().state.strokes==before.state.strokes&&saved().state.coins==before.state.coins);
        alarm_pointer("停止闹钟");assert(!alarm_visible()&&has("照顾与状态")&&alarm_notes()==0);
        assert(saved_alarm().last_trigger_day>=0&&blobs["network-fixture"]==std::vector<uint8_t>({1,2,3}));
    } else if(mode=="alarm-controls") {
        wait_alarm();assert_alarm_modal_layout();assert(alarm_notes()==1);save_ppm(output,"alarm-ringing.ppm");
        const auto cancellation=alarm_sound_cancellations;alarm_pointer("停止闹钟");assert(!alarm_visible()&&alarm_sound_cancellations==cancellation+1);
        const auto notes=alarm_notes();advance(4000);assert(alarm_notes()==notes);
        // Each physical key is a stop action while ringing, never an
        // accidental display-off or home/menu navigation.
        for(int day=1;day<=2;++day){
            wall_time(alarm_epoch(day,7,59,58));advance(80);wait_alarm();assert(has("主菜单"));
            const auto stopped=alarm_sound_cancellations;
            if(day==1)pwr();else boot();
            assert(!alarm_visible()&&brightness==57&&has("主菜单")&&alarm_sound_cancellations==stopped+1);
        }
        wall_time(alarm_epoch(3,7,59,58));advance(80);wait_alarm();
        const auto lost_cancel=alarm_sound_cancellations;ch_hw::status.time_valid=false;advance(80);
        assert(!alarm_visible()&&alarm_sound_cancellations==lost_cancel+1&&"losing the trusted wall clock must cancel the live reminder sound");
        ch_hw::status.time_valid=true;advance(120);assert(!alarm_visible());
        const auto record=saved_alarm();start_ui();advance(120);assert(!alarm_visible());
        assert(saved_alarm().last_trigger_day==record.last_trigger_day&&"reboot preserves the once-per-day receipt independently from pet data");
    } else if(mode=="alarm-snooze") {
        wait_alarm();const auto snooze_epoch=ch_hw::status.epoch_seconds;const auto receipt=saved_alarm().last_trigger_day;
        alarm_pointer("延后5分钟");assert(!alarm_visible()&&saved_alarm().last_trigger_day==receipt);
        alarm_settings();assert(has("取消稍后")&&!has("网络校时")&&has("已延后5分钟",true));assert_alarm_page_layout();save_ppm(output,"alarm-snooze-settings.ppm");
        const auto notes=alarm_notes();pwr();assert(brightness==0);
        while(ch_hw::status.epoch_seconds<snooze_epoch+299)advance(1000);
        assert(!alarm_visible()&&brightness==0&&alarm_notes()==notes);
        wait_alarm(2500);assert(brightness==57&&ch_hw::status.epoch_seconds>=snooze_epoch+300&&ch_hw::status.epoch_seconds<=snooze_epoch+301);
        assert_alarm_modal_layout();save_ppm(output,"alarm-snooze-repeat.ppm");
        const auto snooze_cancel=alarm_sound_cancellations;alarm_pointer("停止闹钟");advance(500);
        if(alarm_visible()||!has("网络校时")||has("取消稍后"))std::fprintf(stderr,"Snooze stop: overlay=%d network=%d cancel-button=%d audio-cancel=%u->%u\n",alarm_visible(),has("网络校时"),has("取消稍后"),snooze_cancel,alarm_sound_cancellations);
        assert(!alarm_visible()&&has("网络校时")&&!has("取消稍后"));assert_alarm_page_layout();
        pointer_button("返回");pointer_button("首页");
        wall_time(alarm_epoch(1,7,59,58));advance(80);wait_alarm();alarm_pointer("延后5分钟");alarm_settings();
        assert(has("取消稍后")&&!has("网络校时"));pointer_button("取消稍后");
        assert(has("网络校时")&&!has("取消稍后"));pointer_button("返回");pointer_button("首页");pwr();advance(301000);assert(!alarm_visible());pwr();
        // Snooze is a runtime timer, not a durable request to unexpectedly ring
        // after a cold boot; the daily receipt is retained across the reboot.
        wall_time(alarm_epoch(2,7,59,58));advance(80);wait_alarm();alarm_pointer("延后5分钟");
        const auto last=saved_alarm().last_trigger_day;start_ui();advance(120);pwr();advance(301000);
        assert(!alarm_visible()&&saved_alarm().last_trigger_day==last);
    } else if(mode=="alarm-timeout") {
        wait_alarm();const auto began=clock_ms;const auto cancellation=alarm_sound_cancellations;
        advance(59920);assert(alarm_visible()&&clock_ms-began<60000);
        advance(120);assert(!alarm_visible()&&alarm_sound_cancellations==cancellation+1);
        const auto notes=alarm_notes();advance(4000);assert(alarm_notes()==notes&&"timeout cancels all future scheduled alarm notes");
    } else if(mode=="alarm-invalid") {
        const auto broken=blobs["alarm"];const auto original_growth=saved().state.growth_ms;
        alarm_settings();assert(has("闹钟数据不可用，原数据已保留"));pointer_button("每日闹钟   关");pointer_button("保存闹钟");
        assert(has("闹钟保存失败，请检查")&&blobs["alarm"]==broken);
        pointer_button("返回");pointer_button("首页");pointer_button("睡觉");
        assert(saved().state.sleeping&&saved().state.growth_ms>=original_growth&&saved().state.coins==91&&saved().state.inventory[0]==8);
        assert(blobs["alarm"]==broken&&blobs["network-fixture"]==std::vector<uint8_t>({1,2,3}));
        start_ui();advance(120);assert(saved().state.sleeping);advance(6000);assert(!alarm_visible()&&blobs["alarm"]==broken);
        assert(saved().version==5&&saved().state.companion_ms>=initial.state.companion_ms&&"a damaged alarm must neither poison nor restart pet save migration");
    } else if(mode=="alarm-game") {
        // Hold an actual LVGL ball drag across the alarm boundary. Its stale
        // release must neither award a hit nor activate a modal/hidden button.
        pointer_button("玩耍");pointer_button("彩球入圈");assert(has("命中 0",true));
        auto *ball=unique_sized(52,52);const auto source=object_center(ball),goal=object_center(active_ball_goal());
        pointer_point=source;pointer_down=true;advance_pointer(40);
        pointer_point=goal;advance_pointer(40);
        const auto drag_alarm_deadline=clock_ms+6000;while(!alarm_visible()&&clock_ms<drag_alarm_deadline)advance_pointer(10);
        assert_alarm_modal_layout();assert(has("结束")&&has("命中 0",true));
        pointer_down=false;advance_pointer(80);assert(alarm_visible()&&has("命中 0",true));
        alarm_pointer("停止闹钟");assert(!alarm_visible()&&has("结束")&&has("命中 0",true));
        // Start a new gesture, move away then into the goal, and settle once.
        const auto origin=object_center(unique_sized(52,52));pointer_point=origin;pointer_down=true;advance_pointer(40);
        pointer_point={233,340};advance_pointer(40);pointer_point=object_center(active_ball_goal());advance_pointer(40);
        pointer_down=false;advance_pointer(80);assert(has("命中 1",true)&&has("结束"));
        advance(1800);assert_running_game_layout(3);save_ppm(output,"alarm-game-resumed.ppm");
        pointer_button("结束");assert_simple_home();assert(saved().state.coins==93&&saved().state.inventory[0]==8&&saved().state.meals==0);
        assert(saved().state.affinity==23&&"an alarm overlay never completes or restarts a game to grant affection");
        // A reminder does not pause the pet or reset the 20-second game clock.
        // Let a second earned game finish beneath the modal, then verify its
        // once-only full-session reward survives dismissal and reboot.
        wall_time(alarm_epoch(1,7,59,56));start_ui();advance(120);const auto before_full=saved();
        pointer_button("玩耍");pointer_button("捕虫挑战");hunt_hit();assert(has("命中 1",true));
        wait_alarm();advance(22000);assert(alarm_visible()&&has("一起玩耍")&&!has("结束"));
        const auto settled=saved();assert(settled.state.coins==before_full.state.coins+2&&settled.state.affinity==before_full.state.affinity+2);
        assert(settled.state.care_total==before_full.state.care_total+1&&settled.state.inventory[0]==8&&settled.state.meals==0);
        assert(settled.state.companion_ms>before_full.state.companion_ms&&settled.state.growth_ms>before_full.state.growth_ms);
        pointer_tap(233,432);assert(alarm_visible());alarm_pointer("停止闹钟");advance(4000);
        assert(saved().state.coins==settled.state.coins&&saved().state.affinity==settled.state.affinity);
        start_ui();advance(120);assert(!alarm_visible()&&saved().state.coins==settled.state.coins&&saved().state.affinity==settled.state.affinity);
    } else if(mode=="standby-menu") {
        click("主菜单");pwr();advance(35000);
        assert(saved().state.sleeping&&saved().state.sleep_reason==ch::SleepReason::Nap&&"a dark menu must permit physiological rest");
        const auto asleep=saved();advance(31000);assert(saved().state.energy>=asleep.state.energy);
        pwr();assert(has("状态")&&"wake must preserve the menu the user left open");
        boot();assert(has("叫醒"));
    } else if(mode=="weather-city") {
        // Weather is opened from the clock page; it is not a setting-menu
        // item. Keep this regression aligned with the public navigation path.
        auto *clock=find_text(lv_screen_active(),"12:34");assert(clock&&"home clock must open the clock page");
        lv_obj_send_event(clock,LV_EVENT_CLICKED,nullptr);advance(100);click("天气");
        assert(has("汕头")&&has("26°C"));
        std::strcpy(ch_hw::status.weather_city,"北京");ch_hw::status.temp_c=31;advance(600);
        assert(has("北京")&&!has("汕头")&&has("31°C")&&"weather city and temperature must refresh from the same snapshot");
    } else if(mode=="standby") {
        assert(ui_timer->period==40&&adapter_max_wait==15&&!frequency_screen_off);
        pwr();assert(brightness==0&&ui_timer->period==1000&&adapter_max_wait==1000&&frequency_screen_off);
        assert(lv_timer_get_paused(lv_display_get_refr_timer(display))&&lv_timer_get_paused(lv_anim_get_timer()));
        const auto off=saved();const auto writes=nvs_writes;const auto paints=flush_calls;
        advance(29000);
        assert(nvs_writes==writes&&"standby digestion must not write at the old five-second interval");
        assert(flush_calls==paints&&"standby must not periodically flush a hidden screen");
        advance(1200);
        const auto thirty=saved();
        assert(nvs_writes==writes+1&&"dirty standby care persists at thirty seconds");
        assert(thirty.state.companion_ms-off.state.companion_ms>=30000&&thirty.state.companion_ms-off.state.companion_ms<=30200);
        assert(thirty.state.growth_ms-off.state.growth_ms==thirty.state.companion_ms-off.state.companion_ms);
        assert(off.state.digestion_ms[0]-thirty.state.digestion_ms[0]==thirty.state.companion_ms-off.state.companion_ms);
        const auto refreshes=status_refresh_requests;
        // Run the real worker's pre-handler hook before the one-second deadline.
        ++ch_hw::status.pwr_presses;ch_hw::ui_wake_callback();chameleon_display_process_pending_input();lv_timer_handler();
        assert(brightness==57&&ui_timer->period==40&&adapter_max_wait==15&&!frequency_screen_off);
        assert(status_refresh_requests==refreshes+1&&!has("短按侧键唤醒"));
        advance(60);assert(flush_calls>paints&&"wake resumes drawing immediately");
        assert_incremental_matches_full("standby wake restores full/partial pixels");
        click("睡觉");const auto asleep=saved();pwr();advance(31000);const auto rested=saved();
        assert(rested.state.sleeping&&rested.state.energy>=asleep.state.energy);
        assert(rested.state.companion_ms>asleep.state.companion_ms&&rested.state.growth_ms>asleep.state.growth_ms);
        boot();assert(brightness==57&&saved().state.sleeping&&"display wake never wakes the sleeping pet");
        const auto forced=nvs_writes;click("叫醒");assert(nvs_writes>forced&&!saved().state.sleeping&&"care actions still save immediately");
        // A delayed worker uses actual powered-on time, not a two-second cap.
        const auto before_delay=saved();clock_ms+=4500;lv_tick_inc(4500);lv_timer_handler();click("睡觉");
        assert(saved().state.companion_ms>=before_delay.state.companion_ms+4500);
        // IRQ-driven LVGL input stays idle without losing hold/release handling.
        auto *input=lv_indev_get_next(nullptr);assert(input);
        pwr();assert(brightness==0);const auto quiet_reads=pointer_reads;advance(1000);assert(pointer_reads==quiet_reads);
        const auto sounds=ch_hw::sound_requests.size();pointer_point={183,55};pointer_down=true;lv_indev_read(input);lv_timer_handler();
        assert(brightness==0&&has("短按侧键唤醒")&&ui_timer->period==1000&&"a dark contact must not wake or click through");
        advance_pointer(50);pointer_down=false;lv_indev_read(input);lv_timer_handler();
        assert(lv_timer_get_paused(lv_indev_get_read_timer(input))&&ch_hw::sound_requests.size()==sounds);
        boot();assert(brightness==57&&!has("短按侧键唤醒")&&ui_timer->period==40);assert_simple_home();
        std::printf("Standby: 1 Hz care/growth/digestion, 30 s saves, immediate key-only wake, ignored dark touch, idle render/input gating and elapsed-time preservation passed.\n");
    } else if(mode=="bond-ui") {
        bond_page();assert(has("亲密 49/100 · 熟悉\n陪伴 2天3小时"));
        assert_incremental_matches_full("relationship page on partial display");save_ppm(output,"bond-record.ppm");
        click("首页");click("主菜单");click("图鉴");click("抚摸");advance(5200);
        const auto stroked=saved();assert(stroked.state.affinity==50&&stroked.state.strokes==1);
        // The action saves immediately. Quiet countdown ticks need not write
        // flash, so the persisted remainder may still be the full minute.
        assert(stroked.state.affection_cooldown_ms>0&&stroked.state.affection_cooldown_ms<=ch::kAffectionCooldownMs);
        bond_page();assert(has("亲密 50/100 · 亲近",true));click("首页");
        click("主菜单");click("图鉴");click("抚摸");advance(5200);
        assert(saved().state.affinity==50&&saved().state.strokes==2&&"rapid petting must not farm intimacy");
        const auto retained=saved();start_ui();advance(240);bond_page();assert(has("亲密 50/100 · 亲近",true));
        assert(saved().state.companion_ms>=retained.state.companion_ms&&saved().state.affection_cooldown_ms<=retained.state.affection_cooldown_ms);
        assert(saved().state.anniversary_claimed==0&&"the relationship page must not unlock an immature anniversary");
    } else if(mode=="encounter-dawn"||mode=="encounter-night"||mode=="encounter-rainbow"||mode=="encounter-defer") {
        const bool night=mode=="encounter-night",rainbow=mode=="encounter-rainbow";
        const char *event=night?"夜间萤火":rainbow?"雨后彩虹":"清晨露珠";
        const int bit=night?4:rainbow?2:1,reward=night?2:rainbow?3:1;
        const auto before=saved();assert(!has(event,true));
        if(mode=="encounter-defer") {
            advance(32000);assert(!has(event,true)&&saved().state.encounter_claimed==0&&saved().state.sleeping);
            click("叫醒");advance(1900);click("主菜单");advance(32000);
            assert(!has(event,true)&&saved().state.encounter_claimed==0&&"encounters must wait while browsing menus");
            click("首页");advance(32000);
        } else if(rainbow) {
            advance(32000);assert(!has(event,true)&&"rain alone must not create a rainbow");
            ch_hw::status.weather_code=0;advance(32000);
        } else advance(32000);
        assert(has(event,true)&&"a suitable quiet forest must offer its contextual encounter");
        auto *event_label=find_text(lv_screen_active(),event,true);assert(event_label);
        auto *event_button=lv_obj_get_parent(event_label);lv_area_t event_area;lv_obj_get_coords(event_button,&event_area);assert_inside_circle(event_area);
        auto *pet=frame_object(CH_IDLE);if(!pet)pet=frame_object(CH_BLINK);assert(pet);
        assert_pet_uncovered(pet,{event_button});assert_incremental_matches_full("contextual forest event");
        save_ppm(output,(mode+"-offer.ppm").c_str());const auto sound_before=ch_hw::sound_requests.size();
        click(event,true);assert(ch_hw::sound_requests.size()==sound_before+1&&"an encounter button needs exactly one tap sound");
        const auto claimed=saved();assert((claimed.state.encounter_claimed&bit)&&claimed.state.encounter_total==before.state.encounter_total+1);
        assert(claimed.state.coins==before.state.coins+reward&&claimed.state.affinity==before.state.affinity+1);
        assert(frame_object(CH_HAPPY)&&"a discovered event must produce the actual happy animation");
        assert_incremental_matches_full("forest event response");save_ppm(output,(mode+"-response.ppm").c_str());
        advance(64000);assert(!has(event,true)&&saved().state.encounter_total==claimed.state.encounter_total&&"each kind is rewarded only once a day");
        start_ui();advance(32000);assert(!has(event,true)&&saved().state.encounter_total==claimed.state.encounter_total);
        // Rain-to-sun is observed per session: a reboot cannot invent a new
        // rainbow window, while dawn/night offers return on a verified new day.
        verified_day(2);ch_hw::status.hour=night?20:rainbow?12:7;advance(32000);
        assert(saved().state.encounter_claimed==0);
        if(!rainbow)assert(has(event,true));else assert(!has(event,true));
    } else if(mode.rfind("anniversary-",0)==0) {
        const bool thirty=mode=="anniversary-30";const int day=thirty?30:21;
        if(mode=="anniversary-sleep") {
            click("睡觉");advance(1000);assert(saved().state.sleeping&&!has("陪伴21天啦！"));
            click("叫醒");advance(2000);
        } else if(mode=="anniversary-dark") {
            pwr();assert(brightness==0);advance(1000);assert(!has("陪伴21天啦！"));
            pwr();advance(300);assert(brightness>0);
        } else if(mode=="anniversary-feeding") {
            click("喂食");click("蟋蟀 ×3");advance(400);assert_capture_mouth(CH_STAGE_ADULT);
            assert(!has("陪伴21天啦！")&&"a milestone must not interrupt a tongue capture");
            advance(1300);assert(saved().state.meals==1&&saved().state.inventory[0]==2);
        } else if(mode=="anniversary-game") {
            click("玩耍");click("捕虫挑战");advance(1000);assert(has("结束")&&!has("陪伴21天啦！"));
            hunt_hit();advance(550);assert_capture_mouth(CH_STAGE_ADULT);
            click_without_advance("结束");assert(!has("陪伴21天啦！"));advance(1000);
        } else if(mode!="anniversary-backlog")advance(300);
        assert_anniversary_moment(day);const auto earned=saved();const int mask=thirty?3:1;
        assert(earned.state.anniversary_claimed==mask&&earned.state.companion_ms>=static_cast<uint64_t>(day)*ch::kCompanionDayMs);
        assert(earned.state.coins>=initial.state.coins+(thirty?15:10)&&"adult anniversary must grant its one-time reward");
        const auto first_party=pixels;const auto first_strips=paper_positions();advance(240);assert_anniversary_moment(day);
        assert(first_party!=pixels&&"paper and the approved frames must animate on the celebration");
        const auto moved_strips=paper_positions();for(unsigned i=0;i<24;++i)assert(first_strips[i]!=moved_strips[i]&&"every paper strip must participate in the falling loop");
        save_ppm(output,(mode+"-party.ppm").c_str());
        if(mode=="anniversary-21") {
            advance(12500);assert_simple_home();assert(!has("陪伴21天啦！"));
            assert(saved().state.anniversary_claimed==1&&saved().state.coins==earned.state.coins&&saved().state.affinity==earned.state.affinity);
            bond_page();assert(!has("补拍纪念照")&&!has("保存纪念照")&&"anniversaries must not retain deleted photo actions");
            click("首页");
        } else click("回到森林");
        if(mode=="anniversary-backlog") {
            advance(300);assert_anniversary_moment(30);assert(saved().state.anniversary_claimed==3);
            click("回到森林");
        }
        assert_stage(CH_STAGE_ADULT,"anniversary returns to the real adult forest");
        const auto finished=saved();start_ui();advance(5200);
        assert(!has("陪伴21天啦！")&&!has("陪伴30天啦！")&&"reboot must not replay or re-reward a completed anniversary");
        assert(saved().state.coins==finished.state.coins&&saved().state.affinity==finished.state.affinity);
        assert(saved().state.anniversary_claimed==finished.state.anniversary_claimed);
        bond_page();assert(has("重温21天"));click("重温21天");assert_anniversary_moment(21);
        assert(saved().state.coins==finished.state.coins&&saved().state.affinity==finished.state.affinity&&"revisiting a party must not duplicate its reward");
        click("回到森林");assert(partial_flush_calls>0);
    } else if(mode=="world-first-frame") {
        assert_world_first_frame(ch::Scene::Clearing,125,0x294f68,true);
        struct Lighting {int hour,weather,opacity;uint32_t tint;bool stable;};
        const Lighting cases[]={{12,0,0,0x294f68,true},{7,0,27,0xd0e4dc,true},
            {18,3,47,0xe4ae6c,true},{20,3,125,0x294f68,true},
            {12,61,30,0x294f68,false},{12,95,58,0x294f68,false},
            {12,45,0,0x294f68,false},{12,71,0,0x294f68,false}};
        for(int s=0;s<5;++s)for(const auto &lighting:cases) {
            auto fixture=initial;fixture.state.scene=static_cast<ch::Scene>(s);fixture.state.scene_locked=true;
            ch_hw::status.hour=lighting.hour;ch_hw::status.weather_code=lighting.weather;seed(fixture);start_ui();
            const bool night=s==2||s==4;
            const int opacity=lighting.opacity-(lighting.hour==20&&night?105:0);
            auto check=[&]{assert_world_first_frame(static_cast<ch::Scene>(s),opacity,lighting.tint,lighting.stable);};
            check();click_without_advance("喂食");check();click_without_advance("返回");check();
            click_without_advance("卫生");check();click_without_advance("首页");
            click_without_advance("玩耍");check();click_without_advance("首页");
            click_without_advance("主菜单");click_without_advance("陪伴");click_without_advance("首页");check();
            auto *clock=find_text(lv_screen_active(),lighting.hour==7?"07:34":lighting.hour==18?"18:34":lighting.hour==20?"20:34":"12:34");assert(clock);
            lv_obj_send_event(clock,LV_EVENT_CLICKED,nullptr);check();
            click_without_advance("天气");check();click_without_advance("时钟");
            click_without_advance("充电");check();click_without_advance("首页");check();
        }
        // A scene selection and automatic day/night choice must not leave
        // the old background visible until the next application timer.
        const char *names[]={"蘑菇森林","林间草地","夜间森林","蓝天蘑菇草地","月夜蘑菇草地"};
        for(int s=0;s<5;++s) {
            ch_hw::status.hour=20;ch_hw::status.weather_code=3;seed(initial);start_ui();
            click_without_advance("主菜单");click_without_advance("场景");click_without_advance(names[s],true);
            assert(!saved().state.auto_world&&"manual scene selection retains its existing weather-off rule");
            assert_world_first_frame(static_cast<ch::Scene>(s),0,0x294f68,true);
        }
        for(int s:{0,3}) {
            auto fixture=initial;fixture.state.scene=static_cast<ch::Scene>(s);fixture.state.scene_locked=false;
            ch_hw::status.hour=20;seed(fixture);start_ui();
            assert_world_first_frame(static_cast<ch::Scene>(s==0?2:4),20,0x294f68,true);
        }
        std::puts("Verified first-page lighting across 5 scenes, 4 daylight phases, 7 weather conditions and all world pages.");
    } else if(mode=="care-info") {
        click("卫生");const auto full=saved();click("洗澡");
        assert(has("已经很干净啦，不用再洗澡")&&!has("需要泡泡皂，或先叫醒卷卷"));
        assert(saved().state.inventory[5]==full.state.inventory[5]&&saved().state.cleanliness==100);
        auto fixture=initial;fixture.state.inventory[5]=0;seed(fixture);start_ui();advance(120);click("卫生");click("洗澡");
        assert(has("已经很干净啦，不用再洗澡")&&"full cleanliness takes priority even when soap is empty");
        fixture.state.cleanliness=70;seed(fixture);start_ui();advance(120);click("卫生");click("洗澡");
        assert(has("泡泡皂用完了，去背包补给商店补充"));
        fixture.state.inventory[5]=2;fixture.state.sleeping=true;fixture.state.sleep_reason=ch::SleepReason::Manual;
        seed(fixture);start_ui();advance(120);click("卫生");click("洗澡");assert(has("先叫醒卷卷再洗澡")&&saved().state.inventory[5]==2);
        fixture.state.sleeping=false;fixture.state.sleep_reason=ch::SleepReason::None;seed(fixture);start_ui();advance(120);
        click("喂食");click("蟋蟀 ×3");click("主菜单");click("卫生");click("洗澡");assert(has("先等捕食或游戏结束再洗澡")&&saved().state.inventory[5]==2);
        advance(1800);click("首页");click("玩耍");click("捕虫挑战");boot();click("卫生");click("洗澡");
        // Leaving the game properly ends it, so bathing is now valid.
        assert(saved().state.inventory[5]==1&&saved().state.cleanliness==100);
        click("首页");click("主菜单");click("关于");assert(has("版本 0.4.7"));
        for(const char *old:{"无升级与跳跃","天气模型","场景相机不是摄像头"})assert(!has(old,true));
        save_ppm(output,"about-simple.ppm");
        fixture=initial;fixture.state.fullness=20;fixture.state.energy=5;seed(fixture);start_ui();advance(120);
        click("玩耍");click("捕虫挑战");assert(has("精力不足，先睡一会儿")&&!has("先吃点小虫，再玩耍"));
        fixture.state.energy=10;seed(fixture);start_ui();advance(120);click("玩耍");click("捕虫挑战");assert(has("结束")&&"energy exactly ten remains allowed");
        fixture=initial;fixture.state.inventory[6]=0;
        for(const char *game:{"捕虫挑战","叶下寻虫","动作模仿"}){seed(fixture);start_ui();advance(120);click("玩耍");click(game);assert(has("结束")&&"only the ball game needs a bag ball");boot();}
        seed(fixture);start_ui();advance(120);click("玩耍");click("彩球入圈");assert(has("没有彩球了，去背包补给"));
        fixture=initial;fixture.state.inventory[0]=99;seed(fixture);start_ui();advance(120);click("主菜单");click("背包");click("补给商店");
        const auto before=saved();click("蟋蟀\n金币",true);assert(has("已有99份，不能继续购买")&&saved().state.coins==before.state.coins&&saved().state.inventory[0]==99);
        fixture.state.inventory[0]=0;seed(fixture);start_ui();advance(120);click("主菜单");click("背包");click("蟋蟀\n×0");assert(has("没有蟋蟀了，去背包补充"));
    } else if(mode=="rain-shelter") {
        for(int stage=0;stage<CH_STAGE_COUNT;++stage)for(int scene=0;scene<CH_BACKGROUND_COUNT;++scene) {
            auto fixture=initial;fixture.state.growth_ms=stage==0?1000:stage==1?ch::kGrowthTeenMs+1000:ch::kGrowthAdultMs;
            fixture.state.scene=fixture.state.awake_scene=static_cast<ch::Scene>(scene);fixture.state.scene_locked=true;
            seed(fixture);start_ui();advance(240);
            const auto &animation=ch_stages[stage].animations[CH_SHELTER];assert(animation.count==6);
            std::vector<bool> seen(animation.count,false);
            for(unsigned t=0;t<2400;t+=80) {
                auto *pet=frame_object(CH_SHELTER);assert(pet&&source_stage(asset_source(pet))==stage&&"rain automatically displays this stage's real leaf-shelter frames");
                const auto *source=asset_source(pet);for(int i=0;i<animation.count;++i)if(source==animation.frames[i])seen[i]=true;
                const auto *image=static_cast<const lv_image_dsc_t *>(source);const auto *alpha=image->data+image->header.stride*image->header.h;
                int top=128;for(int y=0;y<128;++y)for(int x=0;x<128;++x)if(alpha[y*128+x])top=std::min(top,y);
                int canopy=0,left=128,right=0;
                for(int y=top;y<std::min(128,top+12);++y)for(int x=0;x<128;++x)if(alpha[y*128+x]){++canopy;left=std::min(left,x);right=std::max(right,x);}
                // Visual inspection establishes these top bands as the wide
                // leaf roof, not the old narrow crest/crouch-only drawings.
                assert(canopy>=390&&right-left+1>=50&&"each real shelter frame needs a broad visible leaf roof");
                std::vector<lv_obj_t *> controls;for(const char *name:{"喂食","睡觉","玩耍","卫生","主菜单"})controls.push_back(control(name));
                controls.push_back(find_text(lv_screen_active(),"12:34"));controls.push_back(find_any_prefix(lv_screen_active(),"10月1日"));
                for(auto *o:controls)assert(o);
                assert_pet_uncovered(pet,controls);
                auto *clock=find_text(lv_screen_active(),"12:34");assert(lv_obj_is_visible(clock));
                assert(lv_obj_get_style_text_font(clock,LV_PART_MAIN)==(stage==CH_STAGE_ADULT&&scene==0?&lv_font_montserrat_20:&lv_font_montserrat_28)&&"the whole shelter loop keeps one readable clock size at the original pet scale");
                assert_incremental_matches_full("rain shelter real frame has no partial-render artifacts");advance(80);
            }
            for(bool frame:seen)assert(frame&&"all six leaf-shelter frames must play");
            if(scene==0)save_ppm(output,stage==0?"rain-baby.ppm":stage==1?"rain-teen.ppm":"rain-adult.ppm");
            char rain_file[64];std::snprintf(rain_file,sizeof(rain_file),"rain-%s-scene-%d.ppm",ch_stages[stage].id,scene);save_ppm(output,rain_file);
            ch_hw::status.weather_code=95;advance(120);assert(frame_object(CH_SHELTER));
            ch_hw::status.weather_code=0;advance(120);assert(!frame_object(CH_SHELTER));
            assert(lv_obj_get_style_text_font(find_text(lv_screen_active(),"12:34"),LV_PART_MAIN)==&lv_font_montserrat_28&&"leaving shelter restores the ordinary clock font");
            click("主菜单");click("图鉴");click("避雨");assert(frame_object(CH_SHELTER)&&"leaf shelter is also available in the life catalogue");
            advance(2200);click("睡觉");ch_hw::status.weather_code=61;advance(120);assert(frame_object(CH_SLEEP)&&!frame_object(CH_SHELTER)&&"manual sleep still takes priority over rain");
            ch_hw::status.weather_code=61;
        }
    }

    if(mode=="queued-touch") {
        assert_simple_home();assert(brightness==57&&!touch_screen_off);
        for(unsigned hold_ms:{10u,20u}) {
            queued_button("主菜单",hold_ms);assert_main_menu();
            queued_button("状态",hold_ms,true);assert(has("照顾与状态"));
            queued_button("首页",hold_ms);assert_simple_home();
            queued_button("喂食",hold_ms,true);assert(has("今天想捉哪只小虫？"));
            queued_button("返回",hold_ms);assert_simple_home();
            queued_button("卫生",hold_ms,true);assert(has("让卷卷清清爽爽"));
            queued_button("首页",hold_ms);assert_simple_home();
            queued_button("睡觉",hold_ms,true);assert(saved().state.sleeping&&has("叫醒"));
            queued_button("叫醒",hold_ms,true);assert(!saved().state.sleeping&&has("睡觉"));
            queued_button("玩耍",hold_ms,true);assert(has("一起玩耍"));
            queued_button("首页",hold_ms);assert_simple_home();
        }

        // A pet stroke keeps the same home page; a second captured tap can
        // safely enter the menu after both taps arrive during one stall.
        const auto strokes=saved().state.strokes;const auto sounds=ch_hw::sound_requests.size();
        const auto menu_point=queued_button_center("主菜单");
        queued_capture_tap({233,250},10,true);queued_elapsed(10);
        queued_capture_tap(menu_point,20);queued_elapsed(150);
        assert(queued_samples.size()==5&&"two complete taps retain distinct Down/Up edges with a coalesced Move");
        queued_drain();assert_main_menu();
        assert(saved().state.strokes==strokes+1&&ch_hw::sound_requests.size()==sounds+2&&
               "two captured contacts must trigger both distinct controls exactly once");
        queued_button("首页",10);assert_simple_home();

        // Cancel a contact that LVGL has already accepted. It must release
        // visual pressing without turning a cancelled gesture into a click.
        auto point=queued_button_center("主菜单");const auto cancel_sounds=ch_hw::sound_requests.size();
        queued_samples.record(point.x,point.y,true,lv_tick_get());
        lv_indev_read(queued_input);assert(!queued_samples.pending());
        queued_elapsed(10);queued_samples.cancel(lv_tick_get());queued_drain();
        assert_simple_home();assert(ch_hw::sound_requests.size()==cancel_sounds);
        queued_samples.record(point.x,point.y,false,lv_tick_get());queued_drain();
        queued_button("主菜单",10);assert_main_menu();queued_button("首页",10);

        // Overflow must cancel rather than replay a partial press/release.
        // Feed alternating edges until the queue clears itself to Cancel.
        point=queued_button_center("主菜单");const auto overflow_sounds=ch_hw::sound_requests.size();
        queued_samples.record(point.x,point.y,true,lv_tick_get());lv_indev_read(queued_input);
        bool overflowed=false;bool down=true;
        for(unsigned i=0;i<512;++i) {
            const auto previous=queued_samples.size();down=!down;queued_elapsed(1);
            queued_samples.record(point.x,point.y,down,lv_tick_get());
            if(previous>queued_samples.size()){overflowed=true;break;}
        }
        assert(overflowed&&queued_samples.size()==1);
        queued_drain();assert_simple_home();assert(ch_hw::sound_requests.size()==overflow_sounds);
        queued_samples.record(point.x,point.y,false,lv_tick_get());queued_drain();
        queued_button("主菜单",20);assert_main_menu();queued_button("首页",10);

        // Even a complete queued short contact must keep the dark display
        // asleep; only a subsequent physical key may restore this page.
        point=queued_button_center("主菜单");pwr();assert(brightness==0&&touch_screen_off);
        const auto wake_sounds=ch_hw::sound_requests.size();
        queued_capture_tap(point,10);queued_elapsed(150);queued_drain();
        assert(brightness==0&&touch_screen_off&&has("短按侧键唤醒"));
        assert(ch_hw::sound_requests.size()==wake_sounds&&"a dark queued contact must never click or wake");
        pwr();assert(brightness==57&&!touch_screen_off&&!has("短按侧键唤醒"));assert_simple_home();
        queued_button("主菜单",10,true);assert_main_menu();queued_button("首页",10);
        assert_incremental_matches_full("queued light taps preserve full/partial pixels");
        std::printf("Queued touch: real Buffer/LVGL 10/20 ms edges survived 150 ms worker stalls, movement, two contacts, cancel/overflow, ignored dark taps and key-only wake. Physical latency remains a device check.\n");
    } else if(mode=="power-touch") {
        assert_simple_home();assert(brightness==57&&has("电量 85%"));

        // Watch actual paint events rather than calls to a particular helper:
        // a hidden-world status page isolates unchanged labels from animated
        // pet dirty areas, which can intersect the home battery strip.
        pointer_button("主菜单",3);pointer_button("状态");assert(has("照顾与状态"));advance(240);
        unsigned battery_draws=0,health_draws=0;
        auto *battery=find_text(lv_screen_active(),"电量 85%");auto *health=find_text(lv_screen_active(),"82/100");assert(battery&&health);
        lv_obj_add_event_cb(battery,count_draw,LV_EVENT_DRAW_MAIN,&battery_draws);lv_obj_add_event_cb(health,count_draw,LV_EVENT_DRAW_MAIN,&health_draws);
        lv_refr_now(nullptr);battery_draws=health_draws=0;advance(1200);
        if(battery_draws||health_draws)std::fprintf(stderr,"Stable paints: battery=%u health=%u last flush=%d,%d..%d,%d\n",battery_draws,health_draws,last_flush_area.x1,last_flush_area.y1,last_flush_area.x2,last_flush_area.y2);
        assert(battery_draws==0&&health_draws==0&&"unchanged panel labels must not redraw on every status refresh");
        ch_hw::status.battery_percent=84;advance(500);assert(has("电量 84%")&&battery_draws==1&&health_draws==0);
        advance(800);assert(battery_draws==1&&health_draws==0);
        pointer_button("首页",3);assert_simple_home();
        advance(20100);assert(brightness==28);const auto blank_sounds=ch_hw::sound_requests.size();
        pointer_point={233,145};pointer_down=true;advance_pointer(40);
        assert(brightness==57&&has("主菜单")&&ch_hw::sound_requests.size()==blank_sounds&&"a blank touch must restore brightness on press without triggering a button");
        pointer_down=false;advance_pointer(80);assert_simple_home();assert(ch_hw::sound_requests.size()==blank_sounds);

        // Use the real pointer read callback and LVGL hit testing, including
        // points outside the visual rectangle but inside the circular screen.
        for(int distance:{3,8}){
            pointer_button("喂食",distance,true);assert(has("今天想捉哪只小虫？"));pointer_button("返回",3);assert_simple_home();
            pointer_button("玩耍",distance,true);assert(has("一起玩耍"));pointer_button("首页",3);assert_simple_home();
            pointer_button("睡觉",distance,true);assert(saved().state.sleeping&&has("叫醒"));pointer_button("叫醒",distance,true);assert(!saved().state.sleeping&&has("睡觉"));
            pointer_button("卫生",distance,true);assert(has("让卷卷清清爽爽"));pointer_button("首页",3);assert_simple_home();
        }
        pointer_button("主菜单",3);assert_main_menu();
        auto *hidden_pet=frame_object(CH_IDLE);assert(hidden_pet);auto *hidden_world=lv_obj_get_parent(hidden_pet);assert(lv_obj_has_flag(hidden_world,LV_OBJ_FLAG_HIDDEN));
        const auto *hidden_source=lv_image_get_src(hidden_pet);std::vector<lv_point_t> hidden_positions;
        for(uint32_t i=0;i<lv_obj_get_child_count(hidden_world);++i){auto *child=lv_obj_get_child(hidden_world,i);hidden_positions.push_back({lv_obj_get_x(child),lv_obj_get_y(child)});}
        advance(240);lv_refr_now(nullptr);const auto static_flushes=flush_calls;advance(1200);
        assert(flush_calls==static_flushes&&"a static menu must not repaint an invisible animated world");
        assert(lv_image_get_src(hidden_pet)==hidden_source);
        for(uint32_t i=0;i<hidden_positions.size();++i){auto *child=lv_obj_get_child(hidden_world,i);assert(lv_obj_get_x(child)==hidden_positions[i].x&&lv_obj_get_y(child)==hidden_positions[i].y);}
        pointer_button("首页",3);assert_simple_home();assert_incremental_matches_full("real expanded pointer clicks preserve full/partial pixels");

        auto assert_preferences=[](int value,int idle){const auto data=saved();assert(data.state.brightness==value&&data.state.idle_minutes==idle);};
        advance(19800);assert(brightness==57);advance(240);assert(brightness==28);assert_preferences(57,1);
        save_ppm(output,"power-touch-half-bright.ppm");pointer_button("主菜单",3);assert_main_menu();assert(brightness==57);assert_preferences(57,1);
        advance(20100);assert(brightness==28);boot();assert_simple_home();assert(brightness==57);assert_preferences(57,1);
        advance(20100);assert(brightness==28);pwr();assert(brightness==0);assert_preferences(57,1);
        step_clock_on_read=2;pwr();assert(step_clock_on_read==0&&brightness==57&&"a key wake one ms after tick must not underflow idle time and immediately darken again");assert_preferences(57,1);pwr();assert(brightness==0);
        assert(has("短按侧键唤醒"));const auto wake_sounds=ch_hw::sound_requests.size();pointer_tap(183,55);
        assert(brightness==0&&has("短按侧键唤醒")&&ch_hw::sound_requests.size()==wake_sounds&&"a dark screen must ignore even a touch over its menu");
        pointer_point={233,250};pointer_down=true;advance_pointer(1800);
        assert(brightness==0&&has("短按侧键唤醒")&&ch_hw::sound_requests.size()==wake_sounds);
        boot();assert(brightness==57&&!has("短按侧键唤醒"));
        pointer_down=false;advance_pointer(80);assert_simple_home();assert_preferences(57,1);
        assert(ch_hw::sound_requests.size()==wake_sounds+1&&"a held dark finger must not become a stroke after a physical wake");
        pointer_button("主菜单",3);assert_main_menu();pointer_button("首页",3);assert_simple_home();
        advance(60120);assert(brightness==0&&has("短按侧键唤醒"));assert_preferences(57,1);boot();assert(brightness==57&&has("主菜单"));
        pointer_button("主菜单",3);step_clock_on_read=2;boot();assert(step_clock_on_read==0&&brightness==57&&has("主菜单")&&"BOOT returning home after the cached tick timestamp must preserve the awake display");

        pointer_button("主菜单",3);pointer_button("场景");assert(has("自由换场景"));advance(20100);assert(brightness==28);
        pointer_button("蓝天蘑菇草地");assert_simple_home();assert(saved().state.scene==ch::Scene::Meadow&&brightness==57);assert_preferences(57,1);
        assert_incremental_matches_full("scene selection restores user brightness and renders the new world immediately");

        for(int low:{18,10}){
            auto fixture=initial;fixture.state.brightness=low;fixture.state.idle_minutes=0;seed(fixture);start_ui();advance(120);
            assert(brightness==low);advance(20100);assert(brightness==10);assert_preferences(low,0);
            advance(41000);assert(brightness==10&&!has("短按侧键唤醒"));assert_preferences(low,0);
            pointer_button("主菜单",3);assert(brightness==low);assert_main_menu();boot();assert_simple_home();assert(brightness==low);assert_preferences(low,0);
        }

        // Growth uses a paper panel too; its explicit celebration must still
        // animate real forms and confetti while the hidden world stays idle.
        auto growing=initial;growing.state.growth_ms=ch::kGrowthTeenMs-500;growing.state.idle_minutes=0;seed(growing);start_ui();advance(520);
        assert_growth_moment(CH_STAGE_BABY,CH_STAGE_TEEN);auto *growth_world=lv_obj_get_parent(frame_object(CH_IDLE));assert(lv_obj_has_flag(growth_world,LV_OBJ_FLAG_HIDDEN));
        std::vector<lv_obj_t *> strips;std::vector<lv_point_t> strip_positions;
        for(uint32_t i=0;i<lv_obj_get_child_count(lv_screen_active());++i){auto *child=lv_obj_get_child(lv_screen_active(),i);if((lv_obj_get_width(child)==3&&lv_obj_get_height(child)==9)||(lv_obj_get_width(child)==8&&lv_obj_get_height(child)==3)){strips.push_back(child);strip_positions.push_back({lv_obj_get_x(child),lv_obj_get_y(child)});}}
        assert(strips.size()==24);const auto party_flushes=flush_calls;advance(400);int moved=0;
        for(unsigned i=0;i<strips.size();++i)if(lv_obj_get_x(strips[i])!=strip_positions[i].x||lv_obj_get_y(strips[i])!=strip_positions[i].y)++moved;
        assert(moved==24&&flush_calls>party_flushes);assert_growth_moment(CH_STAGE_BABY,CH_STAGE_TEEN);advance(21000);
        assert(brightness==57&&has("回到森林"));assert_preferences(57,0);assert_growth_moment(CH_STAGE_BABY,CH_STAGE_TEEN);
        save_ppm(output,"power-touch-growth.ppm");pointer_button("回到森林");assert_simple_home();assert(brightness==57);
        assert(partial_flush_calls>0);std::printf("Power/touch: actual timer-driven clicks, 3/8 px arc hit areas, stable status paints, hidden-world pause, 20 s dim/restore and saved preferences passed. Physical touch latency remains a device check.\n");
    } else if(mode=="poop-perch") {
        const char *names[]={"蘑菇森林","林间草地","夜间森林","蓝天蘑菇草地","月夜蘑菇草地"};
        const int sequence[]={0,2,1,3,4,0};
        for(int stage=0;stage<CH_STAGE_COUNT;++stage)for(unsigned count=0;count<=3;++count){
            auto fixture=initial;fixture.state.poop_count=count;fixture.state.scene=fixture.state.awake_scene=ch::Scene::Forest;
            fixture.state.scene_locked=true;fixture.state.growth_ms=stage==CH_STAGE_BABY?1000:stage==CH_STAGE_TEEN?ch::kGrowthTeenMs+1000:ch::kGrowthAdultMs;
            seed(fixture);start_ui();advance(120);assert_stage(stage,"perch fixture keeps its real growth stage");
            for(int scene:sequence){
                if(static_cast<int>(saved().state.scene)!=scene){open_scenes_from_menu();click(names[scene],true);}
                assert(static_cast<int>(saved().state.scene)==scene&&saved().state.scene_locked);
                assert_droppings(count);assert_droppings_clear_subject_and_home_controls();
                assert_incremental_matches_full("scene changes immediately relocate physical droppings without remnants");
                assert(saved().state.poop_count==count&&saved().state.meals==fixture.state.meals);
                for(int item=0;item<ch::kItemCount;++item)assert(saved().state.inventory[item]==fixture.state.inventory[item]);
                if(count==3){const char *id=ch_stages[stage].id;save_ppm(output,(std::string("poop-perch-")+id+"-"+std::to_string(scene)+".ppm").c_str());}
            }
            if(count){click("卫生");assert_droppings(count);click("清理便便");assert_droppings(0);assert(saved().state.poop_count==0);click("首页");assert_droppings(0);assert_incremental_matches_full("branch cleanup leaves no pile remnants");}
        }
        assert(partial_flush_calls>0);
    } else if(heart_check) {
        const int stage=mode=="pet-heart-baby"?CH_STAGE_BABY:mode=="pet-heart-teen"?CH_STAGE_TEEN:CH_STAGE_ADULT;
        const auto &animation=ch_stages[stage].animations[CH_PET];assert(animation.count==6);
        assert_heart_hidden();click("主菜单");click("图鉴");click_without_advance("抚摸");
        for(bool snapshot_check:{false,true}){
            bool seen[6]{};
            if(snapshot_check){
                advance(1800);click("主菜单");click("图鉴");click_without_advance("抚摸");
                click_without_advance("主菜单");assert_heart_hidden();click_without_advance("首页");
            }
            const auto started=clock_ms;
            for(int step=0;step<82;++step){
                if(clock_ms-started<1600){
                    auto *heart=assert_pet_heart(stage);(void)heart;
                    const auto *source=asset_source(frame_object(CH_PET));int frame=-1;
                    for(int i=0;i<6;++i)if(source==animation.frames[i])frame=i;assert(frame>=0);
                    if(!seen[frame]){seen[frame]=true;save_ppm(output,(mode+(snapshot_check?"-snapshot-":"-home-")+std::to_string(frame)+".ppm").c_str());if(snapshot_check)assert_snapshot_has_heart(stage);}
                    assert_incremental_matches_full(snapshot_check?"host snapshot pet heart dirty-region redraw":"home pet heart dirty-region redraw");
                }else assert_heart_hidden();
                advance(20);
            }
            for(bool frame:seen)assert(frame&&"all six pet frames need a visible pink heart at home and in host snapshots");
        }
        assert_heart_hidden();
        // A queued milestone owns a new screen and must not inherit the heart
        // from the preceding petting world, even when both forms are visible.
        auto growing=initial;growing.state.growth_ms=stage==CH_STAGE_BABY?ch::kGrowthTeenMs-500:ch::kGrowthAdultMs-500;
        seed(growing);start_ui();advance(520);assert(has("回到森林"));assert_heart_hidden();assert_incremental_matches_full("growth excludes petting hearts");
        assert(partial_flush_calls>0);
    } else if(sick_stage_check) {
        const int stage=mode=="sick-stage-baby"?CH_STAGE_BABY:mode=="sick-stage-teen"?CH_STAGE_TEEN:CH_STAGE_ADULT;
        const auto &animation=ch_stages[stage].animations[CH_SICK];assert(animation.count==4&&animation.fps==2&&animation.loop);
        bool seen[4]{};int previous=-1;uint64_t changed=0;int changes=0;
        for(int step=0;step<210;++step){
            auto *pet=assert_pet_layout("redesigned low sick posture");assert(frame_object(CH_SICK)==pet&&source_stage(asset_source(pet))==stage);assert_heart_hidden();
            int frame=-1;for(int i=0;i<4;++i)if(asset_source(pet)==animation.frames[i])frame=i;assert(frame>=0);
            if(!seen[frame]){
                if(stage==CH_STAGE_ADULT){
                    const auto *original=static_cast<const lv_image_dsc_t *>(asset_source(pet));
                    const auto *displayed=static_cast<const lv_image_dsc_t *>(lv_image_get_src(pet));
                    auto source_pixel=[](const lv_image_dsc_t *image,int x,int y){uint16_t value;std::memcpy(&value,image->data+y*image->header.stride+x*2,2);return value;};
                    auto alpha_pixel=[](const lv_image_dsc_t *image,int x,int y){return image->data[image->header.stride*image->header.h+y*(image->header.stride/2)+x];};
                    auto assert_rendered=[&](int x,int y,uint16_t expected){
                        lv_area_t area;lv_obj_get_coords(pet,&area);const float scale=lv_image_get_scale_x(pet)/256.f;
                        // LVGL's nearest-neighbor transform is anchored at
                        // source pixel centers x/y with pivot (0,0).
                        const int px=static_cast<int>(std::lround(area.x1+x*scale)),py=static_cast<int>(std::lround(area.y1+y*scale));
                        assert(px>=0&&px<466&&py>=0&&py<466);
                        const auto *pixel=&pixels[(py*466+px)*4];const uint16_t actual=rgb565((pixel[2]<<16)|(pixel[1]<<8)|pixel[0]);
                        if(actual!=expected)std::fprintf(stderr,"Adult sick frame%d source %d,%d displayed %d,%d expected=%04x actual=%04x\n",frame,x,y,px,py,expected,actual);
                        assert(actual==expected&&"the native display must render the recolored body and preserved blue accessories, not only change its descriptor");
                    };
                    // These are real approved-PNG pixels. 1CCB is a body
                    // shadow in this sick design, while 4EB4 is a cyan stripe
                    // and 869D belongs to the ice bag above the eye.
                    int body_x=55,body_y=86;
                    if(frame){body_x=body_y=-1;for(int y=75;y<95&&body_x<0;++y)for(int x=40;x<70;++x)if(alpha_pixel(original,x,y)==255&&source_pixel(original,x,y)==0x1ccb){body_x=x;body_y=y;break;}}
                    assert(body_x>=0&&source_pixel(original,body_x,body_y)==0x1ccb&&alpha_pixel(original,body_x,body_y)==255);
                    const uint16_t gray=source_pixel(displayed,body_x,body_y);assert(gray!=0x1ccb);
                    const int r=(gray>>11)*255/31,g=((gray>>5)&63)*255/63,b=(gray&31)*255/31;
                    assert(std::max({r,g,b})-std::min({r,g,b})<=32&&g>=r&&g>=b&&g<153&&"adult sick body shadows must be muted gray teal instead of saturated green");
                    assert_rendered(body_x,body_y,gray);
                    const int stripe_x[]={48,55,60,58},stripe_y[]={85,84,82,79},ice_x[]={94,88,95,94},ice_y[]={67,66,62,55};
                    for(bool ice:{false,true}){const int x=ice?ice_x[frame]:stripe_x[frame],y=ice?ice_y[frame]:stripe_y[frame];const uint16_t color=ice?0x869d:0x4eb4;
                        assert(alpha_pixel(original,x,y)==255&&source_pixel(original,x,y)==color&&source_pixel(displayed,x,y)==color&&"the ice bag and true cyan stripe must retain their exact approved RGB565 colors");assert_rendered(x,y,color);}
                }
                seen[frame]=true;save_ppm(output,(mode+"-"+std::to_string(frame)+".ppm").c_str());
            }
            if(frame!=previous){if(changes>1)assert(clock_ms-changed>=460&&clock_ms-changed<=540&&"sick source frames must advance gently at two frames per second");previous=frame;changed=clock_ms;++changes;}
            assert_pet_uncovered(pet,{control("喂食"),control("睡觉"),control("玩耍"),control("卫生")});
            assert_incremental_matches_full("redesigned sick frames dirty-region redraw");advance(20);
        }
        for(bool frame:seen)assert(frame&&"all four redesigned sick drawings must appear for each growth stage");
        assert(changes>=8&&saved().state.sick&&saved().state.growth_ms==initial.state.growth_ms&&partial_flush_calls>0);
    } else if(color_check) {
        const int stage=mode=="colors-baby"?CH_STAGE_BABY:mode=="colors-teen"?CH_STAGE_TEEN:CH_STAGE_ADULT;
        const char *names[]={"normal","happy","unhappy","hungry","sick","sleep"};
        for(int variant=0;variant<6;++variant){
            auto fixture=initial;fixture.state.mood=variant==1?90:variant==2?20:60;
            fixture.state.fullness=variant==3?20:70;fixture.state.sick=variant==4;
            fixture.state.sickness_cause=variant==4?ch::SicknessCause::Weakness:ch::SicknessCause::None;
            fixture.state.sleeping=variant==5;fixture.state.sleep_reason=variant==5?ch::SleepReason::Manual:ch::SleepReason::None;
            seed(fixture);start_ui();advance(240);auto *pet=assert_pet_layout("approved body color at each growth stage");
            const auto *original=static_cast<const lv_image_dsc_t *>(asset_source(pet));
            const auto *displayed=static_cast<const lv_image_dsc_t *>(lv_image_get_src(pet));
            assert(source_stage(original)==stage);
            assert((displayed==original)==(variant==0||variant==5)&&"normal and sleeping must use the exact original image");
            const size_t offset=original->header.stride*original->header.h;
            assert(displayed->data_size==original->data_size&&!std::memcmp(displayed->data+offset,original->data+offset,original->data_size-offset));
            for(int tick=0;tick<10;++tick){advance(100);assert_incremental_matches_full("colored real frame dirty-region redraw");}
            save_ppm(output,(mode+"-"+names[variant]+".ppm").c_str());
        }
    } else if(mode=="game-clock") {
        const auto before=saved();click_without_advance("玩耍");advance_pointer(10);
        uint64_t started=0;auto *start=control("捕虫挑战");
        lv_obj_add_event_cb(start,[](lv_event_t *event){*static_cast<uint64_t *>(lv_event_get_user_data(event))=clock_ms;},LV_EVENT_RELEASED,&started);
        pointer_button("捕虫挑战");assert(started&&started%40!=0&&"the real button release must start between UI timer ticks");
        hunt_hit();assert(has("命中 1",true));const auto deadline=started+20000;
        assert(deadline>clock_ms+10);
        while(clock_ms<deadline-10)advance_pointer(10);
        assert(clock_ms==deadline-10&&has("结束")&&!has("一起玩耍")&&"a game must not settle before 20 seconds because start clock was counted twice");
        assert(saved().state.coins==before.state.coins&&saved().state.affinity==before.state.affinity);
        auto *last_target=unique_sized(60,48);advance_pointer(10);assert(clock_ms==deadline);
        lv_obj_send_event(last_target,LV_EVENT_CLICKED,nullptr);assert(has("命中 1",true)&&"an exact-deadline catch must not add another hit");
        click_without_advance("结束");assert_simple_home();const auto earned=saved();
        assert(earned.state.coins==before.state.coins+2&&earned.state.care_total==before.state.care_total+1&&earned.state.affinity==before.state.affinity+2&&"manual end at exactly 20 seconds must include the pending timer fraction");
        advance(200);start_ui();advance(240);assert(saved().state.coins==earned.state.coins&&saved().state.affinity==earned.state.affinity&&saved().state.care_total==earned.state.care_total);
        assert(partial_flush_calls>0);
    } else if(games_check) {
        const int stage=mode=="games-baby"?CH_STAGE_BABY:mode=="games-teen"?CH_STAGE_TEEN:CH_STAGE_ADULT;
        const auto baseline=saved();const char *names[]={"捕虫挑战","叶下寻虫","动作模仿","彩球入圈"};
        for(int kind=0;kind<4;++kind){
            click("玩耍");auto *pet=assert_pet_layout("four-game lobby");std::vector<lv_obj_t *> lobby;
            for(const char *name:names){auto *button=control(name);lobby.push_back(button);lv_area_t area;lv_obj_get_coords(button,&area);assert_inside_circle(area);
                assert(lv_obj_get_width(button)==115&&lv_obj_get_height(button)==40);}
            lobby.push_back(control("首页"));lobby.push_back(lobby_hint());assert_controls_separate(lobby);assert_pet_uncovered(pet,lobby);assert_incremental_matches_full("four-game lobby paint");
            save_ppm(output,(mode+"-lobby-"+std::to_string(kind)+".ppm").c_str());const auto before=saved();
            pointer_button(names[kind]);assert(has("结束")&&has("命中 0",true));assert_running_game_layout(kind);
            if(kind==0){
                auto *target=unique_sized(60,48);const auto old=object_center(target);advance(240);const auto moved=object_center(target);
                assert(old.x!=moved.x||old.y!=moved.y);pointer_tap(233,315);assert(has("命中 0",true));
                const auto hit_at=hunt_point();hunt_hit();assert(has("命中 1",true)&&capture_insect(stage));
                pointer_tap(hit_at.x,hit_at.y);assert(has("命中 1",true));advance(300);assert_capture_mouth(stage);
                assert_incremental_matches_full("moving insect actual tongue hit");
            }else if(kind==1){
                auto *answer=revealed_leaf();const auto original=object_center(answer);pointer_object(answer);assert(has("命中 0",true));
                wait_leaf_input(stage);const auto shuffled=object_center(answer);assert((original.x==shuffled.x)==(stage==CH_STAGE_BABY));
                auto cards=leaf_cards();lv_obj_t *wrong=cards.front()==answer?cards.back():cards.front();pointer_object(wrong);assert(has("命中 0",true));
                advance(850);answer=revealed_leaf();wait_leaf_input(stage);pointer_object(answer);assert(has("命中 1",true)&&capture_insect(stage));
                advance(460);assert_capture_mouth(stage);assert_running_game_layout(kind);assert_incremental_matches_full("leaf reveal shuffle and real capture");
            }else if(kind==2){
                auto sequence=watch_mimic_demo(stage+1);mimic_answer((sequence.front()+1)%3);assert(has("命中 0",true));
                advance(1100);sequence=watch_mimic_demo(stage+1);
                for(int option:sequence)mimic_answer(option);
                assert(has("命中 1",true));assert_running_game_layout(kind);assert_incremental_matches_full("actual pose memory correct answer");
            }else{
                auto *ball=unique_sized(52,52);pointer_object(ball);assert(has("命中 0",true)&&"a tap alone must not score the ball game");
                auto *active=active_ball_goal();auto wrong=object_center(active);wrong.x=wrong.x==144?322:144;
                drag_ball_to(wrong);assert(has("命中 0",true));advance(1100);
                drag_ball_to(object_center(active_ball_goal()));assert(has("命中 1",true));
                bool walk_seen=false,wave_seen=false,happy_seen=false;
                for(unsigned t=0;t<1800;t+=20){
                    for(auto pose:{CH_WALK,CH_WAVE,CH_HAPPY})if(auto *form=frame_object(pose)){
                        assert(source_stage(asset_source(form))==stage);
                        if(pose==CH_WALK){if(!walk_seen)save_ppm(output,(mode+"-ball-walk.ppm").c_str());walk_seen=true;}
                        else if(pose==CH_WAVE){if(!wave_seen)save_ppm(output,(mode+"-ball-wave.ppm").c_str());wave_seen=true;}
                        else{if(!happy_seen)save_ppm(output,(mode+"-ball-happy.ppm").c_str());happy_seen=true;}
                    }
                    assert_running_game_layout(kind);advance(20);
                }
                assert(walk_seen&&wave_seen&&happy_seen&&has("命中 1",true)&&"walk, wave and celebration must share one score without duplicate release rewards");
                assert_incremental_matches_full("real pointer ball drag and release");
            }
            save_ppm(output,(mode+"-"+std::to_string(kind)+"-hit.ppm").c_str());advance(22000);
            assert(has("一起玩耍")&&has("捕虫挑战")&&!has("结束"));const auto earned=saved();
            assert(earned.state.coins==before.state.coins+2&&earned.state.care_total==before.state.care_total+1);
            assert(earned.state.affinity==before.state.affinity+2&&earned.state.daily_mask==ch::DailyPlay);
            assert(earned.state.meals==baseline.state.meals&&earned.state.fullness==baseline.state.fullness);
            for(int item=0;item<ch::kItemCount;++item)assert(earned.state.inventory[item]==baseline.state.inventory[item]);
            click("首页");advance(1800);assert_simple_home();start_ui();advance(240);
            assert(saved().state.coins==earned.state.coins&&saved().state.care_total==earned.state.care_total&&saved().state.affinity==earned.state.affinity&&"completion and reboot must not duplicate game rewards");
        }
        const auto complete=saved();assert(complete.state.affinity==baseline.state.affinity+8&&complete.state.care_total==baseline.state.care_total+4);
        click("玩耍");pointer_button("捕虫挑战");hunt_hit();pointer_button("结束");advance(1700);assert_simple_home();
        const auto manual=saved();assert(manual.state.coins==complete.state.coins+2&&manual.state.care_total==complete.state.care_total+1&&manual.state.affinity==complete.state.affinity);
        click("玩耍");pointer_button("捕虫挑战");pointer_button("结束");advance(1700);assert_simple_home();
        start_ui();advance(240);assert(saved().state.coins==manual.state.coins&&saved().state.care_total==manual.state.care_total&&saved().state.affinity==manual.state.affinity);
        assert(partial_flush_calls>0);std::puts("Four games passed real pointer hits, memory misses and successes, leaf tracking, drag/release, full-session affection and one-time rewards.");
    } else if(capture_check) {
        const int stage=mode=="capture-baby"?CH_STAGE_BABY:mode=="capture-teen"?CH_STAGE_TEEN:CH_STAGE_ADULT;
        const auto baseline=saved();click("玩耍");const auto game_start=clock_ms;click_without_advance("捕虫挑战");advance(200);
        const auto first_point=hunt_point();hunt_hit();assert(capture_insect(stage));assert(has("命中 1",true));
        pointer_tap(first_point.x,first_point.y);assert(has("命中 1",true)&&"repeated hits must wait for the whole capture");
        advance(280);assert_capture_mouth(stage);assert(capture_insect(stage));assert_incremental_matches_full("game tongue extension");
        advance(160);const double long_tongue=capture_length(stage);auto *food=capture_insect(stage);assert(food);
        const int before_x=lv_obj_get_x(food);save_ppm(output,(mode+"-tongue.ppm").c_str());
        advance(160);assert(capture_length(stage)<long_tongue);food=capture_insect(stage);assert(food&&lv_obj_get_x(food)<before_x&&"the captured insect must return with the tongue");
        assert_incremental_matches_full("game tongue retracts with food");advance(140);assert(!capture_insect(stage));
        advance(600);assert(has("命中 1",true));hunt_hit();assert(capture_insect(stage));assert(has("命中 2",true));
        advance(560);assert_capture_mouth(stage);assert(capture_insect(stage));assert_incremental_matches_full("second insect real capture");
        click_without_advance("结束");assert(has("命中 2",true)&&has("结束")&&capture_insect(stage)&&"ending must keep the final capture visible until it completes");advance(1000);assert_simple_home();
        const auto ended=saved();assert(ended.state.meals==baseline.state.meals&&ended.state.fullness==baseline.state.fullness);
        for(int i=0;i<7;++i)assert(ended.state.inventory[i]==baseline.state.inventory[i]);
        assert(ended.state.coins==baseline.state.coins+4&&ended.state.care_total==baseline.state.care_total+1);
        // A last-second successful catch finishes visually after the 20 s
        // deadline, without permitting another hit or dropping its tongue.
        advance(1800);click("玩耍");const auto late_start=clock_ms;click_without_advance("捕虫挑战");advance(19400);
        const auto last_point=hunt_point();hunt_hit();advance(640);assert(clock_ms>late_start+20000);assert_capture_mouth(stage);assert(capture_insect(stage));
        assert(has("命中 1",true)&&has("结束"));pointer_tap(last_point.x,last_point.y);assert(has("命中 1",true));advance(1000);
        assert(has("捕虫挑战")&&!has("结束"));assert_stage(stage,"game expiry restores the actual grown form");
        assert(partial_flush_calls>0&&clock_ms>game_start);
    } else if(mode=="growth-sleep") {
        click("睡觉");advance(260);assert(saved().state.growth_ms>=ch::kGrowthTeenMs&&saved().state.sleeping&&!has("回到森林"));
        assert(frame_object(CH_SLEEP));click("叫醒");assert_growth_moment(CH_STAGE_BABY,CH_STAGE_TEEN);
        click("回到森林");assert_stage(CH_STAGE_TEEN,"growth waits until awake");
    } else if(mode=="growth-dark") {
        pwr();assert(brightness==0);advance(1000);assert(saved().state.growth_ms>=ch::kGrowthTeenMs&&!has("回到森林"));
        pwr();assert(brightness>0);assert_growth_moment(CH_STAGE_BABY,CH_STAGE_TEEN);save_ppm(output,"growth-dark-wake.ppm");
        click("回到森林");assert_stage(CH_STAGE_TEEN,"woken growth milestone dismissed");
    } else if(mode=="growth-feeding") {
        click("喂食");click("蟋蟀 ×3");advance(400);
        assert(saved().state.growth_ms>=ch::kGrowthTeenMs&&!has("回到森林"));assert_capture_mouth(CH_STAGE_BABY);
        assert_incremental_matches_full("growth preserves the ongoing baby capture");advance(1000);
        assert_growth_moment(CH_STAGE_BABY,CH_STAGE_TEEN);assert(saved().state.meals==1&&saved().state.inventory[0]==2);
        click("回到森林");assert_stage(CH_STAGE_TEEN,"growth after feeding");
    } else if(round_layout) {
        const int stage=mode=="round-baby"?CH_STAGE_BABY:mode=="round-teen"?CH_STAGE_TEEN:CH_STAGE_ADULT;
        assert_stage(stage,"round board layout");assert_home_arc();assert_catalog_clears_side_controls(stage);assert_droppings(3);
        save_ppm(output,(mode+"-home.ppm").c_str());
        click("主菜单");assert_main_menu();
        int index=0;for(const char *name:{"状态","喂食","玩耍","卫生","背包","陪伴","设置","图鉴","关于"}){
            auto *tile=control(name);lv_area_t area;lv_obj_get_coords(tile,&area);assert_inside_circle(area);
            assert(lv_obj_get_width(tile)==100&&lv_obj_get_height(tile)==94);
            assert(lv_obj_get_x(tile)==76+(index%3)*107&&lv_obj_get_y(tile)==90+(index/3)*100);++index;
        }
        save_ppm(output,(mode+"-menu.ppm").c_str());click("首页");
        const char *tile_names[]={"状态","喂食","玩耍","卫生","背包","陪伴","设置","图鉴","关于"};
        const char *page_titles[]={"照顾与状态","今天想捉哪只小虫？","一起玩耍","让卷卷清清爽爽","背包","亲密与陪伴","设置","生活图鉴","关于"};
        for(int tile=0;tile<9;++tile){click("主菜单");click(tile_names[tile]);assert(has(page_titles[tile]));boot();assert_simple_home();}
        for(const char *scene:{"林间草地","夜间森林","蓝天蘑菇草地","月夜蘑菇草地","蘑菇森林"}){
            open_scenes_from_menu();click(scene,true);assert_home_arc();assert_droppings(3);
            assert_incremental_matches_full("round scene controls and physical droppings");
        }
        click("卫生");assert(has("清理便便"));assert_droppings(3);save_ppm(output,(mode+"-poop.ppm").c_str());
        click("清理便便");assert(saved().state.poop_count==0);assert_droppings(0);click("首页");
        // The 100/100 layout fixture is deliberately too full to eat. Restore
        // the same persisted stage with normal hunger for real feeding routes.
        auto ready=saved();ready.state.fullness=70;seed(ready);start_ui();advance(120);
        click("喂食");click("蟋蟀 ×3");
        for(const char *name:{"喂食","睡觉","玩耍","卫生"}){
            std::vector<lv_obj_t *> hidden;collect_text(lv_screen_active(),name,hidden);assert(hidden.size()==1);
            assert(lv_obj_has_flag(lv_obj_get_parent(hidden.front()),LV_OBJ_FLAG_HIDDEN)&&"feeding must clear the side controls from the tongue and insect");
        }
        advance(460);assert_capture_mouth(stage);
        auto *capture_subtitle=find_any_prefix(lv_screen_active(),"未校时 ·");assert(capture_subtitle&&lv_obj_has_flag(capture_subtitle,LV_OBJ_FLAG_HIDDEN)==(stage==CH_STAGE_ADULT));
        save_ppm(output,(mode+"-capture.ppm").c_str());
        assert_incremental_matches_full("round feeding with side controls hidden");advance(1000);
        assert(has("喂食")&&has("睡觉")&&has("玩耍")&&has("卫生")&&"all four shortcuts must return after feeding");
        click("睡觉");assert(has("叫醒"));auto *pet=assert_pet_layout("round sleeping");
        assert_pet_uncovered(pet,{control("喂食"),control("叫醒"),control("玩耍"),control("卫生")});
        save_ppm(output,(mode+"-sleep.ppm").c_str());click("叫醒");advance(2000);
        click("玩耍");pet=assert_pet_layout("game before start");
        std::vector<lv_obj_t *> lobby_controls;
        for(const char *name:{"捕虫挑战","叶下寻虫","动作模仿","彩球入圈","首页"})lobby_controls.push_back(control(name));
        for(auto *button:lobby_controls){lv_area_t area;lv_obj_get_coords(button,&area);assert_inside_circle(area);}
        lobby_controls.push_back(lobby_hint());assert_controls_separate(lobby_controls);assert_pet_uncovered(pet,lobby_controls);
        save_ppm(output,(mode+"-game-ready.ppm").c_str());pointer_button("捕虫挑战");
        std::vector<lv_obj_t *> game_controls={control("结束")};
        for(auto *button:game_controls){lv_area_t area;lv_obj_get_coords(button,&area);assert_inside_circle(area);assert(area.y1>=365);}
        auto *target=unique_sized(60,48);lv_area_t target_area;lv_obj_get_coords(target,&target_area);assert_inside_circle(target_area);
        pet=assert_pet_layout("running unobstructed game");assert_pet_uncovered(pet,game_controls);
        hunt_hit();pet=frame_object(CH_EAT);assert(pet&&source_stage(asset_source(pet))==stage);
        assert(asset_source(pet)==ch_stages[stage].animations[CH_EAT].frames[0]&&"a successful tap must immediately show an actual capture frame");
        advance(220);pet=frame_object(CH_EAT);assert(pet&&asset_source(pet)!=ch_stages[stage].animations[CH_EAT].frames[0]);
        assert(has("命中 1",true));assert_pet_uncovered(pet,game_controls);assert_incremental_matches_full("visible game capture action");
        save_ppm(output,(mode+"-game-hit.ppm").c_str());click("结束");advance(1400);assert_simple_home();
        assert(saved().state.growth_ms>=initial.state.growth_ms&&integer_values["fresh_baby"]==1);
        advance(2000);click("主菜单");click("图鉴");click("开心");advance(120);
        auto *subtitle=find_any_prefix(lv_screen_active(),"未校时 ·");assert(subtitle);
        assert(lv_obj_has_flag(subtitle,LV_OBJ_FLAG_HIDDEN)==(stage!=CH_STAGE_BABY));
        auto *header_time=find_text(lv_screen_active(),"--:--");assert(header_time&&has("电量未知")&&has("主菜单"));
        assert(frame_object(CH_HAPPY));save_ppm(output,(mode+"-happy.ppm").c_str());
        advance(2400);assert(!lv_obj_has_flag(subtitle,LV_OBJ_FLAG_HIDDEN)&&"the date subtitle must return after the tall action");
        auto *time=find_text(lv_screen_active(),"--:--");assert(time);lv_obj_send_event(time,LV_EVENT_CLICKED,nullptr);advance(100);
        assert(has("天气")&&has("充电"));assert_pet_layout("separate clock layout",true);
    } else if(mode=="fresh-baby") {
        assert(std::strcmp(ch_asset_stage(),"baby")==0&&CH_FRAME_COUNT==246&&CH_STAGE_FRAME_COUNT==82&&CH_ACTIVE_ANIM_COUNT==14);
        assert(ch_animations[CH_GRIP].count==0&&ch_animations[CH_GRIP].frames==nullptr);
        const auto first=saved();const ch::State defaults{};
        assert(!first.state.sick&&!first.state.sleeping&&first.state.health==defaults.health&&first.state.fullness==defaults.fullness);
        assert(first.state.coins==defaults.coins&&first.state.inventory[0]==defaults.inventory[0]&&first.state.meals==0&&first.state.care_total==0);
        assert(first.state.growth_ms==0&&first.state.growth_speed==ch::GrowthSpeed::Normal);
        assert(first.version==5&&blobs["state"].size()==sizeof(Save));
        assert(integer_values["fresh_baby"]==1&&blobs["network-fixture"]==std::vector<uint8_t>({1,2,3}));
        assert_pet_layout("fresh baby startup");assert_simple_home();save_ppm(output,"fresh-baby.ppm");
        click("喂食");click("蟋蟀 ×3");advance(2000);assert(saved().state.meals==1&&saved().state.inventory[0]==2);
        const auto continued=saved();const int writes=nvs_writes;
        assert(continued.state.growth_ms>0);
        start_ui(); // Re-enter startup with the same NVS marker.
        assert(nvs_writes==writes&&saved().state.meals==1&&saved().state.inventory[0]==2);
        assert(saved().version==5&&blobs["state"].size()==sizeof(Save));
        assert(saved().state.growth_ms==continued.state.growth_ms);advance(240);assert_stage(CH_STAGE_BABY,"fresh baby subsequent boot");
    } else if(growth_transition) {
        const int previous=mode=="growth-teen"?CH_STAGE_BABY:CH_STAGE_TEEN,target=previous+1;
        const uint32_t threshold=target==CH_STAGE_TEEN?ch::kGrowthTeenMs:ch::kGrowthAdultMs;
        assert_stage(previous,"before real stage threshold");advance(240);assert_stage(previous,"twenty ms before real stage threshold");
        assert(saved().state.growth_ms==threshold-500);advance(40);
        assert_growth_moment(previous,target);assert(saved().state.growth_ms>=threshold);
        assert(saved().state.growth_speed==ch::GrowthSpeed::Normal);assert(has(target==CH_STAGE_TEEN?"长成少年啦！":"长成成年啦！"));
        save_ppm(output,(mode+"-moment.ppm").c_str());
        for(int i=0;i<10;++i){advance(200);assert_growth_moment(previous,target);}
        click("回到森林");assert_stage(target,"growth milestone returns to the grown form");
        auto *home_info=find_text(lv_screen_active(),target==CH_STAGE_TEEN?"未校时 · 少年":"未校时 · 成年",true);
        assert(home_info&&lv_obj_get_y(home_info)+lv_obj_get_height(home_info)<129&&"stage text must update with the actual pet and clear the adult crest");
        assert_incremental_matches_full("real stage transition");save_ppm(output,(mode+".ppm").c_str());
        growth_page();assert(has(target==CH_STAGE_TEEN?"当前：少年":"当前：成年",true));save_ppm(output,(mode+"-record.ppm").c_str());click("首页");
        click("主菜单");click("图鉴");click_without_advance("抚摸");
        auto *pet=assert_pet_layout("grown immediate pet event");assert(asset_source(pet)==ch_stages[target].animations[CH_PET].frames[0]);check_pet_sequence(target);
        click("喂食");click("蟋蟀 ×3");advance(460);assert_capture_mouth(target);assert_incremental_matches_full("grown tongue capture");save_ppm(output,(mode+"-eat.ppm").c_str());advance(1000);
        assert(saved().state.meals==1&&saved().state.inventory[0]==2);check_stage_sleep(target);save_ppm(output,(mode+"-sleep.ppm").c_str());click("叫醒");
        const auto continued=saved();start_ui();assert(saved().state.growth_ms==continued.state.growth_ms);advance(240);assert_stage(target,"grown subsequent boot");
        assert(integer_values["fresh_baby"]==1&&partial_flush_calls>0&&!has("回到森林")&&"a reboot must not replay an already reached stage");
    } else if(mode=="growth-paused") {
        advance(6000);assert(saved().state.growth_ms==ch::kGrowthTeenMs-500);assert_stage(CH_STAGE_BABY,"sick growth remains paused");
        growth_page();auto *info=find_text(lv_screen_active(),"当前：幼年",true);assert(info&&std::strstr(lv_label_get_text(info),"暂停成长 · 先照顾好它"));
        assert_incremental_matches_full("paused growth record");save_ppm(output,"growth-paused.ppm");
    } else if(mode=="pet" || mode=="pet-partial" || mode=="pet-te-buffer") {
        for(int repetition = 0; repetition < 3; ++repetition) {
            click("主菜单");click("图鉴");
            click_without_advance("抚摸");
            // Do not advance the clock: a refresh can happen before the
            // next pet timer, which used to display a native sprite at (0,0).
            auto *pet = assert_pet_layout("immediate pet event");
            const int stage=source_stage(asset_source(pet));assert(stage>=0);
            assert(asset_source(pet) == ch_stages[stage].animations[CH_PET].frames[0] &&
                   "the first visible pose after petting must already be the first pet frame");
            assert_simple_home();
            assert_incremental_matches_full("immediate pet event");
            check_pet_sequence(stage);
            click_without_advance("主菜单");
            click_without_advance("首页");
            assert_pet_layout("immediate menu return");
            assert_simple_home();
            assert_incremental_matches_full("immediate menu return");
        }
        assert(flush_calls > 0);
        std::fprintf(stderr,"Pet render checks (%s): %u flushes, %u partial areas\n",
                     mode.c_str(),flush_calls,partial_flush_calls);
        if(partial) assert(partial_flush_calls > 0);
        save_ppm(output,te_buffer?"pet-te-buffer.ppm":partial?"pet-partial.ppm":"pet.ppm");
    } else if(mode=="flow") {
        save_ppm(output,"home.ppm");assert_simple_home();
        click("主菜单");assert_main_menu();save_ppm(output,"menu.ppm");
        click("喂食");assert(has("蟋蟀 ×3"));click("蟋蟀 ×3");advance(560);save_ppm(output,"tongue.ppm");advance(1000);
        assert(saved().state.inventory[0]==2 && saved().state.meals==1);
        click("主菜单");click("设置");assert(!has("倾斜校准"));assert(has("自由换场景"));
        click("首页");click("主菜单");click("图鉴");assert(!has("抓紧树枝"));assert(has("今日照顾与徽章"));
        advance(2000);click("走路");advance(2700);click("主菜单");click("玩耍");click("捕虫挑战");assert(has("捕虫挑战")&&has("结束"));
        click("结束");advance(1800);click("睡觉");assert(has("叫醒"));click("叫醒");assert(!saved().state.sleeping);
    } else if(mode=="sick") {
        assert(has("生病了 · 去状态页治疗"));assert(frame_object(CH_SICK));save_ppm(output,"sick.ppm");
        click("玩耍");click("捕虫挑战");assert(has("生病时先治疗，康复后再玩"));assert(!has("结束"));
        click("首页");click("生病了 · 去状态页治疗");assert(has("长时间饥饿 · 需要药水",true));
        click("治疗");assert(saved().state.sick && saved().state.treatment_ms>0 && saved().state.inventory[4]==0);
        save_ppm(output,"recovery.ppm");click("首页");click("睡觉");advance(32000);
        click("叫醒");assert(!saved().state.sick && saved().state.treatment_ms==0 && saved().state.health>=45);
        advance(2000);click("玩耍");click("捕虫挑战");assert(has("结束"));
    } else if(mode=="hungry") {
        assert(has("很饿了 · 快捉只小虫"));assert(frame_object(CH_ANGRY));save_ppm(output,"hungry.ppm");
        click("很饿了 · 快捉只小虫");click("蟋蟀 ×3");advance(2000);
        assert(saved().state.fullness>25 && saved().state.meals==1);assert(!has("很饿了 · 快捉只小虫"));
    } else if(mode=="sleep") {
        assert_simple_home();assert(has("叫醒"));auto *pet=frame_object(CH_SLEEP);assert(pet);
        const int x=lv_obj_get_x(pet),y=lv_obj_get_y(pet);const int scale=lv_image_get_scale_x(pet);
        for(int i=0;i<24;++i){advance(200);pet=frame_object(CH_SLEEP);assert(pet);assert(lv_obj_get_x(pet)==x&&lv_obj_get_y(pet)==y&&lv_image_get_scale_x(pet)==scale);}
        save_ppm(output,"sleep.ppm");
        auto *world=lv_obj_get_parent(pet);int marks=0;
        for(uint32_t i=0;i<lv_obj_get_child_count(world);++i){auto *child=lv_obj_get_child(world,i);if(lv_obj_get_width(child)==21&&lv_obj_get_height(child)==21&&!lv_obj_has_flag(child,LV_OBJ_FLAG_HIDDEN))++marks;}
        assert(marks==4);click("叫醒");assert(!saved().state.sleeping);assert_simple_home();
    } else if(mode=="manualscene") {
        for(const auto *name:{"林间草地","夜间森林","蓝天蘑菇草地","月夜蘑菇草地","蘑菇森林"}){
            open_scenes_from_menu();click(name,true);assert(saved().state.scene_locked&&!saved().state.auto_world);assert_simple_home();
        }
        click("睡觉");open_scenes_from_menu();click("蓝天蘑菇草地",true);
        assert(saved().state.sleeping&&saved().state.scene==ch::Scene::Meadow);
        assert(find_source(lv_screen_active(),ch_backgrounds[3]));click("叫醒");
        assert(saved().state.scene==ch::Scene::Meadow);advance(3000);assert(!frame_object(CH_GRIP));
        save_ppm(output,"manual-scene.ppm");
        click("主菜单");click("设置");click("昼夜天气   关");assert(saved().state.auto_world&&!saved().state.scene_locked);
    } else if(mode=="buttons-sound") {
        assert(ch_hw::sound_enabled&&saved().state.sound&&ch_hw::sound_requests.empty());
        // Actual common LVGL callbacks must cover every menu tile, including
        // pages which have no pet-care action or game reward to play a sound.
        for(const char *name:{"状态","喂食","玩耍","卫生","背包","陪伴","设置","图鉴","关于"}) {
            sound_click("主菜单");sound_click(name);
            if(std::strcmp(name,"喂食")==0)sound_click("返回");
            else sound_click("首页");
            assert_simple_home();
        }
        sound_click("卫生");sound_click("清理便便");assert(saved().state.poop_count==0);
        sound_click("清理便便");assert(has("现在没有便便要清理")); // rejected action still clicks
        sound_click("洗澡");advance(2400);sound_click("首页");
        sound_click("玩耍");sound_click("捕虫挑战");
        auto hit_sounds=ch_hw::sound_requests.size();hunt_hit();assert(ch_hw::sound_requests.size()==hit_sounds+1&&ch_hw::sound_requests.back()==ch_hw::Sound::Tap);
        sound_click("结束");advance(1500);
        assert_simple_home();
        sound_click("主菜单");sound_click("陪伴");assert(!has("相册")&&!has("补拍纪念照"));sound_click("首页");
        auto before=ch_hw::sound_requests.size();pwr();pwr();assert(ch_hw::sound_requests.size()==before+2&&brightness==57);
        before=ch_hw::sound_requests.size();boot();boot();assert(ch_hw::sound_requests.size()==before+2);assert_simple_home();
        // Multiple debounced releases may arrive before the next UI tick. Each
        // accepted short press must be consumed, rather than coalesced to one.
        before=ch_hw::sound_requests.size();ch_hw::status.pwr_presses+=2;advance(120);
        assert(ch_hw::sound_requests.size()==before+2&&brightness==57);
        before=ch_hw::sound_requests.size();ch_hw::status.boot_presses+=2;advance(120);
        assert(ch_hw::sound_requests.size()==before+2);assert_simple_home();
        // Rapid ordinary clicks at the same clock are not subject to a UI
        // sound debounce: they each enqueue one tap through the common path.
        for(int i=0;i<6;++i){sound_click("主菜单",false,false);sound_click("首页",false,false);}
        sound_click("主菜单");sound_click("设置");sound_click("声音   开");
        assert(!saved().state.sound&&!ch_hw::sound_enabled);
        assert(ch_hw::sound_preferences.back()==std::make_pair(false,true)&&"mute click needs one final confirmation after cancelling old feedback");
        before=ch_hw::sound_requests.size();click("首页");pwr();boot();boot();
        click("陪伴");click("首页");
        assert(ch_hw::sound_requests.size()==before&&"mute must silence both buttons and physical keys");
        start_ui();advance(120);assert(!ch_hw::sound_enabled&&!saved().state.sound);
        click("主菜单");click("设置");assert(ch_hw::sound_requests.size()==before);
        sound_click("声音   关");assert(saved().state.sound&&ch_hw::sound_enabled);
        assert(ch_hw::sound_preferences.back()==std::make_pair(true,false)&&"unmute uses one ordinary tap after enabling, without a second mute confirmation");
        sound_click("首页");
        // Restored sound preferences apply at startup; disabled runs never
        // silently fall back to the default-on value after loading a save.
        start_ui();advance(120);assert(ch_hw::sound_enabled&&saved().state.sound);sound_click("主菜单");
    } else if(mode=="keys") {
        assert_simple_home();assert(has("电量 85%")&&brightness==57);const int writes=nvs_writes;
        // Requests remain queued until the UI services them; the latest request
        // wins before the timer applies a command to the panel.
        ch_hw::set_brightness(0);assert(brightness==57);
        ch_hw::set_brightness(44);advance(80);assert(brightness==44);
        ch_hw::set_brightness(57);assert(brightness==44);advance(80);assert(brightness==57);
        pwr();assert(brightness==0&&saved().state.brightness==57&&nvs_writes>writes);
        auto *wake=find_text(lv_screen_active(),"短按侧键唤醒");assert(wake&&!lv_obj_has_flag(lv_obj_get_parent(wake),LV_OBJ_FLAG_HIDDEN));
        pwr();assert(brightness==57&&lv_obj_has_flag(lv_obj_get_parent(wake),LV_OBJ_FLAG_HIDDEN));
        boot();assert_main_menu();assert(has("电量 85%"));
        click("状态");assert(has("照顾与状态")&&has("电量 85%"));
        pwr();assert(brightness==0);boot();assert(brightness==57&&has("照顾与状态"));
        boot();assert(has("主菜单")&&!has("照顾与状态"));
        ch_hw::status.charging=true;advance(600);assert(has("充电 85%"));save_ppm(output,"keys-battery.ppm");
        auto *clock_label=find_text(lv_screen_active(),"12:34");assert(clock_label);lv_obj_send_event(clock_label,LV_EVENT_CLICKED,nullptr);advance(100);
        assert(has("充电 85%")&&has("天气"));boot();assert(has("主菜单"));
        ch_hw::status.battery_valid=false;advance(600);assert(has("电量未知")&&!has("电量 0%"));
        // A timed game can finish while the display is off. Its page rebuild
        // must retain the wake cover, and the care clock/save must keep running.
        click("玩耍");click("捕虫挑战");pwr();assert(brightness==0);
        advance(22000);assert(brightness==0);
        wake=find_text(lv_screen_active(),"短按侧键唤醒");assert(wake&&!lv_obj_has_flag(lv_obj_get_parent(wake),LV_OBJ_FLAG_HIDDEN));
        boot();assert(brightness==57&&has("捕虫挑战"));boot();assert(has("主菜单"));
    } else if(mode=="cleanup") {
        assert(has("有便便了 · 去清理一下"));assert_droppings(2);click("有便便了 · 去清理一下");assert_droppings(2);save_ppm(output,"cleanup.ppm");
        click("清理便便");assert(saved().state.poop_count==0&&saved().state.inventory[5]==2);
        assert_droppings(0);
        assert(saved().state.care_total==1);advance(1400);click("洗澡");assert(saved().state.inventory[5]==1 && saved().state.cleanliness>70);
        click("清理便便");assert(has("现在没有便便要清理"));
    } else if(mode=="daily") {
        care_page();assert(has("喂食一次 · 待完成"));click("首页");click("喂食");click("蟋蟀 ×3");advance(1700);
        click("主菜单");click("卫生");click("清理便便");click("首页");advance(1600);play_one();
        assert(saved().state.daily_mask==7&&saved().state.care_total==12);const int before=saved().state.coins;
        care_page();assert(has("领取 10 金币"));save_ppm(output,"daily.ppm");click("领取 10 金币");
        assert(saved().state.coins==before+10&&saved().state.daily_claimed&&saved().state.daily_claims==1);
        click("今天已领取");assert(saved().state.coins==before+10);
        verified_day(2);advance(5200);assert(saved().state.daily_mask==0&&!saved().state.daily_claimed&&saved().state.care_total==12);
        assert(has("喂食一次 · 待完成"));
    } else if(mode=="migration-v4") {
        const auto data=saved();assert(pet_nvs_writes==1&&"V4 must be upgraded exactly once to a smaller gallery-free V5 save");
        assert_simple_home();assert_stage(CH_STAGE_ADULT,"V4 adult remains adult after gallery removal");
        assert(data.state.health==73&&data.state.fullness==68&&data.state.energy==58&&data.state.mood==61&&data.state.cleanliness==49);
        assert(data.state.coins==234&&data.state.inventory[0]==8&&data.state.meals==17&&data.state.strokes==41);
        assert(data.state.sick&&data.state.sickness_cause==ch::SicknessCause::Dirt&&data.state.treatment_ms==12345);
        assert(data.state.poop_count==2&&data.state.digestion_ms[0]==6543&&data.state.neglect_ms[1]==98765);
        assert(data.state.care_total==55&&data.state.day_key==20261001&&data.state.daily_mask==3&&data.state.daily_claimed&&data.state.daily_claims==4);
        assert(data.state.growth_ms==ch::kGrowthAdultMs&&data.state.growth_speed==ch::GrowthSpeed::Normal);
        assert(data.state.scene==ch::Scene::Meadow&&data.state.awake_scene==ch::Scene::Meadow&&data.state.scene_locked);
        assert(!data.state.auto_life&&!data.state.auto_world&&data.state.idle_minutes==0);
        assert(data.state.affinity==67&&data.state.affection_cooldown_ms==30000&&data.state.companion_ms==23ull*ch::kCompanionDayMs+123456);
        assert(data.state.anniversary_claimed==1&&data.state.encounter_claimed==5&&data.state.encounter_total==9);
        assert(data.version==5&&blobs["state"].size()==sizeof(Save)&&blobs["state"].size()<sizeof(LegacySaveV4));
        const int writes=nvs_writes;start_ui();advance(240);
        assert(nvs_writes==writes&&saved().state.coins==234&&saved().state.affinity==67);
        click("主菜单");assert_main_menu();click("陪伴");assert(has("重温21天")&&!has("相册")&&!has("补拍纪念照"));
    } else if(mode=="migration-v3") {
        const auto data=saved();assert(pet_nvs_writes==1&&"V3 must migrate once, not be mistaken for an invalid enlarged V4 blob");
        assert_simple_home();assert_stage(CH_STAGE_ADULT,"migrated adult remains adult");
        assert(data.state.health==73&&data.state.fullness==68&&data.state.energy==58&&data.state.mood==61&&data.state.cleanliness==49);
        assert(data.state.coins==234&&data.state.inventory[0]==8&&data.state.meals==17&&data.state.strokes==41);
        assert(data.state.sick&&data.state.sickness_cause==ch::SicknessCause::Dirt&&data.state.treatment_ms==12345);
        assert(data.state.poop_count==2&&data.state.digestion_ms[0]==6543&&data.state.neglect_ms[1]==98765);
        assert(data.state.care_total==55&&data.state.day_key==20261001&&data.state.daily_mask==3&&data.state.daily_claimed&&data.state.daily_claims==4);
        assert(data.state.growth_ms==ch::kGrowthAdultMs&&data.state.growth_speed==ch::GrowthSpeed::Normal);
        assert(data.state.scene==ch::Scene::Meadow&&data.state.awake_scene==ch::Scene::Meadow&&data.state.scene_locked);
        assert(!data.state.auto_life&&!data.state.auto_world&&data.state.idle_minutes==0);
        assert(data.state.affinity==0&&data.state.affection_cooldown_ms==0&&data.state.companion_ms==ch::kGrowthAdultMs);
        assert(data.state.anniversary_claimed==0&&data.state.encounter_claimed==0&&data.state.encounter_total==0);
        assert(data.version==5&&blobs["state"].size()==sizeof(Save));
        assert(integer_values["fresh_baby"]==1&&!has("陪伴21天啦！")&&!has("回到森林"));
        const int writes=nvs_writes;start_ui();advance(240);assert(nvs_writes==writes&&saved().state.growth_ms==ch::kGrowthAdultMs);
        click("主菜单");assert_main_menu();click("陪伴");assert(!has("相册")&&!has("补拍纪念照"));
    } else if(mode=="migration-v2") {
        const auto data=saved();assert(pet_nvs_writes==1);
        assert(data.state.health==60&&data.state.fullness==74&&data.state.cleanliness==48&&data.state.coins==123);
        assert(data.state.sick&&data.state.sickness_cause==ch::SicknessCause::Dirt&&data.state.treatment_ms==12345);
        assert(data.state.poop_count==2&&data.state.digestion_ms[0]==6543&&data.state.neglect_ms[1]==98765);
        assert(data.state.care_total==35&&data.state.day_key==20261001&&data.state.daily_mask==3&&data.state.daily_claimed&&data.state.daily_claims==4);
        assert(data.state.growth_ms==0&&data.state.affinity==0&&data.state.companion_ms==0&&data.state.anniversary_claimed==0);
        assert(data.version==5&&blobs["state"].size()==sizeof(Save));
    } else if(mode=="migration") {
        const auto data=saved();assert(pet_nvs_writes==1);assert_simple_home("小卷");
        assert(std::strcmp(data.state.name,"小卷")==0 && "hiding the nickname must not erase it from the saved pet");
        assert(data.state.health==73&&data.state.fullness==62&&data.state.energy==57&&data.state.mood==44&&data.state.cleanliness==51);
        assert(data.state.coins==345&&data.state.inventory[0]==9&&data.state.inventory[4]==5);
        assert(data.state.sleeping&&data.state.sleep_reason==ch::SleepReason::Manual&&data.state.scene==ch::Scene::MeadowNight);
        assert(data.state.awake_scene==ch::Scene::Meadow&&!data.state.auto_life&&!data.state.auto_world&&!data.state.sound);
        assert(data.state.brightness==57&&data.state.idle_minutes==0&&data.state.meals==12&&data.state.strokes==34);
        assert(data.state.scene_locked&&data.state.care_total==0&&data.state.poop_count==0);
        assert(data.version==5&&blobs["state"].size()==sizeof(Save));
        click("主菜单");assert_main_menu();click("陪伴");assert(!has("相册")&&!has("补拍纪念照"));
    } else if(mode=="invalidsave") {
        assert(has("存档不可用，原数据已保留"));click("睡觉");advance(6000);
        assert(nvs_writes==0&&blobs["state"]==original_blob);
        integer_values.erase("fresh_baby");start_ui();advance(240);
        assert(has("存档不可用，原数据已保留")&&blobs["state"]==original_blob&&nvs_writes==0);
        click("睡觉");advance(6000);
        assert(blobs["state"]==original_blob&&nvs_writes==0&&!integer_values.count("fresh_baby")&&"a missing fresh-start marker must never authorize replacing a corrupt save");
    }
    std::printf("Native LVGL checks passed: %s (real mode-specific assertions).\n",mode.c_str());
}
