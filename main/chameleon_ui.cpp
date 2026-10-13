// SPDX-License-Identifier: MIT
// Native LVGL frontend for the original chameleon computer design.
#include "chameleon_ui.h"
#include "chameleon_state.h"
#include "chameleon_games.h"
#include "chameleon_alarm.h"
#include "chameleon_audio.h"
#include "chameleon_forest.h"
#include "chameleon_assets.h"
#include "chameleon_mood_color.h"
#include "chameleon_hardware.h"
#include "chameleon_display.h"
#include "lvgl.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstddef>

namespace {
constexpr uint32_t PAPER=0xf5f2de,INK=0x304c31,SOFT=0xdce8bf,GREEN=0x507746;
enum class Page {Home,Menu,Status,Feed,Game,Bag,Clean,Settings,Catalogue,About,Clock,Weather,Charging,Scenes,Network,Care,Growth,GrowthMoment,Bond,Anniversary,Alarm,Audio,Wishes,Workshop,Calendar};
enum Cmd {Home=1,Menu,Status,Feed,Game,Bag,Clean,Settings,Catalogue,About,Clock,Weather,Charging,Scenes,Network,Pet,Sleep,Wash,EndGame,Shop,Backpack,AutoLife,AutoWorld,Sound,Idle,Provision,RefreshWeather,ClearPoop,Treat,NeedCare,Care,ClaimDaily,Growth,FinishGrowth,Bond,FinishAnniversary,Replay21,Replay30,CollectEncounter,Alarm,AlarmHourUp,AlarmHourDown,AlarmMinuteUp,AlarmMinuteDown,AlarmToggle,AlarmSave,AlarmStop,AlarmSnooze,AlarmCancelSnooze,Audio,EffectsDown,EffectsUp,AlarmVolumeDown,AlarmVolumeUp,AlarmPreview,Wishes,Workshop,Calendar,Materials,Recipes,Forage,HolidayGift,FoodBase=100,BuyBase=120,UseBase=140,SceneBase=160,MotionBase=180,GameSelectBase=240,GameOptionBase=250,HuntHit=260,CraftBase=280,WishActionBase=290,SkipWishBase=300};
ch::Model model;
ch_forest::Model forest;
nvs_handle_t forest_storage=0;
bool forest_storage_ok=false,workshop_recipes=false,forest_game_recorded=true,forest_game_wish_done=false;
uint64_t forest_last_save=0,forest_game_started=0,forest_celebrate_until=0;
lv_obj_t *wish_button=nullptr,*forest_note=nullptr,*wish_goal[2]{},*wish_status[2]{},*wish_actions[2]{},*wish_skips[2]{};
lv_obj_t *material_labels[4]{},*recipe_labels[3]{},*recipe_actions[3]{},*forage_button=nullptr,*holiday_button=nullptr,*calendar_date=nullptr,*calendar_season=nullptr,*calendar_holiday=nullptr;
lv_obj_t *forest_decoration[3]{},*season_particles[6]{},*holiday_particles[8]{};
uint8_t forest_last_season=255;
ch_alarm::Scheduler alarm;
ch_alarm::Config alarm_draft;
nvs_handle_t alarm_storage=0;
bool alarm_storage_ok=false;
char alarm_message[100]{};
lv_obj_t *alarm_hour=nullptr,*alarm_minute=nullptr,*alarm_toggle=nullptr,*alarm_note=nullptr,*alarm_now=nullptr,*alarm_overlay=nullptr,*alarm_cancel_button=nullptr,*alarm_network_button=nullptr;
uint64_t alarm_next_sound=0;
int effects_level=ch_audio::kDefaultEffects,alarm_level=ch_audio::kDefaultAlarm;
nvs_handle_t audio_storage=0;
bool audio_storage_ok=false;
lv_obj_t *effects_value=nullptr,*alarm_value=nullptr,*audio_note=nullptr;
Page page=Page::Home;
ch_hw::Status hw;
ch::Life living;
ch::Scene rendered_scene=ch::Scene::Forest;
bool storage_ok=false,shop=false,screen_dimmed=false,ambient_dimmed=false;
lv_timer_t *ui_timer=nullptr;
constexpr uint32_t kUiActiveMs=40,kUiStandbyMs=1000;
constexpr uint64_t kAmbientDimDelayMs=20000;
nvs_handle_t storage_handle=0;
lv_obj_t *world=nullptr,*background=nullptr,*shade=nullptr,*face=nullptr,*tongue=nullptr,*bug=nullptr,*toast_obj=nullptr,*cover=nullptr;
lv_obj_t *particles[18]{},*stat_labels[5]{},*bars[5]{},*info=nullptr,*time_label=nullptr,*date_label=nullptr,*life_label=nullptr,*game_buttons[3]{},*sleep_label=nullptr;
lv_obj_t *sleep_marks[4]{},*droppings[3]{},*need_label=nullptr,*daily_labels[3]{},*daily_claim_label=nullptr,*badge_label=nullptr,*battery_label=nullptr,*badge_labels[3]{};
lv_obj_t *care_buttons[4]{};
lv_obj_t *growth_forms[2]{},*growth_sparkles[6]{},*growth_confetti[24]{};
lv_obj_t *pet_heart=nullptr;
lv_obj_t *bond_bar=nullptr,*anniversary_cards[2]{},*encounter_button=nullptr,*weather_city_label=nullptr;
uint8_t anniversary_day=0;
uint64_t anniversary_started=0,next_encounter_scan=0,encounter_expires=0;
int active_encounter=-1;
const lv_image_dsc_t *heart_anchor_source=nullptr;
int16_t heart_head_top[CH_FRAME_SIZE]{};
int heart_head_right=0;
constexpr int kPetHeartWidth=18,kPetHeartHeight=16,kPetHeartGap=8,kPetHeartSafeTop=114;
const char *pet_heart_pattern[]={"011000110","133101221","132222221","122222221","012222210","001222100","000121000","000010000"};
bool game_capture=false,game_finish_pending=false,game_return_home=false,growth_pending=false;
uint64_t game_capture_start=0,growth_shown_at=0;
int game_capture_stage=CH_STAGE_BABY,feed_visual_stage=-1,growth_from=CH_STAGE_BABY,growth_to=CH_STAGE_TEEN;
ch::Item game_capture_item=ch::Item::Cricket;
ch_mood::Tone current_tone=ch_mood::Tone::Original;
ch::games::Session game_session;
lv_obj_t *game_hint=nullptr,*game_hunt_hit=nullptr,*game_ball=nullptr,*game_goals[3]{},*game_leaf_art[3]{},*game_leaf_bug[3]{};
int game_seen_hits=0,game_seen_misses=0;
ch::Item game_drawn_insect=ch::Item::ItemCount;
bool ball_dragging=false;
uint8_t ball_reaction_step=0;
float ball_offset_x=0,ball_offset_y=0,game_capture_x=400,game_capture_y=230;
uint64_t game_success_at=0;
uint64_t last_tick=0,last_input=0,last_save=0,toast_until=0,manual_start=0,manual_until=0,last_panel=0;
ch::Pose manual_pose=ch::Pose::Idle;
ch::Pose current_pose=ch::Pose::Idle;
int current_frame=0;
int current_stage=-1;
lv_obj_t *growth_bar=nullptr;
uint8_t previous_needs=0;
uint32_t seen_pwr=0,seen_boot=0;
const char *scene_names[]={"蘑菇森林","林间草地","夜间森林","蓝天蘑菇草地","月夜蘑菇草地"};
const char *pose_names[]={"待机","开心","捕食","睡觉","走路","眨眼","伸懒腰","打招呼","眩晕","生气","生病","洗澡","抚摸","抓紧树枝","避雨"};
const uint32_t stat_colors[]={0xec8794,0xe5ad63,0xcfba62,0x90b66b,0x79b7b0};
const char *stat_names[]={"健康","饱腹","精力","心情","清洁"};
uint64_t now_ms(){return esp_timer_get_time()/1000;}
// A key callback can read a slightly later clock than the enclosing tick.
// Saturate elapsed time so waking across that millisecond cannot look idle.
uint64_t input_idle_ms(uint64_t now){return now>=last_input?now-last_input:0;}
void open(Page next);
void alarm_present();
void alarm_close();
void wake_screen();
void refresh();
void draw_pet(uint64_t now);
ch::Weather world_weather();
void environment_draw(uint64_t now,ch::Weather weather);
void bug_shape(ch::Item type);
void set_label_text(lv_obj_t *object,const char *text);
void game_draw(uint64_t now);
void forest_refresh();
void forest_draw(uint64_t now,ch::Weather weather);
void forest_attention(uint64_t now);
bool forest_save(bool force=false);
void forest_game_finish(uint64_t now);
bool forest_page(){return page==Page::Wishes||page==Page::Workshop||page==Page::Calendar;}
bool game_capture_active(uint64_t now){return game_capture&&now>=game_capture_start&&now-game_capture_start<ch::kFeedDurationMs;}
void say(const char *text){if(forest_page()&&forest_note){set_label_text(forest_note,text);toast_until=now_ms()+4000;return;}if(page==Page::Game&&game_hint){set_label_text(game_hint,text);toast_until=now_ms()+2400;return;}if(toast_obj){lv_label_set_text(toast_obj,text);lv_obj_remove_flag(toast_obj,LV_OBJ_FLAG_HIDDEN);if(page==Page::Home&&need_label)lv_obj_add_flag(lv_obj_get_parent(need_label),LV_OBJ_FLAG_HIDDEN);toast_until=now_ms()+2400;}}
void empty(lv_obj_t *o){lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);lv_obj_set_style_pad_all(o,0,0);lv_obj_set_style_border_width(o,0,0);lv_obj_set_style_shadow_width(o,0,0);}
lv_obj_t *box(lv_obj_t *p,int x,int y,int w,int h,uint32_t color,int radius=0,int opacity=255){auto*o=lv_obj_create(p);empty(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,opacity,0);lv_obj_set_style_radius(o,radius,0);return o;}
void pet_heart_create(){
 pet_heart=box(world,0,0,kPetHeartWidth,kPetHeartHeight,0,0,0);
 const uint32_t colors[]={0,0xc95683,0xf48db2,0xffcadb};
 for(int row=0;row<8;++row)for(int col=0;col<9;){
  const int color=pet_heart_pattern[row][col]-'0';int end=col+1;
  while(end<9&&pet_heart_pattern[row][end]==pet_heart_pattern[row][col])++end;
  if(color)box(pet_heart,col*2,row*2,(end-col)*2,2,colors[color]);
  col=end;
 }
 lv_obj_add_flag(pet_heart,LV_OBJ_FLAG_HIDDEN);heart_anchor_source=nullptr;
}
void pet_heart_draw(const lv_image_dsc_t *source,int x,int y,int size,uint64_t now){
 if(!pet_heart)return;
 const auto &header=source->header;
 if(header.cf!=LV_COLOR_FORMAT_RGB565A8||header.w!=CH_FRAME_SIZE||header.h!=CH_FRAME_SIZE||!source->data){lv_obj_add_flag(pet_heart,LV_OBJ_FLAG_HIDDEN);return;}
 if(heart_anchor_source!=source){
  // The unmodified source alpha, rather than the image canvas or a scaled
  // bounding box, follows the forehead in all six actual petting drawings.
  const auto *alpha=source->data+static_cast<size_t>(header.stride)*header.h;
  const unsigned alpha_stride=header.stride/2;heart_head_right=header.w/2;
  for(unsigned col=0;col<header.w;++col){
   heart_head_top[col]=-1;
   if(col<header.w/2)continue;
   for(unsigned row=0;row<header.h;++row)if(alpha[row*alpha_stride+col]){heart_head_top[col]=row;heart_head_right=col;break;}
  }
  heart_anchor_source=source;
 }
 const int half_source=(kPetHeartWidth*CH_FRAME_SIZE+size*2-1)/(size*2);
 const int first_center=(CH_FRAME_SIZE*85+50)/100;
 int heart_x=0,heart_y=0;bool found=false;
 for(int center=first_center;center<=std::min(CH_FRAME_SIZE-1,heart_head_right+half_source);++center){
  int top=CH_FRAME_SIZE;
  for(int col=std::max(CH_FRAME_SIZE/2,center-half_source);col<=std::min(CH_FRAME_SIZE-1,center+half_source);++col)if(heart_head_top[col]>=0)top=std::min(top,static_cast<int>(heart_head_top[col]));
  if(top==CH_FRAME_SIZE)continue;
  const int forehead_y=y+static_cast<int>(std::lround(static_cast<float>(size)*top/static_cast<float>(CH_FRAME_SIZE)));
  const int lowest_top=forehead_y-kPetHeartHeight-kPetHeartGap-2;
  if(lowest_top<kPetHeartSafeTop)continue;
  // Shift along the forehead if the tall crest would put a heart in the
  // clock strip. The eight-pixel clear gap never covers the eye or crest.
  heart_x=x+static_cast<int>(std::lround(static_cast<float>(size)*center/static_cast<float>(CH_FRAME_SIZE)))-kPetHeartWidth/2;
  heart_y=forehead_y-kPetHeartHeight-kPetHeartGap;found=true;break;
 }
 if(!found){lv_obj_add_flag(pet_heart,LV_OBJ_FLAG_HIDDEN);return;}
 const int drift=std::abs(static_cast<int>((now/180)%4)-2);
 lv_obj_set_pos(pet_heart,heart_x,heart_y-drift);lv_obj_remove_flag(pet_heart,LV_OBJ_FLAG_HIDDEN);
}
// LVGL invalidates a label even when set_text receives the same string. Keep
// stable clock / status panels out of the redraw queue and display mutex.
void set_label_text(lv_obj_t *object,const char *text){
 if(object&&text&&std::strcmp(lv_label_get_text(object),text))lv_label_set_text(object,text);
}
void set_label_text_fmt(lv_obj_t *object,const char *format,...){
 if(!object)return;
 char buffer[384];va_list args;va_start(args,format);
 const int length=std::vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);
 if(length<0)return;
 if(static_cast<size_t>(length)<sizeof(buffer)){set_label_text(object,buffer);return;}
 // Preserve full messages if a future network/status string grows; do not
 // turn an energy optimization into silent text truncation.
 auto *full=static_cast<char *>(lv_malloc(static_cast<size_t>(length)+1));
 if(!full)return;
 va_start(args,format);std::vsnprintf(full,static_cast<size_t>(length)+1,format,args);va_end(args);
 set_label_text(object,full);lv_free(full);
}
lv_obj_t *label(lv_obj_t*p,const char*text,int x,int y,int w,uint32_t color=INK,const lv_font_t*f=nullptr){auto*o=lv_label_create(p);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);lv_obj_set_style_text_font(o,f?f:&chameleon_font_18,0);lv_obj_set_style_text_color(o,lv_color_hex(color),0);lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);lv_label_set_text(o,text);return o;}
void icon(lv_obj_t*p,int kind,int x,int y,int scale=3){
 const char *patterns[]={"0110110/1111111/1111111/0111110/0011100/0001000","0000000/0011100/0111110/1111111/0111110/1111111","0001100/0011000/0110000/1111111/0001100/0011000/0110000","0111110/1100011/1010101/1000001/1011101/1100011/0111110","0001000/0011100/0111110/1111111/1111111/0111110/0011100","0011100/0100010/1111111/1101011/1101011/1111111/0111110","0011100/1111111/1100011/1011101/1011101/1100011/1111111","0101010/0011100/1100011/0110110/1100011/0011100/0101010","1110111/1010101/1010101/1010101/1110111/1111111","0011100/0111110/0001000/0011000/0001000/0001000/0011100","0111110/1110011/1101011/1011101/1101011/1110011/0111110","0001110/0011000/0110000/0110000/0110001/0011111/0001110"};
 const uint32_t colors[]={0xec8794,0xce9454,0xd8b956,0x739a4f,0x79b7b0,0xb88655,0x839db2,0x739153,0x6c9158,0x8e8eb5,0xdb9f5a,0x74929e};
 const char*s=patterns[kind%12];int dx=0,dy=0;for(;*s;++s){if(*s=='/'){dx=0;++dy;continue;}if(*s=='1')box(p,x+dx*scale,y+dy*scale,scale,scale,colors[kind%12]);++dx;}
}
uint32_t crc32(const void*data,size_t size){uint32_t c=0xffffffff;const auto*p=static_cast<const uint8_t*>(data);while(size--){c^=*p++;for(int i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320:0);}return ~c;}
// Version 1 was a CRC-protected raw State. Keep its exact layout independent
// from today's State, so adding care fields cannot silently reset old pets.
struct LegacyStateV1 {
 int health=82,fullness=70,energy=65,mood=90,cleanliness=80;
 int coins=30;
 int inventory[ch::kItemCount]={3,3,2,4,1,2,1};
 bool sleeping=false;
 ch::SleepReason sleep_reason=ch::SleepReason::None;
 ch::Scene scene=ch::Scene::Forest,awake_scene=ch::Scene::Forest;
 bool auto_life=true,auto_world=true,sound=true;
 int brightness=80,idle_minutes=1;
 char name[40]="卷卷";
 uint32_t meals=0,strokes=0;
};
struct SaveV1 {uint32_t magic,version;LegacyStateV1 state;uint32_t count,serial,ids[20],crc;};
// Freeze the complete shipped v2 prefix before adding growth fields.
struct LegacyStateV2 {
 int health=82,fullness=70,energy=65,mood=90,cleanliness=80,coins=30;
 int inventory[ch::kItemCount]={3,3,2,4,1,2,1};
 bool sleeping=false;ch::SleepReason sleep_reason=ch::SleepReason::None;
 ch::Scene scene=ch::Scene::Forest,awake_scene=ch::Scene::Forest;
 bool auto_life=true,auto_world=true,sound=true;int brightness=80,idle_minutes=1;
 char name[40]="卷卷";uint32_t meals=0,strokes=0;
 bool scene_locked=false,sick=false;ch::SicknessCause sickness_cause=ch::SicknessCause::None;
 uint32_t treatment_ms=0;uint8_t poop_count=0;
 uint32_t digestion_ms[3]{},neglect_ms[3]{},care_total=0,day_key=0;
 uint8_t daily_mask=0;bool daily_claimed=false;uint32_t daily_claims=0;
};
struct SaveV2 {uint32_t magic,version;LegacyStateV2 state;uint32_t count,serial,ids[20],crc;};
struct LegacyStateV3 {LegacyStateV2 care;uint32_t growth_ms=0;ch::GrowthSpeed growth_speed=ch::GrowthSpeed::Normal;uint8_t reserved[3]{};};
struct SaveV3 {uint32_t magic,version;LegacyStateV3 state;uint32_t count,serial,ids[20],crc;};
struct SaveV4 {uint32_t magic,version;ch::State state;uint32_t count,serial,ids[20],crc;};
// V5 stores only pet care. Old image indexes are decoded solely for migration.
struct Save {uint32_t magic,version;ch::State state;uint32_t crc;};
static_assert(sizeof(ch::State)==216&&sizeof(SaveV4)==320&&offsetof(SaveV4,crc)==312,"Shipped V4 ABI must remain readable");
static_assert(sizeof(Save)==232&&offsetof(Save,crc)==224,"V5 pet-only ABI");
static_assert(sizeof(LegacyStateV1)==116,"Legacy ESP32 save layout must stay unchanged");
static_assert(offsetof(SaveV1,crc)==212,"Legacy album and CRC layout must stay unchanged");
static_assert(offsetof(ch::State,growth_ms)==sizeof(LegacyStateV2),"v2 care prefix must remain unchanged");
static_assert(sizeof(LegacyStateV3)==176&&sizeof(SaveV3)==276&&offsetof(SaveV3,crc)==272,"Shipped v3 pet and album ABI must stay unchanged");
static_assert(offsetof(ch::State,affinity)==sizeof(LegacyStateV3),"v4 additions must follow the entire shipped v3 prefix");
ch::State migrate_v3(const LegacyStateV3& old){ch::State next;std::memcpy(static_cast<void*>(&next),&old,sizeof(old));next.companion_ms=old.growth_ms;return next;}
ch::State migrate_v2(const LegacyStateV2& old){ch::State next;std::memcpy(static_cast<void*>(&next),&old,sizeof(old));return next;}
ch::State migrate_v1(const LegacyStateV1& old) {
 ch::State next;
 next.health=old.health;next.fullness=old.fullness;next.energy=old.energy;
 next.mood=old.mood;next.cleanliness=old.cleanliness;next.coins=old.coins;
 std::memcpy(next.inventory,old.inventory,sizeof(old.inventory));
 next.sleeping=old.sleeping;next.sleep_reason=old.sleep_reason;
 next.scene=old.scene;next.awake_scene=old.awake_scene;
 next.auto_life=old.auto_life;next.auto_world=old.auto_world;next.sound=old.sound;
 next.brightness=old.brightness;next.idle_minutes=old.idle_minutes;
 std::memcpy(next.name,old.name,sizeof(old.name));
 next.meals=old.meals;next.strokes=old.strokes;
 next.scene_locked=!old.auto_world;
 // Older firmware only represented illness by health. Preserve that need.
 if(old.health<40){next.sick=true;next.sickness_cause=ch::SicknessCause::Weakness;}
 return next;
}
bool save(bool force=false){
 if(!storage_ok)return false;
 const uint32_t interval=screen_dimmed?30000:5000;
 if(!force&&(!model.dirty()||now_ms()-last_save<interval))return true;
 Save data{};data.magic=0x43484d31;data.version=5;data.state=model.state;
 data.state.retired_photo_receipt=0;std::memset(data.state.retired_photo_ids,0,sizeof(data.state.retired_photo_ids));
 data.crc=crc32(&data,offsetof(Save,crc));
 esp_err_t e=nvs_set_blob(storage_handle,"state",&data,sizeof(data));
 if(e==ESP_OK)e=nvs_commit(storage_handle);
 if(e!=ESP_OK){storage_ok=false;ESP_LOGE("chameleon","NVS save: %s",esp_err_to_name(e));say("存档失败，请检查");}
 else model.clear_dirty();
 last_save=now_ms();
 return e==ESP_OK;
}
struct AlarmRecord {uint32_t magic=0x43484131,version=1;ch_alarm::Config config;ch_alarm::Receipt receipt;uint32_t crc=0;};
static_assert(sizeof(AlarmRecord)==20&&offsetof(AlarmRecord,crc)==16,"Alarm NVS ABI");
bool alarm_save(const ch_alarm::Config &config){
 if(!alarm_storage_ok)return false;
 AlarmRecord record{};record.config=config;record.receipt=alarm.receipt();record.crc=crc32(&record,offsetof(AlarmRecord,crc));
 esp_err_t error=nvs_set_blob(alarm_storage,"alarm",&record,sizeof(record));if(error==ESP_OK)error=nvs_commit(alarm_storage);
 if(error!=ESP_OK){alarm_storage_ok=false;ESP_LOGE("chameleon","Alarm save failed: %s",esp_err_to_name(error));}
 return error==ESP_OK;
}
void alarm_load(bool ready){
 alarm=ch_alarm::Scheduler{};alarm_message[0]=0;alarm_storage_ok=ready&&nvs_open("ch_alarm",NVS_READWRITE,&alarm_storage)==ESP_OK;
 if(!alarm_storage_ok)return;
 size_t size=0;esp_err_t error=nvs_get_blob(alarm_storage,"alarm",nullptr,&size);
 if(error==ESP_ERR_NVS_NOT_FOUND)return;
 AlarmRecord record{};
 if(error==ESP_OK&&size==sizeof(record)){error=nvs_get_blob(alarm_storage,"alarm",&record,&size);
  if(error==ESP_OK&&record.magic==0x43484131&&record.version==1&&record.crc==crc32(&record,offsetof(AlarmRecord,crc))&&ch_alarm::Scheduler::valid_config(record.config)&&alarm.restore_receipt(record.receipt)){alarm.configure(record.config);return;}
 }
 alarm_storage_ok=false;std::snprintf(alarm_message,sizeof(alarm_message),"闹钟数据不可用，原数据已保留");ESP_LOGE("chameleon","Invalid alarm retained");
}
struct AudioRecord {
 uint32_t magic=0x43484155,version=1;
 uint8_t effects=ch_audio::kDefaultEffects,alarm=ch_audio::kDefaultAlarm;
 uint16_t reserved=0;
 uint32_t crc=0;
};
static_assert(sizeof(AudioRecord)==16&&offsetof(AudioRecord,crc)==12,"Independent audio NVS ABI");
void audio_load(bool ready){
 effects_level=ch_audio::kDefaultEffects;alarm_level=ch_audio::kDefaultAlarm;
 audio_storage_ok=ready&&nvs_open("ch_audio",NVS_READWRITE,&audio_storage)==ESP_OK;
 if(audio_storage_ok){
  size_t size=0;esp_err_t error=nvs_get_blob(audio_storage,"levels",nullptr,&size);
  if(error!=ESP_ERR_NVS_NOT_FOUND){
   AudioRecord record{};
   if(error==ESP_OK&&size==sizeof(record))error=nvs_get_blob(audio_storage,"levels",&record,&size);
   else error=ESP_FAIL;
   if(error==ESP_OK&&record.magic==0x43484155&&record.version==1&&record.reserved==0&&record.effects<=100&&record.alarm<=100&&record.crc==crc32(&record,offsetof(AudioRecord,crc))){effects_level=record.effects;alarm_level=record.alarm;}
   else {audio_storage_ok=false;ESP_LOGE("chameleon","Invalid audio settings retained");}
  }
 }
 ch_hw::set_sound_volumes(effects_level,alarm_level);
}
bool audio_save(){
 if(!audio_storage_ok)return false;
 AudioRecord record{};record.effects=effects_level;record.alarm=alarm_level;record.crc=crc32(&record,offsetof(AudioRecord,crc));
 esp_err_t error=nvs_set_blob(audio_storage,"levels",&record,sizeof(record));if(error==ESP_OK)error=nvs_commit(audio_storage);
 if(error!=ESP_OK){audio_storage_ok=false;ESP_LOGE("chameleon","Audio save failed: %s",esp_err_to_name(error));}
 return error==ESP_OK;
}
struct ForestRecord {uint32_t magic=0x43484631,version=1;ch_forest::State state;uint32_t crc=0;};
static_assert(sizeof(ForestRecord)==64&&offsetof(ForestRecord,crc)==60,"Independent forest NVS ABI");
bool forest_save(bool force){
 if(!forest_storage_ok)return false;
 if(!force&&(!forest.dirty()||now_ms()-forest_last_save<(screen_dimmed?30000u:5000u)))return false;
 ForestRecord record{};record.state=forest.state;record.crc=crc32(&record,offsetof(ForestRecord,crc));
 esp_err_t error=nvs_set_blob(forest_storage,"forest",&record,sizeof(record));
 if(error==ESP_OK)error=nvs_commit(forest_storage);
 if(error!=ESP_OK){forest_storage_ok=false;ESP_LOGE("chameleon","Forest save failed: %s",esp_err_to_name(error));say("森林记录保存失败");}
 else forest.clear_dirty();
 forest_last_save=now_ms();return error==ESP_OK;
}
void forest_load(bool ready){
 forest.initialize(model.state.encounter_total,model.state.meals,model.state.strokes);
 forest_last_save=0;forest_celebrate_until=0;workshop_recipes=false;
 forest_storage_ok=ready&&storage_ok&&nvs_open("ch_forest",NVS_READWRITE,&forest_storage)==ESP_OK;
 if(!forest_storage_ok)return;
 size_t size=0;esp_err_t error=nvs_get_blob(forest_storage,"forest",nullptr,&size);
 if(error==ESP_ERR_NVS_NOT_FOUND){forest_save(true);return;}
 ForestRecord record{};
 if(error==ESP_OK&&size==sizeof(record))error=nvs_get_blob(forest_storage,"forest",&record,&size);else error=ESP_FAIL;
 if(error!=ESP_OK||record.magic!=0x43484631||record.version!=1||record.crc!=crc32(&record,offsetof(ForestRecord,crc))||!forest.restore(record.state)){
  forest_storage_ok=false;ESP_LOGE("chameleon","Invalid forest record retained");return;
 }
 // Pet encounters are saved first. Recover a receipt missed during power
 // loss, without awarding old encounters on first installation.
 if(forest.sync_encounters(model.state.encounter_total))forest_save(true);
}
void forest_game_finish(uint64_t now){
 if(forest_game_recorded)return;
 forest_game_recorded=true;
 const auto previous=forest.state;
 if(forest_storage_ok&&forest.game(static_cast<uint8_t>(game_session.kind()),model.game_hits(),now>=forest_game_started&&now-forest_game_started>=ch::kGameDurationMs)){
  if(forest_save(true)){forest_game_wish_done=true;say("愿望完成 · 收获森林材料");}
  else forest.restore(previous);
 }
}
constexpr uint32_t kFreshBabyStart=1;
void fresh_baby_start(){
 // The user-requested baby start runs once; network settings are separate.
 model.reset();model.mark_dirty();save(true);
 if(!storage_ok)return;
 esp_err_t e=nvs_set_u32(storage_handle,"fresh_baby",kFreshBabyStart);
 if(e==ESP_OK)e=nvs_commit(storage_handle);
 if(e!=ESP_OK){storage_ok=false;ESP_LOGE("chameleon","Fresh-start marker failed: %s",esp_err_to_name(e));}
 else ESP_LOGI("chameleon","Fresh baby pet saved; previous care state reset");
}
void load(bool ready){
 storage_ok=ready&&nvs_open("chameleon",NVS_READWRITE,&storage_handle)==ESP_OK;
 if(!storage_ok)return;
 uint32_t start_marker=0;
 const esp_err_t marker_error=std::strcmp(ch_asset_stage(),"baby")==0?nvs_get_u32(storage_handle,"fresh_baby",&start_marker):ESP_OK;
 if(marker_error!=ESP_OK&&marker_error!=ESP_ERR_NVS_NOT_FOUND){storage_ok=false;ESP_LOGE("chameleon","Fresh-start marker read failed; save retained");return;}
 const bool fresh=std::strcmp(ch_asset_stage(),"baby")==0&&(marker_error==ESP_ERR_NVS_NOT_FOUND||start_marker!=kFreshBabyStart);
 size_t size=0;esp_err_t e=nvs_get_blob(storage_handle,"state",nullptr,&size);
 if(e==ESP_ERR_NVS_NOT_FOUND){if(fresh)fresh_baby_start();return;}
 if(e!=ESP_OK){storage_ok=false;ESP_LOGE("chameleon","Save read failed; original retained");return;}
 if(size==sizeof(SaveV1)){
  SaveV1 old{};e=nvs_get_blob(storage_handle,"state",&old,&size);
  if(e==ESP_OK&&old.magic==0x43484d31&&old.version==1&&old.crc==crc32(&old,offsetof(SaveV1,crc))&&old.count<=20){
   model.restore(migrate_v1(old.state));model.mark_dirty();
   if(fresh){fresh_baby_start();return;}
   // NVS commit replaces the validated entry atomically, without erasing NVS.
   save(true);ESP_LOGI("chameleon","Migrated pet save v1 to v5");return;
  }
 } else if(size==sizeof(SaveV2)){
  SaveV2 old{};e=nvs_get_blob(storage_handle,"state",&old,&size);
  if(e==ESP_OK&&old.magic==0x43484d31&&old.version==2&&old.crc==crc32(&old,offsetof(SaveV2,crc))&&old.count<=20){
   model.restore(migrate_v2(old.state));model.mark_dirty();
   if(fresh)fresh_baby_start();else save(true);
   ESP_LOGI("chameleon","Migrated pet save v2 to v5");return;
  }
 } else if(size==sizeof(SaveV3)){
  SaveV3 old{};e=nvs_get_blob(storage_handle,"state",&old,&size);
  if(e==ESP_OK&&old.magic==0x43484d31&&old.version==3&&old.crc==crc32(&old,offsetof(SaveV3,crc))&&old.count<=20){
   model.restore(migrate_v3(old.state));model.mark_dirty();
   if(fresh)fresh_baby_start();else save(true);
   ESP_LOGI("chameleon","Migrated pet save v3 to v5");return;
  }
 } else if(size==sizeof(SaveV4)){
  SaveV4 data{};e=nvs_get_blob(storage_handle,"state",&data,&size);
  if(e==ESP_OK&&data.magic==0x43484d31&&data.version==4&&data.crc==crc32(&data,offsetof(SaveV4,crc))&&data.count<=20){
   model.restore(data.state);model.mark_dirty();if(fresh)fresh_baby_start();else save(true);
   ESP_LOGI("chameleon","Migrated pet save v4 to v5");return;
  }
 } else if(size==sizeof(Save)){
  Save data{};e=nvs_get_blob(storage_handle,"state",&data,&size);
  if(e==ESP_OK&&data.magic==0x43484d31&&data.version==5&&data.crc==crc32(&data,offsetof(Save,crc))){
   model.restore(data.state);if(fresh)fresh_baby_start();return;
  }
 }
 // A missing first-start marker is not permission to overwrite a damaged
 // save. Keep the original blob for later recovery.
 storage_ok=false;ESP_LOGE("chameleon","Unknown or invalid save retained unchanged");
}
lv_obj_t *anniversary_plaque(uint8_t day){
 auto *plaque=box(world,113,322,240,50,0xf5f2de,0,245);
 lv_obj_set_style_border_width(plaque,2,0);lv_obj_set_style_border_color(plaque,lv_color_hex(GREEN),0);
 char text[64];std::snprintf(text,sizeof(text),"陪伴 %u 天",static_cast<unsigned>(day));label(plaque,text,2,3,232,GREEN);
 if(hw.time_valid)std::snprintf(text,sizeof(text),"%d年%d月%d日",hw.year,hw.month,hw.day);
 else std::snprintf(text,sizeof(text),"一起长大的森林回忆");
 label(plaque,text,2,25,232);return plaque;
}
void animate(ch::Pose pose,uint32_t duration=1600){manual_pose=pose;manual_start=now_ms();manual_until=manual_start+duration;}
void wake_screen(){
 last_input=now_ms();model.life_interaction(last_input);
 const bool restore_brightness=screen_dimmed||ambient_dimmed;
 if(screen_dimmed){
  screen_dimmed=false;
  chameleon_display_set_screen_off(false);
  if(ui_timer){lv_timer_set_period(ui_timer,kUiActiveMs);lv_timer_ready(ui_timer);}
  ch_hw::request_status_refresh();
 }
 ambient_dimmed=false;
 // Queue brightness after arranging the resumed draw; tick applies it last.
 if(restore_brightness)ch_hw::set_brightness(model.state.brightness);
 if(cover)lv_obj_add_flag(cover,LV_OBJ_FLAG_HIDDEN);
}
void touch(){if(!screen_dimmed)wake_screen();}
void touch_activity_cb(lv_event_t *event){
 if(screen_dimmed){lv_indev_wait_release(static_cast<lv_indev_t *>(lv_event_get_target(event)));return;}
 touch();
}
void bind_touch_activity(){
 for(auto *input=lv_indev_get_next(nullptr);input;input=lv_indev_get_next(input)){
  if(lv_indev_get_type(input)!=LV_INDEV_TYPE_POINTER)continue;
  lv_indev_remove_event_cb_with_user_data(input,touch_activity_cb,nullptr);
  lv_indev_add_event_cb(input,touch_activity_cb,LV_EVENT_PRESSED,nullptr);
 }
}
void command(int cmd);
bool button_command_active=false;
void sound(ch_hw::Sound value){if(model.state.sound)ch_hw::play_sound(value);}
void action_cb(lv_event_t*e){
 if(screen_dimmed)return;
 touch();const bool was_sound=model.state.sound;
 const int cmd=static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
 const bool audio_change=cmd==EffectsDown||cmd==EffectsUp||cmd==AlarmVolumeDown||cmd==AlarmVolumeUp;
 if(was_sound&&cmd!=Sound&&cmd!=AlarmPreview&&!audio_change)sound(ch_hw::Sound::Tap);
 // Button actions receive one common tap; feeding completion has its own
 // later sound from tick(), outside this input event.
 button_command_active=true;
 command(cmd);
 button_command_active=false;
 if(audio_change&&was_sound)sound(ch_hw::Sound::Tap);
 if(!was_sound&&model.state.sound)sound(ch_hw::Sound::Tap);
}
lv_obj_t *button(lv_obj_t*p,const char*text,int x,int y,int w,int h,int cmd,uint32_t color=SOFT){auto*o=lv_button_create(p);empty(o);lv_obj_add_flag(o,LV_OBJ_FLAG_CLICKABLE);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_set_ext_click_area(o,6);lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_radius(o,12,0);auto*t=label(o,text,0,0,w,color==GREEN?0xfffee3:INK);lv_obj_center(t);lv_obj_add_event_cb(o,action_cb,LV_EVENT_CLICKED,reinterpret_cast<void*>(static_cast<intptr_t>(cmd)));return o;}
lv_obj_t*btn(const char*t,int x,int y,int w,int h,int cmd,uint32_t color=SOFT){return button(lv_screen_active(),t,x,y,w,h,cmd,color);}
void title(const char*text){btn("‹",83,48,35,35,Menu);label(lv_screen_active(),text,134,53,198);}
void bottom(){btn("首页",184,409,98,32,Home);}
void forest_pixel(lv_obj_t *parent,const char *pattern,int scale,uint32_t dark,uint32_t light,uint32_t accent){
 const uint32_t colors[]={0,dark,light,accent};int x=0,y=0;
 for(const char *p=pattern;*p;++p){if(*p=='/'){x=0;++y;continue;}if(*p>='1'&&*p<='3')box(parent,x*scale,y*scale,scale,scale,colors[*p-'0']);++x;}
}
void forest_art_create(){
 forest_decoration[0]=box(world,83,225,36,36,0,0,0);
 forest_pixel(forest_decoration[0],"100000000001/010000000010/001111111100/002200220022/012210122122/012210122122/002100021021/003000030030",3,0x385c2f,0x8cc750,0xf4a8be);
 forest_decoration[1]=box(world,339,314,30,30,0,0,0);
 forest_pixel(forest_decoration[1],"0001111000/0012332100/0122322210/1222222221/1111111111/0001331000/0001331000/0001331000/0011111100",3,0x5c4738,0xe9b35c,0xffebad);
 forest_decoration[2]=box(world,335,147,24,33,0,0,0);
 forest_pixel(forest_decoration[2],"00011000/00122100/01111110/10011001/10011001/10011001/20011002/32011023/23011032/00022000/00033000",3,0x637c40,0xea88ae,0xffc9dd);
 for(auto *art:forest_decoration)lv_obj_add_flag(art,LV_OBJ_FLAG_HIDDEN);
 for(auto &particle:season_particles){particle=box(world,0,0,6,6,0,0,0);box(particle,2,0,4,2,0xffffff);box(particle,0,2,4,4,0xffffff);lv_obj_add_flag(particle,LV_OBJ_FLAG_HIDDEN);}
 const uint32_t colors[]={0xed6b9b,0xf0c55b,0x72cf96,0x6eb4e6,0xb896e2,0xf0aa69,0xf5e589,0x9cdab9};
 for(int i=0;i<8;++i){holiday_particles[i]=box(world,0,0,i%2?3:6,i%2?8:3,colors[i]);lv_obj_add_flag(holiday_particles[i],LV_OBJ_FLAG_HIDDEN);}
 forest_last_season=255;
}
void world_create(){world=box(lv_screen_active(),0,0,466,466,0xa6c68c);background=lv_image_create(world);lv_image_set_src(background,ch_backgrounds[static_cast<int>(rendered_scene)]);lv_image_set_pivot(background,0,0);lv_image_set_scale(background,256);lv_image_set_antialias(background,false);lv_obj_set_pos(background,0,0);shade=box(world,0,0,466,466,0x294f68,0,0);for(auto &p:particles)p=box(world,0,0,3,7,0xe0f1db,0,0);forest_art_create();face=lv_image_create(world);lv_obj_add_flag(face,LV_OBJ_FLAG_HIDDEN);current_frame=-1;current_stage=-1;lv_image_set_src(face,ch_animations[CH_IDLE].frames[0]);lv_image_set_pivot(face,0,0);lv_image_set_antialias(face,false);lv_obj_remove_flag(face,LV_OBJ_FLAG_CLICKABLE);tongue=lv_line_create(world);lv_obj_set_style_line_color(tongue,lv_color_hex(0xdf8797),0);lv_obj_set_style_line_width(tongue,4,0);lv_obj_set_style_line_rounded(tongue,false,0);lv_obj_add_flag(tongue,LV_OBJ_FLAG_HIDDEN);bug=box(world,0,0,23,17,0,0,0);if(model.feeding())bug_shape(model.feed_item());for(int i=0;i<4;++i){
 sleep_marks[i]=box(world,0,0,21,21,0,0,0);
 box(sleep_marks[i],0,0,18,3,0xf9ffe1);box(sleep_marks[i],12,3,3,3,0xf9ffe1);
 box(sleep_marks[i],9,6,3,3,0xf9ffe1);box(sleep_marks[i],6,9,3,3,0xf9ffe1);
 box(sleep_marks[i],3,12,3,3,0xf9ffe1);box(sleep_marks[i],0,15,18,3,0xf9ffe1);
 lv_obj_add_flag(sleep_marks[i],LV_OBJ_FLAG_HIDDEN);
 }
 pet_heart_create();
 for(int i=0;i<3;++i){
 droppings[i]=lv_image_create(world);lv_image_set_src(droppings[i],ch_poop);
 lv_image_set_pivot(droppings[i],0,0);lv_image_set_antialias(droppings[i],false);
 lv_image_set_scale(droppings[i],256);
 lv_obj_remove_flag(droppings[i],LV_OBJ_FLAG_CLICKABLE);
 lv_obj_add_flag(droppings[i],LV_OBJ_FLAG_HIDDEN);
 }
}
void insect_shape(lv_obj_t *target,ch::Item type){lv_obj_clean(target);if(type==ch::Item::Mealworm){for(int i=0;i<6;++i)box(target,i*3,6+(i%2),4,6,i%2?0xd9ae66:0xefc782);box(target,17,5,5,7,0x80502f);}else{bool roach=type==ch::Item::Cockroach,fly=type==ch::Item::Fly;box(target,4,5,roach?14:11,8,roach?0x9b673d:0x334f37);box(target,15,5,5,5,fly?0xc87955:0x4e5e32);for(int i=0;i<3;++i){box(target,3+i*5,2,2,4,0x3a4b35);box(target,2+i*5,12,2,4,0x3a4b35);}if(fly){box(target,5,0,7,5,0xe5f1e4);box(target,11,1,6,5,0xbad8dc);}}}
void bug_shape(ch::Item type){insect_shape(bug,type);}
void reset_screen(Page next){
 effects_value=alarm_value=audio_note=nullptr;
 wish_button=forest_note=forage_button=holiday_button=calendar_date=calendar_season=calendar_holiday=nullptr;
 for(int i=0;i<2;++i)wish_goal[i]=wish_status[i]=wish_actions[i]=wish_skips[i]=nullptr;
 for(auto &v:material_labels)v=nullptr;
 for(int i=0;i<3;++i)recipe_labels[i]=recipe_actions[i]=forest_decoration[i]=nullptr;
 for(auto &v:season_particles)v=nullptr;
 for(auto &v:holiday_particles)v=nullptr;
 page=next;alarm_hour=alarm_minute=alarm_toggle=alarm_note=alarm_now=alarm_cancel_button=alarm_network_button=nullptr;bond_bar=encounter_button=weather_city_label=nullptr;
 game_hint=game_hunt_hit=game_ball=nullptr;ball_dragging=false;game_drawn_insect=ch::Item::ItemCount;
 for(int i=0;i<3;++i){game_goals[i]=game_leaf_art[i]=game_leaf_bug[i]=nullptr;}
 for(auto &card:anniversary_cards){card=nullptr;}
 active_encounter=-1;growth_bar=nullptr;pet_heart=nullptr;heart_anchor_source=nullptr;
 world=background=shade=face=tongue=bug=toast_obj=cover=info=time_label=date_label=life_label=sleep_label=need_label=daily_claim_label=badge_label=battery_label=nullptr;
 for(auto &mark:sleep_marks){mark=nullptr;}
 for(auto &form:growth_forms){form=nullptr;}
 for(auto &sparkle:growth_sparkles){sparkle=nullptr;}
 for(auto &strip:growth_confetti){strip=nullptr;}
 for(int i=0;i<3;++i){droppings[i]=daily_labels[i]=badge_labels[i]=nullptr;}
 for(auto &particle:particles){particle=nullptr;}
 for(int i=0;i<5;++i){stat_labels[i]=bars[i]=nullptr;}
 for(auto &button:game_buttons){button=nullptr;}
 for(auto &button:care_buttons){button=nullptr;}
 auto *screen=lv_screen_active();lv_obj_clean(screen);empty(screen);
 lv_obj_add_flag(screen,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_set_style_bg_color(screen,lv_color_hex(PAPER),0);
 lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);world_create();
}
void panel(){if(world)lv_obj_add_flag(world,LV_OBJ_FLAG_HIDDEN);box(lv_screen_active(),0,0,466,466,PAPER,0,255);}
void mini_stats(bool home_layout=false){const int cell=home_layout?61:74;auto*b=box(lv_screen_active(),home_layout?111:85,home_layout?388:345,cell*4,home_layout?52:58,0xf5f5dc,home_layout?20:14,235);for(int i=0;i<4;++i){icon(b,i,(cell-14)/2+i*cell,5,2);stat_labels[i]=label(b,"",i*cell,home_layout?26:28,cell,INK,&lv_font_montserrat_20);}}
void home(bool clock=false){
 btn("主菜单",186,40,94,30,Menu);
 time_label=label(lv_screen_active(),"",93,75,280,0xf8ffed,clock?&lv_font_montserrat_32:&lv_font_montserrat_28);
 date_label=label(lv_screen_active(),"",91,clock?109:104,284,0xf8ffed);
 if(clock)life_label=label(lv_screen_active(),"",105,136,256,0xf8ffed);
 auto*hit=box(lv_screen_active(),113,173,256,157,0,0,0);lv_obj_add_flag(hit,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_add_event_cb(hit,action_cb,LV_EVENT_CLICKED,reinterpret_cast<void*>(static_cast<intptr_t>(Pet)));
 auto*needs=button(lv_screen_active(),"",clock?111:98,clock?307:350,clock?244:270,clock?28:30,NeedCare,0xf3f5d8);
 need_label=lv_obj_get_child(needs,0);
 lv_obj_add_flag(time_label,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(time_label,action_cb,LV_EVENT_CLICKED,reinterpret_cast<void*>(static_cast<intptr_t>(clock?Weather:Clock)));
 if(!clock){
  mini_stats(true);
  care_buttons[0]=btn("喂食",18,177,60,44,Feed);
  care_buttons[1]=btn(model.state.sleeping?"叫醒":"睡觉",20,278,60,44,Sleep);
  care_buttons[2]=btn("玩耍",388,177,60,44,Game);
  care_buttons[3]=btn("卫生",386,278,60,44,Clean);
  for(auto *control:care_buttons)lv_obj_set_ext_click_area(control,10);
  sleep_label=lv_obj_get_child(care_buttons[1],0);
  encounter_button=btn("",158,323,150,26,CollectEncounter,0xf6f2ce);
  lv_obj_add_flag(encounter_button,LV_OBJ_FLAG_HIDDEN);
  wish_button=btn("卷卷有小愿望",130,323,206,26,Wishes,0xf6f2ce);
  lv_obj_add_flag(wish_button,LV_OBJ_FLAG_HIDDEN);
 }else{
  btn("天气",148,357,80,33,Weather);btn("充电",239,357,80,33,Charging);
  btn("喂食",139,412,60,33,Feed);sleep_label=lv_obj_get_child(btn(model.state.sleeping?"叫醒":"睡觉",203,412,60,33,Sleep),0);btn("玩耍",267,412,60,33,Game);
 }
}
void menu(){panel();const char*names[]={"状态","喂食","玩耍","卫生","背包","陪伴","设置","图鉴","关于"};const int cmds[]={Status,Feed,Game,Clean,Bag,Bond,Settings,Catalogue,About};const int kinds[]={0,1,10,4,5,0,7,8,9};for(int i=0;i<9;++i){int x=76+(i%3)*107,y=90+(i/3)*100;auto*b=button(lv_screen_active(),"",x,y,100,94,cmds[i],0xe7edcf);icon(b,kinds[i],32,12,5);label(b,names[i],0,64,100);}btn("场景",126,409,98,32,Scenes);btn("首页",242,409,98,32,Home);}
void status(){
 panel();title("照顾与状态");
 for(int i=0;i<5;++i){int y=107+i*39;label(lv_screen_active(),stat_names[i],102,y,87);stat_labels[i]=label(lv_screen_active(),"",263,y,100);bars[i]=lv_bar_create(lv_screen_active());lv_obj_set_pos(bars[i],125,y+24);lv_obj_set_size(bars[i],217,8);lv_obj_set_style_bg_color(bars[i],lv_color_hex(0xd9e0c6),0);lv_obj_set_style_bg_color(bars[i],lv_color_hex(stat_colors[i]),LV_PART_INDICATOR);}
 info=label(lv_screen_active(),"",99,311,268);
 btn("喂食",96,369,61,31,Feed);btn("治疗",163,369,61,31,Treat);btn("卫生",230,369,61,31,Clean);btn("亲密度",297,369,74,31,Bond);bottom();
}
void feed(){auto*b=box(lv_screen_active(),107,93,252,125,0xf9f9e1,15,240);label(b,"今天想捉哪只小虫？",0,10,252);for(int i=0;i<4;++i){char t[56];std::snprintf(t,sizeof(t),"%s ×%d",ch::item_name(static_cast<ch::Item>(i)),model.state.inventory[i]);auto*bt=button(b,t,12+(i%2)*119,40+(i/2)*40,110,34,FoodBase+i);if(model.state.inventory[i]==0)lv_obj_add_state(bt,LV_STATE_DISABLED);}mini_stats();btn("返回",145,413,77,32,Home);btn("补给",238,413,77,32,Bag);}

// Synchronize the interval before a button starts or ends a timed session.
// start_game moves the model clock to now; replaying that interval would
// otherwise grant a full-session reward one UI period early.
void sync_game_clock(uint64_t now){
 if(now<=last_tick)return;
 const auto before=model.stage();const bool was_game=model.game_running();
 const auto elapsed=static_cast<uint32_t>(std::min<uint64_t>(6ULL*60*60*1000,now-last_tick));
 model.tick(elapsed);last_tick=now;
 if(was_game&&!model.game_running())forest_game_finish(now);
 if(model.stage()!=before){growth_from=static_cast<int>(before);growth_to=static_cast<int>(model.stage());growth_pending=true;save(true);}
}
const char *game_name(ch::games::Kind kind){
 const char *names[]={"捕虫挑战","叶下寻虫","动作模仿","彩球入圈"};return names[static_cast<int>(kind)];
}
void pixel_leaf(lv_obj_t *parent){
 const char *rows[]={"000011110000","001111111100","011111111110","111111111111","011111111100","001111110000","000111000000"};
 for(int y=0;y<7;++y)for(int x=0;x<12;++x)if(rows[y][x]=='1')box(parent,17+x*4,7+y*4,4,4,y==3?0xc7dc64:(x+y)%3?0x6d9d49:0x97bd54);
}
// The game insects need to stand out against bright grass without changing
// the quieter food artwork used by normal feeding and tongue-catch feedback.
void game_insect_shape(lv_obj_t *target,ch::Item type){
 lv_obj_clean(target);box(target,0,0,23,17,0xfff6cf,2);
 constexpr uint32_t outline=0x182a32;
 if(type==ch::Item::Mealworm){
  for(int i=0;i<6;++i){const int y=5+(i%2);box(target,i*3,y,5,9,outline);box(target,i*3+1,y+1,3,6,i%2?0xffb719:0xffdf28);}
  box(target,17,4,6,9,outline);box(target,18,5,4,6,0xc96618);box(target,20,5,2,2,outline);
 }else{
  const bool roach=type==ch::Item::Cockroach,fly=type==ch::Item::Fly;
  for(int i=0;i<3;++i){box(target,3+i*5,1,3,5,outline);box(target,2+i*5,11,3,6,outline);}
  box(target,3,4,roach?15:13,10,outline);box(target,4,5,roach?13:11,8,roach?0xcb4c20:fly?0x172b3b:0xffaa18);
  box(target,15,4,7,7,outline);box(target,16,5,5,5,fly?0xf56e25:0xf1b122);box(target,19,5,2,2,outline);
  if(roach)box(target,10,5,2,8,0xffbe41);
  if(fly){box(target,4,0,8,6,outline);box(target,5,1,6,4,0x4ae4ff);box(target,11,1,7,6,outline);box(target,12,2,5,4,0xb8f9ff);}
 }
}
void game_observe_result(uint64_t now){
 if(game_session.hits()>game_seen_hits){
  game_seen_hits=game_session.hits();
  if(model.award_game_hit(now)){
   game_success_at=now;ball_reaction_step=0;
   if(game_session.kind()==ch::games::Kind::Hunt||game_session.kind()==ch::games::Kind::Leaves){
    game_capture=true;game_capture_start=now;game_capture_stage=static_cast<int>(model.stage());
    game_capture_item=game_session.bug_item();if(game_session.kind()==ch::games::Kind::Leaves){game_capture_x=400;game_capture_y=230;}
    manual_until=0;bug_shape(game_capture_item);
   }else animate(game_session.kind()==ch::games::Kind::Ball?ch::Pose::Walk:ch::Pose::Happy,game_session.kind()==ch::games::Kind::Ball?650:1000);
  }
 }
 if(game_session.misses()>game_seen_misses){game_seen_misses=game_session.misses();animate(ch::Pose::Blink,800);}
}
bool game_input_ready(uint64_t now){
 return !alarm.ringing()&&page==Page::Game&&model.game_running()&&model.game_seconds_left(now)>0&&game_session.phase()==ch::games::Phase::Input&&!game_capture_active(now);
}
void ball_cb(lv_event_t *event){
 const auto code=lv_event_get_code(event);const uint64_t now=now_ms();
 if(code==LV_EVENT_PRESS_LOST){ball_dragging=false;return;}
 if(screen_dimmed||!game_input_ready(now)||game_session.kind()!=ch::games::Kind::Ball){ball_dragging=false;return;}
 auto *input=lv_indev_get_act();if(!input)return;lv_point_t point;lv_indev_get_point(input,&point);touch();
 if(code==LV_EVENT_PRESSED){ball_dragging=true;ball_offset_x=game_session.ball_x()-point.x;ball_offset_y=game_session.ball_y()-point.y;sound(ch_hw::Sound::Tap);}
 else if(code==LV_EVENT_PRESSING&&ball_dragging){game_session.drag(point.x+ball_offset_x,point.y+ball_offset_y,now);game_observe_result(now);}
 else if(code==LV_EVENT_RELEASED&&ball_dragging){game_session.drag(point.x+ball_offset_x,point.y+ball_offset_y,now);game_session.drop(now);ball_dragging=false;game_observe_result(now);}
 game_draw(now);draw_pet(now);
}
void game_draw(uint64_t now){
 if(page!=Page::Game||!info)return;
 if(!model.game_running()&&!game_capture_active(now)){
  set_label_text_fmt(info,"上局命中 %d 次",model.game_hits());
  if(now>toast_until)set_label_text(game_hint,"选个小游戏，一起玩吧");
  return;
 }
 using Kind=ch::games::Kind;using Phase=ch::games::Phase;
 const auto kind=game_session.kind();const auto phase=game_session.phase();
 set_label_text_fmt(info,"命中 %d · 剩余 %d 秒",model.game_hits(),model.game_seconds_left(now));
 const bool accepting=game_input_ready(now);
 for(int i=0;i<3;++i)if(game_buttons[i]){if(accepting)lv_obj_remove_state(game_buttons[i],LV_STATE_DISABLED);else lv_obj_add_state(game_buttons[i],LV_STATE_DISABLED);}
 if(kind==Kind::Hunt&&game_hunt_hit){
  if(accepting){
   if(game_drawn_insect!=game_session.bug_item()){game_insect_shape(bug,game_session.bug_item());game_drawn_insect=game_session.bug_item();}
   const int bx=static_cast<int>(std::lround(game_session.bug_x(now))),by=static_cast<int>(std::lround(game_session.bug_y(now)));
   lv_obj_set_pos(bug,bx-11,by-8);lv_obj_remove_flag(bug,LV_OBJ_FLAG_HIDDEN);
   lv_obj_set_pos(game_hunt_hit,bx-30,by-24);lv_obj_remove_flag(game_hunt_hit,LV_OBJ_FLAG_HIDDEN);
  }else lv_obj_add_flag(game_hunt_hit,LV_OBJ_FLAG_HIDDEN);
 }
 if(kind==Kind::Leaves&&game_drawn_insect!=game_session.bug_item()){
  for(int i=0;i<3;++i)if(game_leaf_bug[i])game_insect_shape(game_leaf_bug[i],game_session.bug_item());
  game_drawn_insect=game_session.bug_item();
 }
 if(kind==Kind::Leaves)for(int i=0;i<3;++i)if(game_buttons[i]){
  lv_obj_set_pos(game_buttons[i],103+static_cast<int>(std::lround(88*game_session.leaf_position(i,now))),365);
  const bool uncovered=game_session.leaf_has_bug(i)&&(phase==Phase::Reveal||(phase==Phase::Feedback&&game_session.last_result()==ch::games::Result::Miss));
  if(uncovered){lv_obj_add_flag(game_leaf_art[i],LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(game_leaf_bug[i],LV_OBJ_FLAG_HIDDEN);}
  else{lv_obj_remove_flag(game_leaf_art[i],LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(game_leaf_bug[i],LV_OBJ_FLAG_HIDDEN);}
 }
 if(kind==Kind::Ball&&game_ball){
  // Successful feedback keeps the ball in its goal: the lower hint is clear
  // of both the ball and even the adult's raised Happy/Wave crest.
  const bool celebration=phase==Phase::Feedback&&game_session.last_result()==ch::games::Result::Hit;
  auto *hint=lv_obj_get_parent(game_hint);lv_obj_set_y(hint,celebration?333:103);
  lv_obj_set_height(hint,celebration?26:24);lv_obj_set_y(game_hint,celebration?3:1);
  lv_obj_set_pos(game_ball,static_cast<int>(std::lround(game_session.ball_x()))-26,static_cast<int>(std::lround(game_session.ball_y()))-26);
  if(accepting)lv_obj_remove_state(game_ball,LV_STATE_DISABLED);else lv_obj_add_state(game_ball,LV_STATE_DISABLED);
  for(int i=0;i<3;++i){
   const bool target=i==game_session.goal_index();auto *goal=game_goals[i];
   lv_obj_set_style_border_color(goal,lv_color_hex(target?0x24ed66:0x4b6770),0);lv_obj_set_style_border_width(goal,target?6:2,0);
   lv_obj_set_style_bg_color(goal,lv_color_hex(target?0x103e2c:0x163845),0);lv_obj_set_style_bg_opa(goal,target?240:100,0);
   lv_obj_set_style_outline_color(goal,lv_color_hex(0xffffff),0);lv_obj_set_style_outline_width(goal,target?2:0,0);lv_obj_set_style_outline_pad(goal,0,0);
   // A white pixel arrow marks the target even when colors are hard to tell.
   auto *arrow=lv_obj_get_child(goal,0);if(target)lv_obj_remove_flag(arrow,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(arrow,LV_OBJ_FLAG_HIDDEN);
  }
 }
 if(now<=toast_until)return;
 if(phase==Phase::Feedback)set_label_text(game_hint,game_session.last_result()==ch::games::Result::Hit?(kind==Kind::Hunt||kind==Kind::Leaves?"捉到了，看它伸舌捕食":"做得好，卷卷很开心"):"再试一次，你可以的");
 else if(kind==Kind::Hunt)set_label_text(game_hint,"轻点亮色小虫，让它伸舌捕食");
 else if(kind==Kind::Leaves)set_label_text(game_hint,phase==Phase::Reveal?"记住亮色小虫在哪片叶子下":phase==Phase::Shuffle?"看清叶子换位，跟住小虫":"点开藏着小虫的叶子");
 else if(kind==Kind::Mimic){if(phase==Phase::Reveal)set_label_text_fmt(game_hint,"看它演示 %d 个动作",game_session.sequence_length());else set_label_text_fmt(game_hint,"按顺序回应 %d/%d",game_session.input_progress(),game_session.sequence_length());}
 else set_label_text(game_hint,"按住彩球，拖进白边亮圈");
}
void game(){
 const bool active=model.game_running()||game_capture_active(now_ms());
 auto *heading=box(lv_screen_active(),113,42,240,30,PAPER,11,235);
 label(heading,active?game_name(game_session.kind()):"一起玩耍",0,5,240,GREEN);
 const bool ball_game=active&&game_session.kind()==ch::games::Kind::Ball;
 auto *score=box(lv_screen_active(),113,ball_game?75:78,240,ball_game?24:26,PAPER,10,225);
 info=label(score,"",0,ball_game?1:3,240,INK);
 auto *hint=box(lv_screen_active(),103,!active?312:ball_game?103:333,260,ball_game?24:26,0x153648,10,255);
 game_hint=label(hint,active?"":"选个小游戏，一起玩吧",0,ball_game?1:3,260,0xffffff);
 if(!active){
  for(int i=0;i<4;++i)btn(game_name(static_cast<ch::games::Kind>(i)),106+(i%2)*139,342+(i/2)*45,115,40,GameSelectBase+i);
  btn("首页",183,432,100,28,Home);return;
 }
 using Kind=ch::games::Kind;
 if(game_session.kind()==Kind::Hunt){game_hunt_hit=box(world,360,206,60,48,0,0,0);lv_obj_add_flag(game_hunt_hit,LV_OBJ_FLAG_CLICKABLE);lv_obj_set_ext_click_area(game_hunt_hit,4);lv_obj_add_event_cb(game_hunt_hit,action_cb,LV_EVENT_CLICKED,reinterpret_cast<void*>(static_cast<intptr_t>(HuntHit)));}
 else if(game_session.kind()==Kind::Leaves){
  for(int i=0;i<3;++i){
   auto *card=button(lv_screen_active(),"",103+i*88,365,82,44,GameOptionBase+i);game_buttons[i]=card;lv_obj_set_style_bg_opa(card,LV_OPA_TRANSP,0);
   // Observation locks input, but must not gray out the clue being memorised.
   lv_obj_set_style_recolor_opa(card,LV_OPA_TRANSP,LV_STATE_DISABLED);
   game_leaf_art[i]=box(card,0,0,82,44,0,0,0);pixel_leaf(game_leaf_art[i]);
   game_leaf_bug[i]=box(card,30,12,23,17,0,0,0);game_insect_shape(game_leaf_bug[i],game_session.bug_item());lv_obj_add_flag(game_leaf_bug[i],LV_OBJ_FLAG_HIDDEN);
  }
 }else if(game_session.kind()==Kind::Mimic){const char *names[]={"眨眼","挥爪","伸懒腰"};for(int i=0;i<3;++i)game_buttons[i]=btn(names[i],103+i*88,365,82,44,GameOptionBase+i);}
 else{
  for(int i=0;i<3;++i){
   game_goals[i]=box(lv_screen_active(),116+i*89,360,56,56,0x163845,28,100);
   auto *arrow=box(game_goals[i],18,15,20,25,0,0,0);
   box(arrow,8,0,4,12,0xffffff);box(arrow,2,10,16,4,0xffffff);box(arrow,5,14,10,4,0xffffff);box(arrow,8,18,4,4,0xffffff);
   lv_obj_add_flag(arrow,LV_OBJ_FLAG_HIDDEN);
  }
  game_ball=box(lv_screen_active(),207,314,52,52,0,0,0);lv_obj_add_flag(game_ball,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_flag(game_ball,LV_OBJ_FLAG_PRESS_LOCK);
  icon(game_ball,10,12,12,4);box(game_ball,16,20,20,8,0xf3cd58);box(game_ball,24,12,8,24,0xea9161);
  lv_obj_add_event_cb(game_ball,ball_cb,LV_EVENT_ALL,nullptr);
 }
 btn("结束",183,425,100,32,EndGame);game_draw(now_ms());
}
void bag(){panel();title(shop?"补给商店":"背包");info=label(lv_screen_active(),"",134,88,198);btn("我的道具",129,113,100,31,Backpack,shop?SOFT:GREEN);btn("补给商店",237,113,100,31,Shop,shop?GREEN:SOFT);for(int i=0;i<7;++i){char t[72];std::snprintf(t,sizeof(t),"%s\n%s%d",ch::item_name(static_cast<ch::Item>(i)),shop?"金币 ":"×",shop?ch::item_price(static_cast<ch::Item>(i)):model.state.inventory[i]);btn(t,95+(i%3)*93,163+(i/3)*70,87,63,(shop?BuyBase:UseBase)+i,0xfafbe9);}if(!shop){btn("森林\n工坊",188,303,87,63,Workshop,0xe7edcf);btn("小愿望",281,303,87,63,Wishes,0xe7edcf);}bottom();}
void clean(){label(lv_screen_active(),"让卷卷清清爽爽",103,45,260,0xf8ffed);info=label(lv_screen_active(),"",96,77,274,0xf8ffed);mini_stats();btn("清理便便",102,409,98,33,ClearPoop,GREEN);btn("洗澡",205,409,74,33,Wash);btn("首页",284,409,80,33,Home);}
void brightness_cb(lv_event_t*e){if(screen_dimmed)return;touch();model.state.brightness=lv_slider_get_value(static_cast<lv_obj_t*>(lv_event_get_target(e)));ch_hw::set_brightness(model.state.brightness);model.mark_dirty();}
void settings(){panel();title("设置");btn(model.state.auto_life?"自主生活   开":"自主生活   关",113,101,240,33,AutoLife);btn(model.state.auto_world?"昼夜天气   开":"昼夜天气   关",113,139,240,33,AutoWorld);btn(model.state.sound?"声音   开":"声音   关",113,177,116,33,Sound);btn("音量调节",237,177,116,33,Audio);label(lv_screen_active(),"屏幕亮度",102,215,102);auto*s=lv_slider_create(lv_screen_active());lv_obj_set_pos(s,220,225);lv_obj_set_size(s,120,7);lv_slider_set_range(s,10,100);lv_slider_set_value(s,model.state.brightness,LV_ANIM_OFF);lv_obj_add_event_cb(s,brightness_cb,LV_EVENT_VALUE_CHANGED,nullptr);char t[60];std::snprintf(t,sizeof(t),"自动熄屏   %s",model.state.idle_minutes?"1分钟":"保持亮屏");btn(t,113,252,240,33,Idle);btn("闹钟",113,290,240,33,Alarm,GREEN);btn("网络与校时",113,338,116,38,Network);btn("自由换场景",237,338,116,38,Scenes);bottom();}
void forest_bottom(){btn("返回背包",138,414,91,31,Bag);btn("首页",237,414,91,31,Home);}
void forest_title(const char *text){btn("‹",91,52,35,35,Bag);label(lv_screen_active(),text,134,53,198);}
void forest_note_create(int y){
 forest_note=label(lv_screen_active(),"",113,y,240);
 lv_label_set_long_mode(forest_note,LV_LABEL_LONG_SCROLL_CIRCULAR);lv_obj_set_height(forest_note,22);
 if(!forest_storage_ok)set_label_text(forest_note,"森林记录不可用，原数据保留");
 else if(!forest.date_ready())set_label_text(forest_note,"先到设置中校准日期");
}
void wishes_page(){
 panel();forest_title("小愿望");
 for(int i=0;i<2;++i){
  auto *card=box(lv_screen_active(),113,105+i*138,240,130,0xe7edcf,12);
  char text[32];std::snprintf(text,sizeof(text),"今日愿望 %d",i+1);label(card,text,12,9,216,GREEN);
  wish_goal[i]=label(card,"",12,35,216);wish_status[i]=label(card,"",12,61,216);
  wish_actions[i]=button(card,"",12,88,102,32,WishActionBase+i);
  wish_skips[i]=button(card,"暂时不做",126,88,102,32,SkipWishBase+i);
 }
 forest_note_create(384);forest_bottom();forest_refresh();
}
void material_icon(lv_obj_t *parent,int material,int x,int y){
 auto *art=box(parent,x,y,24,24,0,0,0);
 const char *patterns[]={"00001110/00112210/01122210/12222100/12221000/01110000/00100000/01000000","00100100/01212210/01233210/00122100/01233210/01212210/00100100","00111100/01222210/12233221/12233221/12222221/01222210/00111100","00010000/00121000/01232100/12233210/12222210/01222100/00111000"};
 const uint32_t colors[4][3]={{0x355c32,0x8dbf48,0xd0e48a},{0xb85181,0xf099bb,0xffd3e2},{0x6b4831,0xc58c54,0xebbc7b},{0x32739c,0x72c2e8,0xe9fbf1}};
 forest_pixel(art,patterns[material],3,colors[material][0],colors[material][1],colors[material][2]);
}
void workshop_page(){
 panel();forest_title("森林工坊");
 btn("材料",113,95,116,31,Materials,workshop_recipes?SOFT:GREEN);
 btn("制作",237,95,116,31,Recipes,workshop_recipes?GREEN:SOFT);
 if(!workshop_recipes){
  for(int i=0;i<4;++i){material_icon(lv_screen_active(),i,124,144+i*43);material_labels[i]=label(lv_screen_active(),"",160,147+i*43,179);}
  forest_note_create(304);forage_button=btn("林间采集",113,332,240,35,Forage);
  btn("季节与节日",113,376,240,31,Calendar);
 }else{
  for(int i=0;i<3;++i){
   auto *card=box(lv_screen_active(),113,134+i*84,240,78,0xe7edcf,10);
   material_icon(card,i==1?2:i==2?1:0,12,25);
   recipe_labels[i]=label(card,"",46,8,184,GREEN);
   const char *cost[]={"叶3 花2","壳3 露2","叶2 花3 露1"};label(card,cost[i],6,47,151);
   recipe_actions[i]=button(card,"",162,39,68,31,CraftBase+i);
  }
  forest_note_create(384);
 }
 forest_bottom();forest_refresh();
}
void calendar_page(){
 panel();forest_title("季节与节日");
 calendar_date=label(lv_screen_active(),"",113,101,240);
 calendar_season=label(lv_screen_active(),"",113,132,240,GREEN);
 label(lv_screen_active(),"采集留下四季印记",113,169,240);
 const char *stamps[]={"春芽","夏萤","秋枫","冬晶"};
 for(int i=0;i<4;++i){
  auto *card=box(lv_screen_active(),113+(i%2)*124,203+(i/2)*60,116,51,forest.state.season_seen&(1u<<i)?0xcdddab:0xe7edcf,9);
  char text[48];std::snprintf(text,sizeof(text),"%s · %s",stamps[i],forest.state.season_seen&(1u<<i)?"已集":"未集");label(card,text,2,16,112);
 }
 calendar_holiday=label(lv_screen_active(),"",113,328,240,GREEN);
 holiday_button=btn("",113,356,240,31,HolidayGift);
 forest_note_create(389);forest_bottom();forest_refresh();
}
void forest_refresh(){
 if(page==Page::Wishes){
  for(int i=0;i<2;++i){
   const auto &w=forest.state.wishes[i];
   if(!forest.date_ready())set_label_text(wish_goal[i],"校准日期后开启愿望");
   else if(w.kind==0)set_label_text_fmt(wish_goal[i],"想捉一只%s",ch::item_name(static_cast<ch::Item>(w.target)));
   else if(w.kind==1)set_label_text(wish_goal[i],"想被你轻轻摸摸头");
   else set_label_text_fmt(wish_goal[i],"想玩%s",game_name(static_cast<ch::games::Kind>(w.target)));
   const char *statuses[]={"不用赶时间","可以跳过，不扣状态","完成啦 · 收获森林材料","今天已跳过"};
   if(w.status==1&&w.kind==1)set_label_text_fmt(wish_status[i],"摸摸 %u/3 · 可以跳过",static_cast<unsigned>(w.progress));
   else if(w.status==1&&w.kind==2)set_label_text(wish_status[i],"玩满一局，并命中一次");
   else set_label_text(wish_status[i],statuses[w.status]);
   set_label_text(lv_obj_get_child(wish_actions[i],0),w.status==0?"听听愿望":w.status==1?"去完成":w.status==2?"已完成":"已跳过");
   const bool available=forest_storage_ok&&forest.date_ready()&&(w.status==1||(w.status==0&&forest.active_wish()<0&&(i==0||forest.state.wishes[0].status>=2)));
   if(available)lv_obj_remove_state(wish_actions[i],LV_STATE_DISABLED);else lv_obj_add_state(wish_actions[i],LV_STATE_DISABLED);
   if(forest_storage_ok&&forest.date_ready()&&w.status==1)lv_obj_remove_state(wish_skips[i],LV_STATE_DISABLED);else lv_obj_add_state(wish_skips[i],LV_STATE_DISABLED);
  }
 }
 if(page==Page::Workshop){
  for(int i=0;i<4;++i)if(material_labels[i])set_label_text_fmt(material_labels[i],"%s ×%u",ch_forest::material_name(static_cast<ch_forest::Material>(i)),static_cast<unsigned>(forest.state.materials[i]));
  for(int i=0;i<3;++i)if(recipe_labels[i]){
   const bool made=forest.state.crafted_mask&(1u<<i),placed=forest.state.equipped==i+1;
   set_label_text_fmt(recipe_labels[i],"%s%s",ch_forest::recipes[i].name,placed?" · 当前":"");
   set_label_text(lv_obj_get_child(recipe_actions[i],0),placed?"收起":made?"摆放":"制作");
   if(forest_storage_ok)lv_obj_remove_state(recipe_actions[i],LV_STATE_DISABLED);else lv_obj_add_state(recipe_actions[i],LV_STATE_DISABLED);
  }
  if(forage_button){
   if(!forest.date_ready())set_label_text(lv_obj_get_child(forage_button,0),"校时后可以采集");
   else if(forest.state.forage_used>=3)set_label_text(lv_obj_get_child(forage_button,0),"今天采集完成 3/3");
   else if(forest.state.forage_wait_ms)set_label_text_fmt(lv_obj_get_child(forage_button,0),"下次采集 %lu秒",static_cast<unsigned long>((forest.state.forage_wait_ms+999)/1000));
   else set_label_text_fmt(lv_obj_get_child(forage_button,0),"林间采集 %u/3",static_cast<unsigned>(forest.state.forage_used));
  }
 }
 if(page==Page::Calendar){
  set_label_text_fmt(calendar_date,forest.date_ready()?"%lu年%lu月%lu日":"日期待校准",static_cast<unsigned long>(forest.state.day/10000),static_cast<unsigned long>(forest.state.day/100%100),static_cast<unsigned long>(forest.state.day%100));
  set_label_text(calendar_season,forest.date_ready()?ch_forest::season_name(ch_forest::season_for_date(forest.state.day)):"四季印记会一直保留");
  const auto holiday=forest.date_ready()?ch_forest::holiday_for_date(forest.state.day):ch_forest::Holiday::None;
  set_label_text(calendar_holiday,ch_forest::holiday_name(holiday));
  set_label_text(lv_obj_get_child(holiday_button,0),!forest.date_ready()?"校时后开启彩蛋":holiday==ch_forest::Holiday::None?"今天没有节日礼物":forest.holiday_available()?"领取节日礼物":"今年已领取");
  if(forest_storage_ok&&forest.holiday_available())lv_obj_remove_state(holiday_button,LV_STATE_DISABLED);else lv_obj_add_state(holiday_button,LV_STATE_DISABLED);
 }
}
void audio_refresh(){
 char text[16];std::snprintf(text,sizeof(text),"%d%%",effects_level);set_label_text(effects_value,text);
 std::snprintf(text,sizeof(text),"%d%%",alarm_level);set_label_text(alarm_value,text);
 set_label_text(audio_note,audio_storage_ok?"0% 静音 · 每次调节5%":"音量未保存，原数据保留");
}
void audio_page(){
 panel();title("音量调节");
 label(lv_screen_active(),"按键与互动",113,101,240);
 effects_value=label(lv_screen_active(),"",183,129,100);
 btn("减小",113,165,110,40,EffectsDown);btn("增大",243,165,110,40,EffectsUp);
 label(lv_screen_active(),"闹钟音量",113,224,240);
 alarm_value=label(lv_screen_active(),"",183,252,100);
 btn("减小",113,287,110,40,AlarmVolumeDown);btn("增大",243,287,110,40,AlarmVolumeUp);
 btn("试听闹钟",113,339,240,36,AlarmPreview,GREEN);
 audio_note=label(lv_screen_active(),"",113,382,240);
 btn("返回设置",159,414,148,31,Settings);audio_refresh();
}
void alarm_refresh(){
 if(alarm_cancel_button&&alarm_network_button){
  if(alarm.snoozed()){lv_obj_remove_flag(alarm_cancel_button,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(alarm_network_button,LV_OBJ_FLAG_HIDDEN);}
  else{lv_obj_add_flag(alarm_cancel_button,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(alarm_network_button,LV_OBJ_FLAG_HIDDEN);}
 }
 if(alarm_hour)set_label_text_fmt(alarm_hour,"%02u",static_cast<unsigned>(alarm_draft.hour));
 if(alarm_minute)set_label_text_fmt(alarm_minute,"%02u",static_cast<unsigned>(alarm_draft.minute));
 if(alarm_toggle)set_label_text(alarm_toggle,alarm_draft.enabled?"每日闹钟   开":"每日闹钟   关");
 if(alarm_now){if(hw.time_valid)set_label_text_fmt(alarm_now,"现在 %02d:%02d",hw.hour,hw.minute);else set_label_text(alarm_now,"当前时间未校准");}
 if(alarm_note){
  if(alarm_message[0])set_label_text(alarm_note,alarm_message);
  else if(!alarm_storage_ok)set_label_text(alarm_note,"闹钟保存不可用");
  else if(!hw.time_valid)set_label_text(alarm_note,"先联网或手动校时\n校时后按北京时间提醒");
  else if(alarm.snoozed())set_label_text(alarm_note,"已延后5分钟\n熄屏也会提醒");
  else set_label_text(alarm_note,"北京时间 · 每天一次\n铃声跟随声音设置，熄屏也提醒");
 }
}
void alarm_page(){
 panel();title("闹钟");alarm_draft=alarm.config();if(alarm_storage_ok)alarm_message[0]=0;
 alarm_now=label(lv_screen_active(),"",105,93,256);
 btn("+",135,130,82,36,AlarmHourUp);btn("+",249,130,82,36,AlarmMinuteUp);
 alarm_hour=label(lv_screen_active(),"",135,178,82,INK,&lv_font_montserrat_32);label(lv_screen_active(),":",224,178,18,INK,&lv_font_montserrat_32);alarm_minute=label(lv_screen_active(),"",249,178,82,INK,&lv_font_montserrat_32);
 btn("-",135,226,82,36,AlarmHourDown);btn("-",249,226,82,36,AlarmMinuteDown);
 alarm_toggle=lv_obj_get_child(btn("",113,276,240,36,AlarmToggle),0);
 alarm_note=label(lv_screen_active(),"",93,321,280);lv_obj_set_style_text_font(alarm_note,&chameleon_font_18,0);
 btn("保存闹钟",113,372,240,36,AlarmSave,GREEN);
 alarm_cancel_button=btn("取消稍后",140,416,90,31,AlarmCancelSnooze);alarm_network_button=btn("网络校时",140,416,90,31,Network);btn("返回",236,416,90,31,Settings);alarm_refresh();
}
void alarm_close(){ch_hw::cancel_alarm_sound();if(alarm_overlay){lv_obj_delete(alarm_overlay);alarm_overlay=nullptr;}}
void alarm_present(){
 if(alarm_overlay||!alarm.ringing())return;
 ch_hw::cancel_alarm_sound(); // A real reminder replaces any outstanding preview.
 ball_dragging=false;
 for(auto *input=lv_indev_get_next(nullptr);input;input=lv_indev_get_next(input))if(lv_indev_get_type(input)==LV_INDEV_TYPE_POINTER&&lv_indev_get_state(input)==LV_INDEV_STATE_PRESSED)lv_indev_wait_release(input);
 alarm_overlay=box(lv_layer_top(),0,0,466,466,0x153648,0,210);lv_obj_add_flag(alarm_overlay,LV_OBJ_FLAG_CLICKABLE);
 auto *card=box(alarm_overlay,96,103,274,260,PAPER,24);
 label(card,"闹钟响了",17,28,240,GREEN);char text[16];std::snprintf(text,sizeof(text),"%02d:%02d",hw.hour,hw.minute);label(card,text,17,67,240,INK,&lv_font_montserrat_32);
 label(card,model.state.sound?"森林里的小提醒":"声音已关闭 · 屏幕提醒",17,113,240);
 button(card,"延后5分钟",27,157,220,36,AlarmSnooze);button(card,"停止闹钟",27,207,220,36,AlarmStop,GREEN);
}
void alarm_tick(uint64_t now){
 const auto event=alarm.observe(hw.epoch_seconds,hw.time_valid,now);
 if(event==ch_alarm::Event::RingStarted){
  alarm_message[0]=0;alarm_save(alarm.config());wake_screen();alarm_present();alarm_next_sound=now;
 }else if(event==ch_alarm::Event::TimedOut||event==ch_alarm::Event::ClockLost||event==ch_alarm::Event::ClockChanged)alarm_close();
 if(alarm.ringing()&&now>=alarm_next_sound){sound(ch_hw::Sound::Alarm);alarm_next_sound=now+3000;}
}
void catalogue(){panel();title("生活图鉴");btn("今日照顾与徽章",100,94,164,33,Care,GREEN);btn("成长",269,94,96,33,Growth);const int poses[]={0,4,5,6,7,1,8,9,10,3,11,12,14};for(int i=0;i<13;++i)btn(pose_names[poses[i]],96+(i%3)*93,138+(i/3)*48,86,42,MotionBase+poses[i]);bottom();}
void growth(){
 panel();title("成长记录");info=label(lv_screen_active(),"",104,108,258);
 growth_bar=lv_bar_create(lv_screen_active());lv_obj_set_pos(growth_bar,118,211);lv_obj_set_size(growth_bar,230,12);
 lv_obj_set_style_bg_color(growth_bar,lv_color_hex(0xdce5cb),0);lv_obj_set_style_bg_color(growth_bar,lv_color_hex(GREEN),LV_PART_INDICATOR);
 label(lv_screen_active(),"7天少年 · 14天成年",104,245,258);
 label(lv_screen_active(),"健康照顾累计成长时间\n状态不足时暂停成长\n重启继续，断电期间不计",104,280,258);
 btn("图鉴",139,409,92,33,Catalogue);btn("首页",239,409,92,33,Home);
}
void growth_moment(){
 // A dedicated milestone, using the approved forms rather than new artwork.
 panel();growth_shown_at=now_ms();
 // Pixel paper strips fall behind the forms and text, keeping both clear.
 const uint32_t party_colors[6]={0xef6f8b,0xefbc47,0x78bc72,0x62bfc4,0x8e84cb,0xf08e55};
 for(int i=0;i<24;++i)growth_confetti[i]=box(lv_screen_active(),12+(i*67+31)%440,(i*83+20)%460,i%2?3:8,i%2?9:3,party_colors[i%6],0,205);
 label(lv_screen_active(),growth_to==CH_STAGE_TEEN?"长成少年啦！":"长成成年啦！",91,57,284,GREEN,&chameleon_font_18);
 char stage_text[48];std::snprintf(stage_text,sizeof(stage_text),"%s  →  %s",ch_stages[growth_from].label,ch_stages[growth_to].label);
 label(lv_screen_active(),stage_text,105,98,256);
 const int stages[2]={growth_from,growth_to};
 for(int i=0;i<2;++i){
  const int left=73+i*170;auto*card=box(lv_screen_active(),left,151,150,178,i?0xdce9bd:0xe8ead6,19);
  growth_forms[i]=lv_image_create(card);lv_image_set_src(growth_forms[i],ch_stages[stages[i]].animations[CH_IDLE].frames[0]);
  lv_image_set_pivot(growth_forms[i],0,0);lv_image_set_antialias(growth_forms[i],false);lv_image_set_scale(growth_forms[i],256);
  lv_obj_set_pos(growth_forms[i],11,9);label(card,ch_stages[stages[i]].label,0,146,150);
 }
 label(lv_screen_active(),"→",219,222,28,GREEN);
 label(lv_screen_active(),growth_to==CH_STAGE_TEEN?"7天的健康照顾":"14天的健康照顾",103,350,260);
 btn("回到森林",153,400,160,39,FinishGrowth,GREEN);
 const int sx[6]={116,167,300,351,207,258},sy[6]={128,136,136,128,333,333};
 for(int i=0;i<6;++i){growth_sparkles[i]=box(lv_screen_active(),sx[i],sy[i],6,6,0xe9bf51,0);}
}
void bond(){
 panel();title("亲密与陪伴");icon(lv_screen_active(),0,213,103,5);
 info=label(lv_screen_active(),"",101,146,264);
 bond_bar=lv_bar_create(lv_screen_active());lv_obj_set_pos(bond_bar,121,199);lv_obj_set_size(bond_bar,224,10);
 lv_obj_set_style_bg_color(bond_bar,lv_color_hex(SOFT),0);lv_obj_set_style_bg_color(bond_bar,lv_color_hex(0xec8794),LV_PART_INDICATOR);
 label(lv_screen_active(),"喂食 · 抚摸 · 陪玩\n慢慢成为彼此的伙伴",104,224,258);
 for(int i=0;i<2;++i){
  const int x=103+i*134;auto *card=box(lv_screen_active(),x,284,126,64,0xe7edcf,0);
  anniversary_cards[i]=label(card,"",2,5,122);
  btn(i?"重温30天":"重温21天",x,354,126,31,i?Replay30:Replay21);
 }
 bottom();
}
void anniversary(){
 anniversary_started=now_ms();
 const uint32_t colors[]={0xef6f8b,0xefbc47,0x78bc72,0x62bfc4,0x8e84cb,0xf08e55};
 for(int i=0;i<24;++i)growth_confetti[i]=box(world,12+(i*67+31)%440,(i*83+20)%460,i%2?3:8,i%2?9:3,colors[i%6],0,205);
 lv_obj_move_foreground(face);
 char text[48];std::snprintf(text,sizeof(text),"陪伴%u天啦！",static_cast<unsigned>(anniversary_day));
 auto *heading=box(lv_screen_active(),112,50,242,34,0xf5f2de,0,240);label(heading,text,0,7,242,GREEN);
 anniversary_plaque(anniversary_day);
 btn("回到森林",153,393,160,36,FinishAnniversary);
}
void care(){
 panel();title("今日照顾");info=label(lv_screen_active(),"",102,94,262);
 const char*names[]={"喂食一次","清便便一次","玩耍一次"};const char*actions[]={"喂食","卫生","玩耍"};const int cmds[]={Feed,Clean,Game};
 for(int i=0;i<3;++i){daily_labels[i]=label(lv_screen_active(),names[i],103,150+i*42,185);btn(actions[i],294,144+i*42,68,32,cmds[i]);}
 daily_claim_label=lv_obj_get_child(btn("",129,274,208,33,ClaimDaily,GREEN),0);
 badge_label=label(lv_screen_active(),"",99,319,268);for(int i=0;i<3;++i){auto*b=box(lv_screen_active(),106+i*87,345,80,47,SOFT,9);badge_labels[i]=label(b,"",0,3,80);}bottom();
}
void about(){panel();title("关于");label(lv_screen_active(),"卷卷的小日子",106,126,254);label(lv_screen_active(),"一只卷尾变色龙的森林日常\n陪伴照顾 · 伸舌捕食\n幼年 · 少年 · 成年\n昼夜天气 · 像素场景",104,170,258);label(lv_screen_active(),"版本 0.4.7",104,300,258);bottom();}
void weather(){weather_city_label=label(lv_screen_active(),hw.weather_city,131,51,204,0xf8ffed);info=label(lv_screen_active(),"",103,87,260,0xf8ffed,&lv_font_montserrat_32);date_label=label(lv_screen_active(),"",103,130,260,0xf8ffed);life_label=label(lv_screen_active(),"",105,163,256,0xf8ffed);btn("刷新",123,413,70,32,RefreshWeather);btn("首页",200,413,70,32,Home);btn("时钟",277,413,70,32,Clock);}
void charging(){auto*r=box(lv_screen_active(),155,58,156,130,0xeff4d8,28,245);icon(r,2,69,13,3);info=label(r,"",10,48,136,GREEN,&lv_font_montserrat_32);date_label=label(r,"",8,91,140);bottom();}
void scenes(){panel();title("自由换场景");for(int i=0;i<5;++i){char text[64];std::snprintf(text,sizeof(text),"%s%s",scene_names[i],model.state.scene==static_cast<ch::Scene>(i)?" · 当前":"");btn(text,113,109+i*45,240,37,SceneBase+i,model.state.scene==static_cast<ch::Scene>(i)?GREEN:SOFT);}label(lv_screen_active(),"手选后保持场景，可在设置开启昼夜",101,344,264);bottom();}
void network(){panel();title("网络与校时");info=label(lv_screen_active(),"",105,103,256);label(lv_screen_active(),"连接热点 Juanjuan-Setup\n密码 juanjuan\n打开 192.168.4.1\n设置 Wi-Fi、天气城市和时间",101,168,264);btn(hw.provisioning?"关闭设置热点":"打开设置热点",111,290,244,38,Provision,GREEN);btn("生活场景",129,344,100,33,Scenes);btn("设置",237,344,100,33,Settings);bottom();}
void header_battery(){if(page==Page::Home||page==Page::Clock||page==Page::Menu||page==Page::Status)battery_label=label(lv_screen_active(),"",156,18,154,page==Page::Home||page==Page::Clock?0xf8ffed:INK);}
void footer(){const bool home_layout=page==Page::Home;toast_obj=label(lv_screen_active(),"",home_layout?98:88,home_layout?350:page==Page::Game?333:307,home_layout?270:290);lv_obj_set_style_bg_color(toast_obj,lv_color_hex(0xf9f9e7),0);lv_obj_set_style_bg_opa(toast_obj,LV_OPA_COVER,0);lv_obj_set_style_radius(toast_obj,10,0);lv_obj_set_style_pad_all(toast_obj,7,0);if(home_layout||page==Page::Game){lv_obj_set_style_pad_top(toast_obj,home_layout?4:3,0);lv_obj_set_style_pad_bottom(toast_obj,home_layout?4:3,0);lv_label_set_long_mode(toast_obj,LV_LABEL_LONG_SCROLL_CIRCULAR);lv_obj_set_height(toast_obj,home_layout?32:28);}lv_obj_add_flag(toast_obj,LV_OBJ_FLAG_HIDDEN);cover=box(lv_screen_active(),0,0,466,466,0x425b67,0,245);label(cover,"短按侧键唤醒",117,218,232,0xe7f0db);lv_obj_add_flag(cover,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(cover,[](lv_event_t*){touch();},LV_EVENT_CLICKED,nullptr);if(!screen_dimmed)lv_obj_add_flag(cover,LV_OBJ_FLAG_HIDDEN);}
void open(Page next){if(page==Page::Game&&next!=Page::Game){if(model.game_running()){model.end_game();save(true);}game_capture=game_finish_pending=game_return_home=false;game_session.finish();ball_dragging=false;}
 if(page==Page::Audio&&next!=Page::Audio&&!alarm.ringing())ch_hw::cancel_alarm_sound();
 // A scene command has already changed model.state. Resolve it before
 // world_create chooses both its background and the pet's branch baseline.
 rendered_scene=ch::effective_scene(model.state,hw.time_valid?hw.hour:-1);
 reset_screen(next);switch(next){case Page::Home:home();break;case Page::Menu:menu();break;case Page::Status:status();break;case Page::Feed:feed();break;case Page::Game:game();break;case Page::Bag:bag();break;case Page::Clean:clean();break;case Page::Settings:settings();break;case Page::Catalogue:catalogue();break;case Page::About:about();break;case Page::Clock:home(true);break;case Page::Weather:weather();break;case Page::Charging:charging();break;case Page::Scenes:scenes();break;case Page::Network:network();break;case Page::Care:care();break;case Page::Growth:growth();break;case Page::GrowthMoment:growth_moment();break;case Page::Bond:bond();break;case Page::Anniversary:anniversary();break;case Page::Alarm:alarm_page();break;case Page::Audio:audio_page();break;case Page::Wishes:wishes_page();break;case Page::Workshop:workshop_page();break;case Page::Calendar:calendar_page();break;}header_battery();footer();
 // The first frame needs the same tint, weather and sprite layout as the
 // timer's frames. A transparent initial shade exposed a bright flash until
 // the next 40 ms tick whenever a world page was rebuilt.
 const uint64_t now=now_ms();
 if(!lv_obj_has_flag(world,LV_OBJ_FLAG_HIDDEN)){environment_draw(now,world_weather());forest_draw(now,world_weather());draw_pet(now);}
 else if(page==Page::GrowthMoment){draw_pet(now);}
 game_draw(now);forest_attention(now);refresh();}
void command(int cmd){
 if(screen_dimmed)return;
 if(alarm.ringing()&&cmd!=AlarmStop&&cmd!=AlarmSnooze)return;
 if(cmd==Forage||cmd==HolidayGift||(cmd>=CraftBase&&cmd<CraftBase+3)||(cmd>=WishActionBase&&cmd<WishActionBase+2)||(cmd>=SkipWishBase&&cmd<SkipWishBase+2)){
  if(!forest_storage_ok){say("森林记录不可用，原数据保留");return;}
  const bool needs_date=cmd==Forage||cmd==HolidayGift||cmd>=WishActionBase;
  if(needs_date&&!forest.date_ready()){say("先到设置中校准日期");return;}
  const auto previous=forest.state;
  if(cmd>=CraftBase&&cmd<CraftBase+3){
   const int recipe=cmd-CraftBase;const bool made=forest.state.crafted_mask&(1u<<recipe);
   const bool changed=made?forest.equip(forest.state.equipped==recipe+1?-1:recipe):forest.craft(recipe);
   if(!changed){say("材料不足，先去林间采集");return;}
   if(!forest_save(true)){forest.restore(previous);return;}
   forest_refresh();say(made?(forest.state.equipped?"装饰已摆放":"装饰已收起"):"制作完成，已摆放到森林");return;
  }
  if(cmd>=SkipWishBase&&cmd<SkipWishBase+2){
   if(forest.skip(cmd-SkipWishBase)){if(!forest_save(true)){forest.restore(previous);forest_refresh();return;}forest_refresh();say("今天先休息，不扣任何状态");}return;
  }
  if(cmd>=WishActionBase&&cmd<WishActionBase+2){
   const int index=cmd-WishActionBase;const auto wish=forest.state.wishes[index];
   if(wish.status==0){
    if(index==1&&forest.state.wishes[0].status<2)return;
    if(model.state.sleeping||model.state.sick||model.busy(now_ms())){say("先等它休息或当前互动结束");return;}
    if(forest.issue()){if(!forest_save(true))forest.restore(previous);forest_refresh();}return;
   }
   if(wish.status!=1)return;
   if(wish.kind==0)open(Page::Feed);
   else if(wish.kind==1)command(Pet);
   else command(GameSelectBase+wish.target);
   return;
  }
  if(cmd==Forage){
   if(model.state.sleeping||model.state.sick||model.busy(now_ms())){say("先等它休息或当前互动结束");return;}
   const int material=forest.forage();
   if(material<0){say(forest.state.forage_used>=3?"今天已经采集三次，明天再来":"让森林休息一下，稍后再采集");return;}
   if(!forest_save(true)){forest.restore(previous);return;}
   char text[96];const unsigned found=forest.state.materials[material]-previous.materials[material];
   if(found)std::snprintf(text,sizeof(text),"找到%s ×%u",ch_forest::material_name(static_cast<ch_forest::Material>(material)),found);
   else std::snprintf(text,sizeof(text),"四季印记已记录，这种材料已满");
   forest_refresh();say(text);return;
  }
  if(cmd==HolidayGift&&forest.holiday_gift()){
   if(!forest_save(true)){forest.restore(previous);return;}
   forest_celebrate_until=now_ms()+6000;forest_refresh();say("节日快乐！四种材料已放进工坊");return;
  }
  return;
 }
 if(cmd>=FoodBase&&cmd<FoodBase+4){const auto item=static_cast<ch::Item>(cmd-FoodBase);if(model.feed_start(item,now_ms())){feed_visual_stage=static_cast<int>(model.stage());manual_until=0;open(Page::Home);bug_shape(model.feed_item());say("发现小虫，张嘴捕食！");save(true);}else if(model.state.inventory[static_cast<unsigned>(item)]<=0){char text[72];std::snprintf(text,sizeof(text),"没有%s了，去背包补充",ch::item_name(item));say(text);}else say(model.state.sleeping?"先叫醒卷卷":"已经吃饱，或正忙着");return;}
 if(cmd>=BuyBase&&cmd<BuyBase+ch::kItemCount){const auto item=static_cast<ch::Item>(cmd-BuyBase);if(model.buy(item)){save(true);open(Page::Bag);say("买到一份补给");}else say(model.state.inventory[static_cast<unsigned>(item)]>=99?"已有99份，不能继续购买":"金币不足，玩耍赚金币");return;}
 if(cmd>=UseBase&&cmd<UseBase+ch::kItemCount){const auto item=static_cast<ch::Item>(cmd-UseBase);if(ch::is_food(item)){command(FoodBase+cmd-UseBase);return;}if(item==ch::Item::Medicine){command(Treat);}else if(item==ch::Item::Soap)command(Wash);else command(Game);return;}
 if(cmd>=SceneBase&&cmd<SceneBase+5){if(model.feeding()||model.game_running()){say("先等当前活动结束");return;}model.select_scene(static_cast<ch::Scene>(cmd-SceneBase));model.reset_life(now_ms());manual_until=0;open(Page::Home);say("已手选场景，昼夜天气可重新开启");save(true);return;}
 if(cmd>=MotionBase&&cmd<MotionBase+15){const auto p=static_cast<ch::Pose>(cmd-MotionBase);if(p==ch::Pose::Sleep){command(Sleep);return;}if(p==ch::Pose::Bath){command(Wash);return;}if(p==ch::Pose::Pet){command(Pet);return;}if(p==ch::Pose::Eat){command(Feed);return;}if(model.busy(now_ms())||model.state.sleeping){say("等当前动作结束再试");return;}if(p==ch::Pose::Walk&&!model.walk(now_ms()))return;if(p==ch::Pose::Dizzy)model.shake(now_ms());open(Page::Home);animate(p,p==ch::Pose::Walk?2600:1900);return;}
 if(cmd>=GameSelectBase&&cmd<GameSelectBase+4){
  const auto kind=static_cast<ch::games::Kind>(cmd-GameSelectBase);const uint64_t now=now_ms();sync_game_clock(now);
  if(model.start_game(now,kind==ch::games::Kind::Ball)){
   game_session.start(kind,model.stage(),now,esp_random());forest_game_started=now;forest_game_recorded=false;forest_game_wish_done=false;game_seen_hits=game_seen_misses=0;
   game_capture=game_finish_pending=game_return_home=ball_dragging=false;manual_until=0;toast_until=0;
   open(Page::Game);save(true);
  }else say(model.state.sick?"生病时先治疗，康复后再玩":model.state.sleeping?"先叫醒卷卷，再来玩耍":model.starving()?"先吃点小虫，再玩耍":model.state.energy<10?"精力不足，先睡一会儿":kind==ch::games::Kind::Ball&&model.state.inventory[static_cast<int>(ch::Item::Ball)]<=0?"没有彩球了，去背包补给":"先等当前互动结束");
  return;
 }
 if(cmd==HuntHit||(cmd>=GameOptionBase&&cmd<GameOptionBase+3)){
  const uint64_t now=now_ms();if(!game_input_ready(now))return;
  if(cmd==HuntHit){if(game_session.kind()!=ch::games::Kind::Hunt)return;game_capture_x=game_session.bug_x(now);game_capture_y=game_session.bug_y(now);}
  else if(game_session.kind()!=ch::games::Kind::Leaves&&game_session.kind()!=ch::games::Kind::Mimic)return;
  game_session.choose(cmd==HuntHit?0:cmd-GameOptionBase,now);game_observe_result(now);draw_pet(now);game_draw(now);return;
 }
 switch(cmd){
 case Home:open(Page::Home);break;case Menu:open(Page::Menu);break;case Status:open(Page::Status);break;case Feed:open(Page::Feed);break;case Game:open(Page::Game);break;case Bag:shop=false;open(Page::Bag);break;case Clean:open(Page::Clean);break;case Settings:open(Page::Settings);break;case Catalogue:open(Page::Catalogue);break;case About:open(Page::About);break;case Clock:open(Page::Clock);break;case Weather:open(Page::Weather);break;case Charging:open(Page::Charging);break;case Scenes:open(Page::Scenes);break;case Network:open(Page::Network);break;case Care:open(Page::Care);break;case Growth:open(Page::Growth);break;case Alarm:open(Page::Alarm);break;case Audio:open(Page::Audio);break;case Wishes:open(Page::Wishes);break;case Workshop:workshop_recipes=false;open(Page::Workshop);break;case Calendar:open(Page::Calendar);break;case Materials:case Recipes:workshop_recipes=cmd==Recipes;open(Page::Workshop);break;
 case EffectsDown:case EffectsUp:case AlarmVolumeDown:case AlarmVolumeUp:{
  int &level=(cmd==EffectsDown||cmd==EffectsUp)?effects_level:alarm_level;
  const int next=ch_audio::volume(level+((cmd==EffectsUp||cmd==AlarmVolumeUp)?5:-5));
  if(next!=level){level=next;ch_hw::set_sound_volumes(effects_level,alarm_level);audio_save();}
  audio_refresh();break;
 }
 case AlarmPreview:
  if(!model.state.sound)set_label_text(audio_note,"请先开启声音");
  else if(alarm_level==0)set_label_text(audio_note,"闹钟音量为0%，请调高");
  else {ch_hw::cancel_alarm_sound();sound(ch_hw::Sound::Alarm);}
  break;
 case AlarmHourUp:alarm_draft.hour=(alarm_draft.hour+1)%24;alarm_message[0]=0;alarm_refresh();break;
 case AlarmHourDown:alarm_draft.hour=(alarm_draft.hour+23)%24;alarm_message[0]=0;alarm_refresh();break;
 case AlarmMinuteUp:alarm_draft.minute=(alarm_draft.minute+1)%60;alarm_message[0]=0;alarm_refresh();break;
 case AlarmMinuteDown:alarm_draft.minute=(alarm_draft.minute+59)%60;alarm_message[0]=0;alarm_refresh();break;
 case AlarmToggle:alarm_draft.enabled=!alarm_draft.enabled;alarm_message[0]=0;alarm_refresh();break;
 case AlarmSave:
  if(alarm_save(alarm_draft)){alarm.configure(alarm_draft);alarm_close();std::snprintf(alarm_message,sizeof(alarm_message),alarm_draft.enabled?(hw.time_valid?"已保存，每天到点提醒":"已保存，校准时间后提醒"):"闹钟已关闭");}
  else std::snprintf(alarm_message,sizeof(alarm_message),"闹钟保存失败，请检查");
  alarm_refresh();break;
 case AlarmStop:alarm.stop(now_ms());alarm_close();alarm_message[0]=0;touch();break;
 case AlarmSnooze:if(alarm.snooze(now_ms())==ch_alarm::Event::Snoozed){alarm_close();touch();if(page==Page::Alarm)open(Page::Alarm);}break;
 case AlarmCancelSnooze:alarm.stop(now_ms());alarm_close();open(Page::Alarm);break;
 case Pet:if(model.pet(now_ms())){const auto previous=forest.state;bool complete=forest_storage_ok&&forest.pet(model.state.strokes);save(true);if(forest_storage_ok&&forest.dirty()&&!forest_save(true)){forest.restore(previous);complete=false;}animate(ch::Pose::Pet);open(Page::Home);say(complete?"愿望完成 · 收获森林材料":model.state.affinity>=80?"最喜欢你的摸摸啦":model.state.affinity>=50?"熟悉的小手，让我很安心":"摸摸卷卷的小脑袋");}else say("正在休息或忙着呢");break;
 case Sleep:{const bool was=model.state.sleeping;const bool ok=was?model.wake():model.sleep();if(ok){manual_until=0;open(Page::Home);if(was)animate(ch::Pose::Stretch);say(was?"睡醒啦，伸个懒腰":"晚安，卷卷");save(true);}else say("先等当前动作结束");break;}
 case Wash:
  if(model.clean(now_ms())){open(Page::Clean);animate(ch::Pose::Bath,2200);say("清清爽爽，洗澡完成");save(true);}
  else if(model.state.cleanliness>=100)say("已经很干净啦，不用再洗澡");
  else if(model.state.sleeping)say("先叫醒卷卷再洗澡");
  else if(model.feeding()||model.game_running())say("先等捕食或游戏结束再洗澡");
  else say("泡泡皂用完了，去背包补给商店补充");
  break;
 case EndGame:sync_game_clock(now_ms());model.end_game();game_session.finish();ball_dragging=false;save(true);if(game_capture_active(now_ms())){game_finish_pending=true;game_return_home=true;refresh();}else{open(Page::Home);say(forest_game_wish_done?"愿望完成 · 收获森林材料":"奖励已结算");}break;
 case FinishGrowth:growth_pending=false;open(Page::Home);break;
 case Bond:open(Page::Bond);break;
 case Replay21:case Replay30:{
  const uint8_t day=cmd==Replay21?21:30;
  if(!(model.state.anniversary_claimed&(day==21?1:2))){say("陪伴满期并成年后，会自动庆祝");break;}
  if(model.state.sleeping||model.state.sick||model.feeding()||model.game_running()||game_capture_active(now_ms())){say("先等它休息或当前互动结束");break;}
  anniversary_day=day;manual_until=0;open(Page::Anniversary);break;
 }
 case FinishAnniversary:open(Page::Home);break;
 case CollectEncounter:{
  const int encounter=active_encounter;
  if(encounter>=0&&now_ms()<encounter_expires&&model.complete_encounter(static_cast<ch::Encounter>(encounter),hw.time_valid?hw.hour:-1,world_weather(),now_ms(),now_ms()<manual_until)){
   active_encounter=-1;if(encounter_button)lv_obj_add_flag(encounter_button,LV_OBJ_FLAG_HIDDEN);
   next_encounter_scan=now_ms()+30000;const bool pet_saved=save(true);
   if(pet_saved&&forest_storage_ok&&forest.sync_encounters(model.state.encounter_total))forest_save(true);
   say(encounter==0?"收集露珠 · 亲密+1 · 金币+1":encounter==1?"看见彩虹 · 亲密+1 · 金币+3":"追到萤火虫 · 亲密+1 · 金币+2");
  }else{active_encounter=-1;if(encounter_button)lv_obj_add_flag(encounter_button,LV_OBJ_FLAG_HIDDEN);say("小偶遇已经过去，下次再来吧");}
  break;
 }
 case Shop:shop=true;open(Page::Bag);break;case Backpack:shop=false;open(Page::Bag);break;
 case AutoLife:model.state.auto_life=!model.state.auto_life;model.mark_dirty();save(true);open(Page::Settings);break;case AutoWorld:model.set_world_auto(!model.state.auto_world);save(true);open(Page::Settings);break;case Sound:model.state.sound=!model.state.sound;ch_hw::set_sound_enabled(model.state.sound,button_command_active&&!model.state.sound);model.mark_dirty();save(true);open(Page::Settings);break;case Idle:model.state.idle_minutes=model.state.idle_minutes?0:1;model.mark_dirty();save(true);open(Page::Settings);break;
 case ClearPoop:if(model.clear_poop(now_ms())){open(Page::Clean);say("便便清理完成，不消耗泡泡皂");save(true);}else say("现在没有便便要清理");break;
 case Treat:if(model.medicine(now_ms())){open(Page::Status);say("已用药，吃好休息好才能康复");save(true);}else say(model.state.treatment_ms?"康复中，请补充食物、清洁和睡眠":!model.state.sick?"健康时不需要药水":model.state.inventory[static_cast<int>(ch::Item::Medicine)]==0?"药水不足，可去背包补给商店":model.state.sleeping?"先叫醒卷卷服药，再让它休息":"先等捕食或游戏结束");break;
 case NeedCare:{const auto needs=model.needs();if(needs&ch::NeedMedicine)open(Page::Status);else if(needs&ch::NeedFood)open(Page::Feed);else if(needs&ch::NeedClean)open(Page::Clean);else if(needs&ch::NeedRest){if(model.state.sleeping)open(Page::Status);else command(Sleep);}else if(needs&ch::NeedCompany)command(Pet);else open(Page::Status);break;}
 case ClaimDaily:if(model.claim_daily(now_ms())){save(true);open(Page::Care);say("今天照顾完成，获得 10 金币");}else say(!model.day_verified()?"请先在网络与校时中设置日期":model.state.daily_claimed?"今天的奖励已经领取":"先完成喂食、清洁和玩耍");break;case Provision:ch_hw::set_provisioning(!hw.provisioning);say("热点状态正在切换");break;case RefreshWeather:ch_hw::refresh_weather();say("正在获取城市天气");break;default:break;
 }
}
ch::Weather world_weather(){if(!model.state.auto_world||!hw.weather_valid)return ch::Weather::Unknown;int c=hw.weather_code;if(c>=95)return ch::Weather::Storm;if((c>=71&&c<=77)||(c>=85&&c<=86))return ch::Weather::Snow;if((c>=51&&c<=67)||(c>=80&&c<=82))return ch::Weather::Rain;if(c==45||c==48)return ch::Weather::Fog;if(c>=1&&c<=3)return ch::Weather::Cloudy;if(c==0)return ch::Weather::Clear;return ch::Weather::Unknown;}
const char*weather_name(ch::Weather w){switch(w){case ch::Weather::Clear:return "晴";case ch::Weather::Cloudy:return "阴";case ch::Weather::Rain:return "雨";case ch::Weather::Storm:return "雷雨";case ch::Weather::Fog:return "雾";case ch::Weather::Snow:return "雪";default:return "天气待获取";}}
void encounter_draw(uint64_t now){
 if(page!=Page::Home||!encounter_button)return;
 const auto weather=world_weather();const int hour=hw.time_valid?hw.hour:-1;
 const auto available=[&](int kind){return !screen_dimmed&&model.encounter_available(static_cast<ch::Encounter>(kind),hour,weather,now,now<manual_until||game_capture_active(now));};
 if(active_encounter>=0){
  if(now>=encounter_expires||!available(active_encounter)){active_encounter=-1;lv_obj_add_flag(encounter_button,LV_OBJ_FLAG_HIDDEN);}
  else return;
 }
 if(now<next_encounter_scan)return;
 next_encounter_scan=now+30000;
 if(screen_dimmed||input_idle_ms(now)<20000)return;
 int choices[3],count=0;for(int i=0;i<3;++i)if(available(i))choices[count++]=i;
 if(!count)return;
 active_encounter=choices[esp_random()%static_cast<unsigned>(count)];encounter_expires=now+45000;
 lv_obj_clean(encounter_button);
 label(encounter_button,ch::encounter_name(static_cast<ch::Encounter>(active_encounter)),31,3,116);
 const char *pattern=active_encounter==0?"00100/01110/11111/11111/01110":active_encounter==1?"0011100/0122210/1233321/2300032": "01010/01110/11211/01110/00100";
 const uint32_t colors[4]={0,active_encounter==0?0x79b7ceu:active_encounter==1?0xef6f8bu:0x709959u,active_encounter==1?0xefbc47u:0xefdf79u,active_encounter==1?0x78bc72u:0xc4eb88u};
 int x=0,y=0;for(const char *pixel=pattern;*pixel;++pixel){if(*pixel=='/'){x=0;++y;continue;}const int value=*pixel-'0';if(value)box(encounter_button,7+x*3,5+y*3,3,3,colors[value]);++x;}
 lv_obj_remove_flag(encounter_button,LV_OBJ_FLAG_HIDDEN);
}
void forest_attention(uint64_t now){
 if(!wish_button)return;
 const bool visible=forest_storage_ok&&forest.date_ready()&&forest.active_wish()>=0&&active_encounter<0&&!screen_dimmed&&!alarm.ringing()&&!model.state.sleeping&&!model.state.sick&&!model.busy(now)&&!game_capture_active(now)&&now>=manual_until;
 if(visible){if(lv_obj_has_flag(wish_button,LV_OBJ_FLAG_HIDDEN))lv_obj_remove_flag(wish_button,LV_OBJ_FLAG_HIDDEN);}
 else if(!lv_obj_has_flag(wish_button,LV_OBJ_FLAG_HIDDEN))lv_obj_add_flag(wish_button,LV_OBJ_FLAG_HIDDEN);
}
void forest_visible(lv_obj_t *object,bool visible){
 if(visible){if(lv_obj_has_flag(object,LV_OBJ_FLAG_HIDDEN))lv_obj_remove_flag(object,LV_OBJ_FLAG_HIDDEN);}
 else if(!lv_obj_has_flag(object,LV_OBJ_FLAG_HIDDEN))lv_obj_add_flag(object,LV_OBJ_FLAG_HIDDEN);
}
void forest_move(lv_obj_t *object,int x,int y){if(lv_obj_get_x(object)!=x||lv_obj_get_y(object)!=y)lv_obj_set_pos(object,x,y);}
void forest_draw(uint64_t now,ch::Weather weather){
 const bool perch=rendered_scene==ch::Scene::Forest||rendered_scene==ch::Scene::Night;
 forest_move(forest_decoration[1],339,perch?273:307);
 for(int i=0;i<3;++i)forest_visible(forest_decoration[i],forest_storage_ok&&forest.state.equipped==i+1);
 const bool season_enabled=forest_storage_ok&&forest.date_ready()&&model.state.auto_world&&(weather==ch::Weather::Unknown||weather==ch::Weather::Clear||weather==ch::Weather::Cloudy);
 const unsigned season=static_cast<unsigned>(ch_forest::season_for_date(forest.state.day));
 if(forest_last_season!=season){
  const uint32_t colors[]={0xf59cb8,0xe5e979,0xe9a15e,0xd1edf1};
  for(auto *particle:season_particles)for(int i=0;i<2;++i)lv_obj_set_style_bg_color(lv_obj_get_child(particle,i),lv_color_hex(i?colors[season]:0xfff3d1),0);
  forest_last_season=season;
 }
 for(int i=0;i<6;++i){
  auto *particle=season_particles[i];forest_visible(particle,season_enabled);
  if(season_enabled)forest_move(particle,105+(i*43+static_cast<unsigned>(now/850))%250,145+(i*31+static_cast<unsigned>(now/330))%170);
 }
 const auto holiday=forest.date_ready()?ch_forest::holiday_for_date(forest.state.day):ch_forest::Holiday::None;
 const bool holiday_enabled=forest_storage_ok&&((model.state.auto_world&&holiday!=ch_forest::Holiday::None&&season_enabled)||now<forest_celebrate_until);
 for(int i=0;i<8;++i){
  forest_visible(holiday_particles[i],holiday_enabled);
  if(holiday_enabled)forest_move(holiday_particles[i],110+(i*29+static_cast<unsigned>(now/700))%245,138+(i*37+static_cast<unsigned>(now/200))%180);
 }
}
void background_style(lv_obj_t *obj,uint32_t color,lv_opa_t opacity){
 // LVGL invalidates even when assigning an identical local style. In
 // particular, updating the full-screen shade every tick forced full redraws.
 const auto value=lv_color_hex(color);
 if(!lv_color_eq(lv_obj_get_style_bg_color(obj,LV_PART_MAIN),value))lv_obj_set_style_bg_color(obj,value,0);
 if(lv_obj_get_style_bg_opa(obj,LV_PART_MAIN)!=opacity)lv_obj_set_style_bg_opa(obj,opacity,0);
}
void environment_draw(uint64_t now,ch::Weather weather){const bool night=rendered_scene==ch::Scene::Night||rendered_scene==ch::Scene::MeadowNight;uint32_t tint=0x294f68;int opacity=0;if(model.state.auto_world&&hw.time_valid){auto phase=ch::phase_for_hour(hw.hour);if(phase==ch::Phase::Dusk){tint=0xe4ae6c;opacity=27;}if(phase==ch::Phase::Night&&!night)opacity=105;if(phase==ch::Phase::Dawn){tint=0xd0e4dc;opacity=27;}}if(weather==ch::Weather::Cloudy)opacity+=20;if(weather==ch::Weather::Rain)opacity+=30;if(weather==ch::Weather::Storm)opacity+=58;background_style(shade,tint,std::min(160,opacity));for(int i=0;i<18;++i){auto*p=particles[i];if(weather==ch::Weather::Rain||weather==ch::Weather::Storm){lv_obj_set_size(p,2,weather==ch::Weather::Storm?11:7);lv_obj_set_pos(p,(i*83+static_cast<int>(now/70))%466,(i*71+static_cast<int>(now/(weather==ch::Weather::Storm?5:8)))%466);background_style(p,0xb7dae3,155);}else if(weather==ch::Weather::Snow){lv_obj_set_size(p,4,4);lv_obj_set_pos(p,(i*79+static_cast<int>(now/200))%466,(i*53+static_cast<int>(now/35))%466);background_style(p,0xf3f4e4,210);}else if(weather==ch::Weather::Fog){lv_obj_set_size(p,92,6);lv_obj_set_pos(p,(i*117+static_cast<int>(now/210))%466,154+(i%6)*31);background_style(p,0xd5e4d7,36);}else if(night){lv_obj_set_size(p,2,2);lv_obj_set_pos(p,(i*101+static_cast<int>(now/550))%466,194+(i*27)%176);background_style(p,0xe6eab0,i%2?130:75);}else if(lv_obj_get_style_bg_opa(p,LV_PART_MAIN)!=0)lv_obj_set_style_bg_opa(p,0,0);}}
int dropping_foot_baseline(){
 const auto *alpha=ch_poop->data+static_cast<size_t>(ch_poop->header.stride)*ch_poop->header.h;
 const int stride=ch_poop->header.stride/2;
 for(int row=ch_poop->header.h-1;row>=0;--row)for(int col=0;col<ch_poop->header.w;++col)if(alpha[row*stride+col])return row+1;
 return ch_poop->header.h;
}
void place_dropping(int index){
 // The two branch backgrounds have different moss / wood contours. Register
 // the actual opaque foot with each local surface, away from the grown feet.
 if(rendered_scene==ch::Scene::Forest||rendered_scene==ch::Scene::Night){
  const int branch_x[3]={84,330,358};
  const int day_surface[3]={273,273,277},night_surface[3]={284,288,291};
  const auto *surface=rendered_scene==ch::Scene::Forest?day_surface:night_surface;
  static const int foot=dropping_foot_baseline();
  lv_obj_set_pos(droppings[index],branch_x[index],surface[index]-foot);
 }else{
  const int ground_x[3]={104,289,323};lv_obj_set_pos(droppings[index],ground_x[index],316);
 }
}
void draw_pet(uint64_t now){
 if(page==Page::GrowthMoment){
  lv_obj_add_flag(face,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(tongue,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(bug,LV_OBJ_FLAG_HIDDEN);
  if(pet_heart)lv_obj_add_flag(pet_heart,LV_OBJ_FLAG_HIDDEN);
  const uint64_t party_elapsed=now>=growth_shown_at?now-growth_shown_at:0;
  for(int i=0;i<24;++i)if(growth_confetti[i]){
   const int drift=(static_cast<int>(party_elapsed/150)+i)%7-3;
   const int drop=(i*83+20+static_cast<int>(party_elapsed/24)*(1+i%3))%480-12;
   lv_obj_set_pos(growth_confetti[i],12+(i*67+31)%440+drift*2,drop);
  }
  const int stages[2]={growth_from,growth_to};
  for(int i=0;i<2;++i)if(growth_forms[i]){
   const uint64_t elapsed=party_elapsed;const bool greet=i==1&&elapsed<1600;
   const auto &animation=ch_stages[stages[i]].animations[greet?CH_HAPPY:CH_IDLE];
   const int frame=greet?std::min(animation.count-1,static_cast<int>(elapsed*animation.count/1600)):static_cast<int>(elapsed*animation.fps/1000%animation.count);
   if(lv_image_get_src(growth_forms[i])!=animation.frames[frame])lv_image_set_src(growth_forms[i],animation.frames[frame]);
   lv_obj_set_pos(growth_forms[i],11,141-static_cast<int>(std::lround(128*animation.foot_y[frame])));
  }
  for(int i=0;i<6;++i)if(growth_sparkles[i]){const lv_opa_t opacity=(now/260+i)%3?210:65;if(lv_obj_get_style_bg_opa(growth_sparkles[i],LV_PART_MAIN)!=opacity)lv_obj_set_style_bg_opa(growth_sparkles[i],opacity,0);}
  return;
 }
 const bool game_catching=game_capture_active(now),catching=model.feeding()||game_catching;
 const uint64_t capture_start=model.feeding()?model.feed_start_time():game_capture_start;
 ch::Pose pose=ch::Pose::Idle;uint64_t start=0;uint32_t duration=0;bool finite=false;
 if(page==Page::Anniversary){
  const auto elapsed=now>=anniversary_started?now-anniversary_started:0;
  for(int i=0;i<24;++i)if(growth_confetti[i])lv_obj_set_pos(growth_confetti[i],12+(i*67+31)%440+((static_cast<int>(elapsed/150)+i)%7-3)*2,(i*83+20+static_cast<int>(elapsed/24)*(1+i%3))%480-12);
  pose=elapsed<1600?ch::Pose::Happy:ch::Pose::Idle;start=anniversary_started;duration=1600;finite=elapsed<1600;
 }
 else if(model.state.sleeping||page==Page::Charging)pose=ch::Pose::Sleep;
 else if(catching){pose=ch::Pose::Eat;start=capture_start;duration=ch::kFeedDurationMs;finite=true;}
 else if(page==Page::Game&&model.game_running()&&game_session.kind()==ch::games::Kind::Mimic&&game_session.phase()==ch::games::Phase::Reveal){pose=game_session.demo_pose(now);start=game_session.demo_started(now);duration=game_session.demo_duration();finite=pose!=ch::Pose::Idle;}
 else if(now<manual_until){pose=manual_pose;start=manual_start;duration=manual_until-manual_start;finite=true;}
 else if(model.reaction_until()>now){pose=model.reaction_pose();start=model.reaction_until()>1600?model.reaction_until()-1600:0;duration=1600;finite=true;}
 else if(model.state.sick)pose=ch::Pose::Sick;else if(model.starving()||model.state.mood<35)pose=ch::Pose::Angry;
 else if(living.has_action){pose=living.pose;start=living.start;duration=living.duration;finite=pose!=ch::Pose::Grip&&pose!=ch::Pose::Shelter;}
 else if(now%6900>=6500){pose=ch::Pose::Blink;start=now-now%6900+6500;duration=400;finite=true;}
 const int stage=game_catching?game_capture_stage:model.feeding()&&feed_visual_stage>=0?feed_visual_stage:static_cast<int>(model.stage());const auto &resources=ch_stages[stage];
 if(!resources.animations[static_cast<int>(pose)].count)pose=ch::Pose::Idle;
 const auto &a=resources.animations[static_cast<int>(pose)];float progress=duration?std::clamp(static_cast<float>(now-start)/duration,0.f,.999f):0;const int index=a.loop?static_cast<int>((now-start)*a.fps/1000%a.count):finite?std::min(a.count-1,static_cast<int>(progress*a.count)):0;
 const bool small=page==Page::Clock||page==Page::Weather||page==Page::Charging;
 const int size=small?resources.widget_size:resources.pet_size;
 // The celebration uses a centered adult and its own baseline. Even the
 // complete 265px happy-frame box stays inside the round display.
 const bool celebration=page==Page::Anniversary;
 const int base=celebration?296:rendered_scene==ch::Scene::Forest?280:rendered_scene==ch::Scene::Night?296:315;
 const int x=static_cast<int>(std::round((233-size*.5+(pose==ch::Pose::Sleep||celebration?0:living.x))/2)*2),y=static_cast<int>(std::round(base-size*a.foot_y[pose==ch::Pose::Sleep?0:index]));
 if(page==Page::Home&&date_label){
  // The full-size adult leaf reaches the clock strip on the daytime branch.
  // Keep time readable with a shorter line for the whole shelter action;
  // changing it per frame would make the header pulse during the loop.
  const bool adult_leaf=stage==CH_STAGE_ADULT&&pose==ch::Pose::Shelter;
  const lv_font_t *clock_font=adult_leaf&&base==280?&lv_font_montserrat_20:&lv_font_montserrat_28;
  if(time_label&&lv_obj_get_style_text_font(time_label,LV_PART_MAIN)!=clock_font)lv_obj_set_style_text_font(time_label,clock_font,0);
  // Approved tall poses briefly use the subtitle strip. Keep their full crest
  // visible without shrinking the art or moving its feet off the branch.
  const bool teen_raised=stage==CH_STAGE_TEEN&&base==280&&(pose==ch::Pose::Happy||pose==ch::Pose::Dizzy||pose==ch::Pose::Angry||pose==ch::Pose::Pet||pose==ch::Pose::Shelter);
  const bool adult_raised=stage==CH_STAGE_ADULT&&((base==280&&pose!=ch::Pose::Idle&&pose!=ch::Pose::Sleep&&pose!=ch::Pose::Walk&&pose!=ch::Pose::Stretch)||(base==296&&(pose==ch::Pose::Happy||pose==ch::Pose::Shelter)));
  if(teen_raised||adult_raised)lv_obj_add_flag(date_label,LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(date_label,LV_OBJ_FLAG_HIDDEN);
 }
 const auto tone=pose==ch::Pose::Sleep?ch_mood::Tone::Original:ch_mood::choose(model.state);
 if(current_stage!=stage||current_pose!=pose||current_frame!=index||current_tone!=tone){lv_image_set_src(face,ch_mood::frame(a.frames[index],tone,stage));current_stage=stage;current_pose=pose;current_frame=index;current_tone=tone;}lv_image_set_scale(face,size*2);lv_obj_set_pos(face,x,y);
 lv_obj_remove_flag(face,LV_OBJ_FLAG_HIDDEN);
 if(pose==ch::Pose::Pet&&(page==Page::Home))pet_heart_draw(a.frames[index],x,y,size,now);
 else if(pose==ch::Pose::Happy&&page==Page::Game&&model.state.affinity>=80)pet_heart_draw(a.frames[index],x,y,size,now);
 else if(pet_heart)lv_obj_add_flag(pet_heart,LV_OBJ_FLAG_HIDDEN);
 // Breathing comes only from the gentle source frames. A fixed baseline and
 // constant scale prevent anchor corrections from rocking the sleeping pet.
 for(int i=0;i<4;++i){
  if(pose==ch::Pose::Sleep){const int drift=static_cast<int>((now/700+i)%3);lv_obj_set_pos(sleep_marks[i],x+static_cast<int>(size*.80)+i*18,base-static_cast<int>(size*.46)-i*11-drift);lv_obj_set_style_opa(sleep_marks[i],i==2?155:230,0);lv_obj_remove_flag(sleep_marks[i],LV_OBJ_FLAG_HIDDEN);}
  else lv_obj_add_flag(sleep_marks[i],LV_OBJ_FLAG_HIDDEN);
 }
 for(int i=0;i<3;++i){
  if(i<model.state.poop_count){place_dropping(i);lv_obj_remove_flag(droppings[i],LV_OBJ_FLAG_HIDDEN);}
  else lv_obj_add_flag(droppings[i],LV_OBJ_FLAG_HIDDEN);
 }

 // The side buttons occupy the space where the tongue catches its insect.
 // Keep the short feeding sequence completely visible, then restore controls.
 for(auto *control:care_buttons)if(control){if(catching)lv_obj_add_flag(control,LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(control,LV_OBJ_FLAG_HIDDEN);}

 lv_obj_add_flag(tongue,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(bug,LV_OBJ_FLAG_HIDDEN);
 if(catching){
  const float mx=a.mouth_x[index],my=a.mouth_y[index];const float p=std::clamp(static_cast<float>(now-capture_start)/ch::kFeedDurationMs,0.f,1.f);const float mouth_x=x+size*(mx>=0?mx:.84f),mouth_y=y+size*(my>=0?my:.60f);
  int anchor=0;while(anchor+1<a.count&&a.mouth_x[anchor]<0)++anchor;
  const float target_x=game_catching?game_capture_x:std::min(408.f,x+size*a.mouth_x[anchor]+64),target_y=game_catching?game_capture_y:std::round(base-size*a.foot_y[anchor])+size*a.mouth_y[anchor]-22;
  float extension=0;if(p>=.27f&&p<.43f)extension=(p-.27f)/.16f;else if(p>=.43f&&p<.60f)extension=1-(p-.43f)/.17f;
  if(mx>=0&&my>=0&&extension>0){static lv_point_precise_t points[2];points[0]={static_cast<lv_value_precise_t>(std::lround(mouth_x)),static_cast<lv_value_precise_t>(std::lround(mouth_y))};points[1]={static_cast<lv_value_precise_t>(std::lround(mouth_x+(target_x-mouth_x)*extension)),static_cast<lv_value_precise_t>(std::lround(mouth_y+(target_y-mouth_y)*extension))};lv_line_set_points(tongue,points,2);lv_obj_remove_flag(tongue,LV_OBJ_FLAG_HIDDEN);}
  if(p<.60f){const float bx=p<.43f?target_x:mouth_x+(target_x-mouth_x)*extension,by=p<.43f?target_y:mouth_y+(target_y-mouth_y)*extension;lv_obj_set_pos(bug,static_cast<int>(bx)-11,static_cast<int>(by)-8);lv_obj_remove_flag(bug,LV_OBJ_FLAG_HIDDEN);}
 }
}
const char *need_summary(){
 const auto needs=model.needs();
 if(needs&ch::NeedMedicine){if(model.state.treatment_ms)return "康复中 · 吃好、清洁、休息";return "生病了 · 去状态页治疗";}
 if(model.starving())return "很饿了 · 快捉只小虫";
 if(needs&ch::NeedFood)return "肚子饿了 · 喂点小虫吧";
 if(needs&ch::NeedClean)return model.state.poop_count?"有便便了 · 去清理一下":"有点脏了 · 洗个澡吧";
 if(needs&ch::NeedRest)return model.state.sleeping?"正在补充精力":"困了 · 该休息一下";
 if(needs&ch::NeedCompany)return "想你了 · 摸摸小脑袋";
 return "今天也在好好生活";
}
void refresh(){
 if(battery_label){if(hw.battery_valid)set_label_text_fmt(battery_label,hw.charging?"充电 %d%%":"电量 %d%%",hw.battery_percent);else set_label_text(battery_label,"电量未知");}
 const int values[]={model.state.health,model.state.fullness,model.state.energy,model.state.mood,model.state.cleanliness};
 for(int i=0;i<5;++i){if(stat_labels[i])set_label_text_fmt(stat_labels[i],page==Page::Status?"%d/100":"%d",values[i]);if(bars[i])lv_bar_set_value(bars[i],values[i],LV_ANIM_OFF);}
 if(time_label){if(hw.time_valid)set_label_text_fmt(time_label,"%02d:%02d",hw.hour,hw.minute);else set_label_text(time_label,"--:--");}
 if(date_label&&page==Page::Home){const auto *stage=ch_stages[static_cast<int>(model.stage())].label;if(hw.time_valid)set_label_text_fmt(date_label,"%d月%d日 · %s · %s",hw.month,hw.day,stage,weather_name(world_weather()));else set_label_text_fmt(date_label,"未校时 · %s · %s",stage,weather_name(world_weather()));}
 else if(date_label&&page==Page::Clock){if(hw.time_valid)set_label_text_fmt(date_label,"%d月%d日",hw.month,hw.day);else set_label_text(date_label,"网络与校时中设置时间");}
 if(life_label&&(page==Page::Home||page==Page::Clock))set_label_text_fmt(life_label,"%s · %s · %s",ch_stages[static_cast<int>(model.stage())].label,model.state.sleeping?"休息中":pose_names[static_cast<int>(current_pose)],weather_name(world_weather()));
 if(sleep_label)set_label_text(sleep_label,model.state.sleeping?"叫醒":"睡觉");
 if(need_label)set_label_text(need_label,need_summary());
 if(page==Page::Bond){
  if(bond_bar)lv_bar_set_value(bond_bar,model.state.affinity,LV_ANIM_OFF);
  for(int i=0;i<2;++i)if(anniversary_cards[i])set_label_text_fmt(anniversary_cards[i],"%d天纪念\n%s",i?30:21,model.state.anniversary_claimed&(1<<i)?"已庆祝":"未到期");
 }
 if(page==Page::Care){
  const char*names[]={"喂食一次","清便便一次","玩耍一次"};
  for(int i=0;i<3;++i)if(daily_labels[i])set_label_text_fmt(daily_labels[i],"%s · %s",names[i],model.state.daily_mask&(1<<i)?"完成":"待完成");
  if(daily_claim_label)set_label_text(daily_claim_label,model.state.daily_claimed?"今天已领取":model.daily_ready()?"领取 10 金币":"完成三项领取奖励");
  const auto badges=model.badge_mask();
  if(badge_label)set_label_text_fmt(badge_label,"累计照顾 %lu 次",static_cast<unsigned long>(model.state.care_total));
  const int milestones[]={10,50,100};for(int i=0;i<3;++i)if(badge_labels[i]){set_label_text_fmt(badge_labels[i],"%d次\n%s",milestones[i],badges&(1<<i)?"已获":"未获");lv_obj_set_style_text_color(badge_labels[i],lv_color_hex(badges&(1<<i)?GREEN:INK),0);}
 }
 if(forest_page()){forest_refresh();return;}
 if(page==Page::Alarm){alarm_refresh();return;}
 if(!info)return;
 switch(page){
  case Page::Bond:set_label_text_fmt(info,"亲密 %u/100 · %s\n陪伴 %lu天%lu小时",static_cast<unsigned>(model.state.affinity),ch::affinity_name(model.state.affinity),static_cast<unsigned long>(model.companion_days()),static_cast<unsigned long>(model.state.companion_ms/3600000%24));break;
  case Page::Growth:{
   const auto stage=model.stage();const bool adult=stage==ch::GrowthStage::Adult;
   const uint32_t teen=model.state.growth_speed==ch::GrowthSpeed::Fast?ch::kGrowthFastTeenMs:ch::kGrowthTeenMs;
   const uint32_t end=model.state.growth_speed==ch::GrowthSpeed::Fast?ch::kGrowthFastAdultMs:ch::kGrowthAdultMs;
   const uint32_t start=stage==ch::GrowthStage::Baby?0:teen,target=stage==ch::GrowthStage::Baby?teen:end;
   const unsigned progress=adult?100:static_cast<unsigned>(static_cast<uint64_t>(model.state.growth_ms-start)*100/(target-start));
   if(growth_bar)lv_bar_set_value(growth_bar,progress,LV_ANIM_OFF);
   const uint32_t minutes=(model.growth_remaining_ms()+59999)/60000;
   if(adult)set_label_text_fmt(info,"当前：成年\n成长完成，继续陪它生活");
   else set_label_text_fmt(info,"当前：%s · %u%%\n到%s还需 %lu天%lu小时%lu分\n%s",ch_stages[static_cast<int>(stage)].label,progress,stage==ch::GrowthStage::Baby?"少年":"成年",static_cast<unsigned long>(minutes/1440),static_cast<unsigned long>(minutes/60%24),static_cast<unsigned long>(minutes%60),model.growth_paused()?"暂停成长 · 先照顾好它":"正在成长 · 好好生活");
   break;
  }
  case Page::Status:
   if(model.state.sick)set_label_text_fmt(info,"%s · %s\n%s",ch::sickness_name(model.state.sickness_cause),model.state.treatment_ms?"康复中":"需要药水",need_summary());
   else set_label_text_fmt(info,"%s\n抚摸 %lu · 捕食 %lu",need_summary(),static_cast<unsigned long>(model.state.strokes),static_cast<unsigned long>(model.state.meals));
   break;
  case Page::Bag:set_label_text_fmt(info,"%d 金币",model.state.coins);break;
  case Page::Clean:set_label_text_fmt(info,"清洁 %d/100 · 便便 %u 份\n泡泡皂 ×%d · 清便便不耗道具",model.state.cleanliness,static_cast<unsigned>(model.state.poop_count),model.state.inventory[static_cast<int>(ch::Item::Soap)]);break;
  case Page::Care:if(hw.time_valid)set_label_text_fmt(info,"%d月%d日 · 每天的小约定",hw.month,hw.day);else set_label_text(info,"先校准日期，再开启每日奖励");break;
  case Page::Network:set_label_text(info,hw.provisioning?"设置热点已开启":hw.wifi_connected?"Wi-Fi 已连接":hw.message);break;
  case Page::Game:game_draw(now_ms());break;
  case Page::Weather:set_label_text(weather_city_label,hw.weather_city);if(hw.weather_valid){set_label_text_fmt(info,"%.0f°C",hw.temp_c);set_label_text(date_label,hw.weather);set_label_text(life_label,"Open-Meteo 天气模型");}else{set_label_text(info,"--°C");set_label_text(date_label,"天气暂未获取");set_label_text(life_label,hw.wifi_connected?"可点击刷新重试":"在设置中连接 Wi-Fi");}break;
  case Page::Charging:if(hw.battery_valid){set_label_text_fmt(info,"%d%%",hw.battery_percent);set_label_text(date_label,hw.charging?"正在充电":"电池电量");}else{set_label_text(info,"--%");set_label_text(date_label,"未检测到电池");}break;
  default:break;
 }
}
void screen_off(){
 screen_dimmed=true;ambient_dimmed=false;ball_dragging=false;ch_hw::set_brightness(0);
 if(cover)lv_obj_remove_flag(cover,LV_OBJ_FLAG_HIDDEN);
 save(true);if(forest.dirty()||forest.state.forage_wait_ms||forest.state.active_ms)forest_save(true);
 if(ui_timer){lv_timer_set_period(ui_timer,kUiStandbyMs);lv_timer_reset(ui_timer);}
 chameleon_display_set_screen_off(true);
}
void idle_brightness(uint64_t now){
 if(screen_dimmed)return;
 const bool dim=!alarm.ringing()&&input_idle_ms(now)>=kAmbientDimDelayMs&&!model.feeding()&&!model.game_running()&&!game_capture_active(now)&&page!=Page::GrowthMoment&&page!=Page::Anniversary;
 if(dim==ambient_dimmed)return;
 ambient_dimmed=dim;
 ch_hw::set_brightness(dim?std::max(10,model.state.brightness/2):model.state.brightness);
}
void physical_keys(){
 while(hw.pwr_presses!=seen_pwr){++seen_pwr;sound(ch_hw::Sound::Tap);if(alarm.ringing()){alarm.stop(now_ms());alarm_close();wake_screen();}else if(screen_dimmed)wake_screen();else screen_off();}
 while(hw.boot_presses!=seen_boot){++seen_boot;sound(ch_hw::Sound::Tap);if(alarm.ringing()){alarm.stop(now_ms());alarm_close();wake_screen();}else if(screen_dimmed)wake_screen();else{touch();open(page==Page::Home?Page::Menu:Page::Home);}}
}
void tick(lv_timer_t*){
 const uint64_t now=now_ms();hw=ch_hw::snapshot();physical_keys();
 // A delayed handler must not silently discard powered-on care/growth time.
 // The model accepts up to six hours per integration step; no sleep/offline
 // catch-up is introduced because this is the current boot's monotonic clock.
 const uint32_t elapsed=static_cast<uint32_t>(std::min<uint64_t>(6ULL*60*60*1000,now>=last_tick?now-last_tick:0));last_tick=std::max(last_tick,now);
 const bool was_feed=model.feeding(),was_game=model.game_running();const auto previous_stage=model.stage();
 const uint32_t real_day=hw.time_valid?static_cast<uint32_t>(hw.year*10000+hw.month*100+hw.day):0;
 model.sync_day(real_day);if(forest_storage_ok)forest.sync_date(real_day);model.tick(elapsed);
 const bool grew=model.stage()!=previous_stage;
 if(grew){growth_from=static_cast<int>(previous_stage);growth_to=static_cast<int>(model.stage());growth_pending=true;save(true);}
 if(was_feed&&model.feed_finish(now)){feed_visual_stage=-1;sound(ch_hw::Sound::Feed);manual_until=0;const auto previous=forest.state;bool complete=forest_storage_ok&&forest.food(model.state.meals,static_cast<uint8_t>(model.feed_item()));save(true);if(forest_storage_ok&&forest.dirty()&&!forest_save(true)){forest.restore(previous);complete=false;}say(complete?"愿望完成 · 收获森林材料":"捉到了，吃得很满足");}
 if(was_game&&!model.game_running()){forest_game_finish(now);game_session.finish();ball_dragging=false;game_finish_pending=true;save(true);}
 if(page==Page::Game&&model.game_running()){
  game_session.tick(now);game_observe_result(now);
  if(game_session.kind()==ch::games::Kind::Ball&&game_session.phase()==ch::games::Phase::Input&&ball_reaction_step){manual_until=0;ball_reaction_step=0;}
  if(game_session.kind()==ch::games::Kind::Ball&&game_session.phase()==ch::games::Phase::Feedback&&game_session.last_result()==ch::games::Result::Hit){
   const auto elapsed=now>=game_success_at?now-game_success_at:0;
   if(elapsed>=1100&&ball_reaction_step<2){ball_reaction_step=2;animate(ch::Pose::Happy,500);}
   else if(elapsed>=650&&ball_reaction_step<1){ball_reaction_step=1;animate(ch::Pose::Wave,450);}
  }
 }
 if(game_capture&&!game_capture_active(now)){game_capture=false;if(page==Page::Game)refresh();}
 if(game_finish_pending&&!game_capture_active(now)){const bool home=game_return_home;game_finish_pending=game_return_home=false;if(page==Page::Game)open(home?Page::Home:Page::Game);say(forest_game_wish_done?"愿望完成 · 收获森林材料":home?"奖励已结算":"时间到，奖励已结算");}
 alarm_tick(now);
 if(growth_pending&&!alarm.ringing()&&!screen_dimmed&&!model.state.sleeping&&!model.feeding()&&!model.game_running()&&!game_capture_active(now)&&page!=Page::GrowthMoment){manual_until=0;growth_pending=false;touch();open(Page::GrowthMoment);}
 const auto memorial=model.pending_anniversary();
 if(memorial&&!alarm.ringing()&&!screen_dimmed&&!model.state.sleeping&&!model.state.sick&&!model.feeding()&&!model.game_running()&&!game_capture_active(now)&&now>=manual_until&&page!=Page::GrowthMoment&&page!=Page::Anniversary){
  if(model.complete_anniversary(memorial)){anniversary_day=memorial;save(true);touch();open(Page::Anniversary);}
 }
 const auto next_scene=ch::effective_scene(model.state,hw.time_valid?hw.hour:-1);
 if(next_scene!=rendered_scene){rendered_scene=next_scene;lv_image_set_src(background,ch_backgrounds[static_cast<int>(rendered_scene)]);}
 const bool active=!alarm.ringing()&&!screen_dimmed&&(page==Page::Home||page==Page::Clock||page==Page::Weather);
 living=model.life_step(now,hw.time_valid?hw.hour:-1,world_weather(),0,rendered_scene,active,last_input,now<manual_until);
 if(!screen_dimmed){
  if(!lv_obj_has_flag(world,LV_OBJ_FLAG_HIDDEN)){environment_draw(now,world_weather());forest_draw(now,world_weather());draw_pet(now);}
  else if(page==Page::GrowthMoment)draw_pet(now);
  game_draw(now);
 }
 if(forest_storage_ok){
  const bool eligible=page==Page::Home&&!screen_dimmed&&!alarm.ringing()&&model.state.auto_life&&!model.state.sleeping&&!model.state.sick&&!model.busy(now)&&!game_capture_active(now)&&now>=manual_until;
  if(forest.tick(elapsed,eligible))forest_save(true);
 }
 encounter_draw(now);forest_attention(now);

 if(page==Page::Anniversary&&!alarm.ringing()&&!screen_dimmed&&now>=anniversary_started&&now-anniversary_started>=12000)open(Page::Home);
 const auto needs=model.needs();
 if((needs&~previous_needs)&&page==Page::Home&&!screen_dimmed&&!model.feeding()&&now>=manual_until)say(need_summary());
 previous_needs=needs;
 if(!screen_dimmed&&(grew||now-last_panel>400)){refresh();last_panel=now;}
 if(toast_obj&&now>toast_until){lv_obj_add_flag(toast_obj,LV_OBJ_FLAG_HIDDEN);if(page==Page::Home&&need_label)lv_obj_remove_flag(lv_obj_get_parent(need_label),LV_OBJ_FLAG_HIDDEN);}
 if(!alarm.ringing()&&model.state.idle_minutes>0&&input_idle_ms(now)>static_cast<uint64_t>(model.state.idle_minutes)*60000&&!model.feeding()&&!model.game_running()&&!game_capture_active(now)&&page!=Page::GrowthMoment&&page!=Page::Anniversary&&!screen_dimmed)screen_off();
 idle_brightness(now);
 ch_hw::apply_display_requests();
 save();forest_save();
 // LVGL invalidation/scroll creation may resume its timers during a model
 // step (for example at a day/night boundary). Keep rendering asleep.
 if(screen_dimmed)chameleon_display_set_screen_off(true);
}
}

void chameleon_ui_expand_dirty_area(lv_area_t *area){
 if(!face||!lv_obj_is_visible(face))return;
 // LVGL 9.5 derives nearest-neighbor steps from the clipped image span.
 // Include the complete sprite frame so a partial redraw uses the same
 // sampling grid as a full redraw. Other dirty regions stay local.
 lv_area_t sprite;lv_obj_get_coords(face,&sprite);
 sprite.x2=sprite.x1+lv_image_get_transformed_width(face)-1;
 sprite.y2=sprite.y1+lv_image_get_transformed_height(face)-1;
 if(area->x1<=sprite.x2&&area->x2>=sprite.x1&&area->y1<=sprite.y2&&area->y2>=sprite.y1){
  area->x1=std::min(area->x1,sprite.x1);area->y1=std::min(area->y1,sprite.y1);
  area->x2=std::max(area->x2,sprite.x2);area->y2=std::max(area->y2,sprite.y2);
 }
}

void chameleon_ui_process_key_events(){
 hw=ch_hw::snapshot();physical_keys();
 if(ui_timer)lv_timer_ready(ui_timer);
}

void chameleon_ui_start(bool storage_ready){
 alarm_close();ui_timer=nullptr;
 game_capture=game_finish_pending=game_return_home=growth_pending=false;game_capture_start=growth_shown_at=0;feed_visual_stage=-1;manual_until=0;anniversary_day=0;game_session.finish();game_seen_hits=game_seen_misses=0;ball_dragging=false;next_encounter_scan=now_ms()+30000;
 button_command_active=false;forest_game_recorded=true;forest_game_wish_done=false;forest_game_started=0;
 bind_touch_activity();
 screen_dimmed=ambient_dimmed=false;model.reset();load(storage_ready);alarm_load(storage_ready);audio_load(storage_ready);forest_load(storage_ready);ch_hw::set_sound_enabled(model.state.sound);model.set_seed(esp_random());last_tick=last_input=now_ms();model.reset_life(last_tick);hw=ch_hw::snapshot();seen_pwr=hw.pwr_presses;seen_boot=hw.boot_presses;forest.sync_date(hw.time_valid?static_cast<uint32_t>(hw.year*10000+hw.month*100+hw.day):0);model.sync_day(hw.time_valid?static_cast<uint32_t>(hw.year*10000+hw.month*100+hw.day):0);previous_needs=model.needs();rendered_scene=ch::effective_scene(model.state,hw.time_valid?hw.hour:-1);ch_hw::set_brightness(model.state.brightness);open(Page::Home);ui_timer=lv_timer_create(tick,kUiActiveMs,nullptr);chameleon_display_set_screen_off(false);ch_hw::apply_display_requests();ESP_LOGI("chameleon","UI ready: %s / 3 stages / %d actions per stage / %d frames; four games; free internal=%lu PSRAM=%lu",ch_stages[static_cast<int>(model.stage())].id,CH_ACTIVE_ANIM_COUNT,CH_FRAME_COUNT,static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));if(!storage_ok)say("存档不可用，原数据已保留");else if(!forest_storage_ok)say("森林记录不可用，原数据已保留");
}
