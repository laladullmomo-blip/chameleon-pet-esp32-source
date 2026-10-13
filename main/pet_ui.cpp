// SPDX-License-Identifier: MIT
// Round-screen UI for Waveshare ESP32-S3-Touch-AMOLED-1.75C.
#include "pet_ui.h"
#include "pet_state.h"
#include "pet_save.h"
#include "pet_frames.h"
#include "lvgl.h"
#include "esp_timer.h"
#include "esp_random.h"
#include <cstdint>

extern "C" { LV_FONT_DECLARE(pet_font_18); }

namespace {
constexpr uint32_t BG = 0x101D29, PANEL = 0x203341, INK = 0xEDF3E8;
constexpr uint32_t MUTED = 0x8EA5AF, ACCENT = 0xD8EC9C;
enum class Page { Home, Shop, Game };
enum class Action { Feed, Pet, Sleep, Game, Shop, Home, Snack, Medicine, Revive };
Page page = Page::Home;
lv_obj_t *face = nullptr, *meta = nullptr, *hint = nullptr, *sleep_text = nullptr;
lv_obj_t *bars[4]{}, *stat_text[4]{};
lv_obj_t *holes[3]{}, *game_info = nullptr, *game_tip = nullptr;
bool storage_ok = false, game_running = false, game_paid = false;
int game_target = -1, game_hits = 0, frame = 0;
int64_t animation_until = 0, hint_until = 0, last_save = 0, game_end = 0, target_end = 0;
pet_anim_state_t action_animation = PET_ANIM_IDLE;
const lv_font_t *font = &pet_font_18;
auto &animal() { return pet::Pet::instance(); }
int64_t now_ms() { return esp_timer_get_time() / 1000; }

void open_home();
void open_shop();
void open_game();
void refresh_home();

lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int w,
                uint32_t color = INK, const lv_font_t *f = nullptr)
{
    auto *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, w);
    lv_label_set_text(obj, text);
    lv_obj_set_style_text_font(obj, f ? f : font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    return obj;
}

lv_obj_t *button(const char *text, int x, int y, int w, int h, Action action,
                 uint32_t color = PANEL);

void reset_screen(Page next)
{
    // Timers are permanent and gate access on page; clear references before deletion.
    page = next;
    face = meta = hint = sleep_text = nullptr;
    game_info = game_tip = nullptr;
    for (int i = 0; i < 4; ++i) { bars[i] = stat_text[i] = nullptr; }
    for (auto &hole : holes) hole = nullptr;
    auto *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
}

const char *stage_name(pet::LifeStage stage)
{
    switch (stage) {
        case pet::LifeStage::Egg: return "新生";
        case pet::LifeStage::Baby: return "宝宝";
        case pet::LifeStage::Child: return "幼年";
        case pet::LifeStage::Teen: return "少年";
        case pet::LifeStage::Adult: return "成年";
        case pet::LifeStage::Tombstone: return "等待重生";
        default: return "新生";
    }
}

void say(const char *text)
{
    if (hint) lv_label_set_text(hint, text);
    hint_until = now_ms() + 2500;
}

void animate(pet_anim_state_t state)
{
    action_animation = state;
    animation_until = now_ms() + 2200;
    frame = 0;
}

void save_now()
{
    if (!storage_ok) return;
    const esp_err_t err = pet::PetSave::instance().save_if_dirty(animal());
    if (err != ESP_OK) {
        storage_ok = false;
        say("存档失败，请重启检查");
    }
    last_save = now_ms();
}

void finish_game()
{
    game_running = false;
    game_target = -1;
    if (!game_paid) {
        animal().work_outcome("ha", game_hits > 0 ? 10 : 0, game_hits * 2);
        game_paid = true;
        save_now();
    }
    for (auto *hole : holes) {
        if (hole) {
            lv_obj_set_style_bg_color(hole, lv_color_hex(PANEL), 0);
            lv_label_set_text(lv_obj_get_child(hole, 0), "结束");
            lv_obj_add_state(hole, LV_STATE_DISABLED);
        }
    }
    if (game_info) lv_label_set_text_fmt(game_info, "完成 %d 次 · 获得 %d 饼干", game_hits, game_hits * 2);
    if (game_tip) lv_label_set_text(game_tip, "带着奖励回家吧");
}

void action_cb(lv_event_t *event)
{
    const auto action = static_cast<Action>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
    if (action == Action::Home) {
        if (page == Page::Game && game_running) finish_game();
        open_home();
        return;
    }
    if (action == Action::Shop) { open_shop(); return; }
    if (action == Action::Revive) {
        animal().revive_from_tombstone();
        save_now();
        open_home();
        say("我们重新开始吧");
        return;
    }
    if (animal().stage() == pet::LifeStage::Tombstone) {
        say("点击重生，重新开始");
        return;
    }
    if (action == Action::Game) {
        if (animal().is_sleeping()) { say("先叫醒我，再一起玩"); return; }
        if (animal().get_state().energy < 10) { say("我有点累，先睡一会吧"); return; }
        open_game();
        return;
    }
    if (action == Action::Snack || action == Action::Medicine) {
        const int cost = action == Action::Snack ? 5 : 8;
        if (!animal().spend_coins(cost)) { say("饼干不够，玩游戏可以获得"); return; }
        if (action == Action::Snack) animal().feed_with_amount(35);
        else animal().take_medicine(30);
        save_now();
        if (meta) lv_label_set_text_fmt(meta, "我的饼干：%d", animal().get_state().coins);
        say(action == Action::Snack ? "吃饱啦，谢谢你" : "感觉好多了");
        return;
    }
    switch (action) {
        case Action::Feed:
            animal().feed(); animate(PET_ANIM_EATING); say("好吃！谢谢你的照顾"); break;
        case Action::Pet:
            animal().pet(); animate(PET_ANIM_HAPPY); say("最喜欢你陪着我"); break;
        case Action::Sleep:
            if (animal().is_sleeping()) { animal().wake_up(); say("睡醒了，继续一起玩"); }
            else { animal().sleep(); say("晚安，我要补充体力啦"); }
            animation_until = 0;
            break;
        default: break;
    }
    refresh_home();
}

lv_obj_t *button(const char *text, int x, int y, int w, int h, Action action, uint32_t color)
{
    auto *obj = lv_button_create(lv_screen_active());
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_radius(obj, h / 2, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    auto *text_obj = label(obj, text, 0, 0, w, color == ACCENT ? BG : INK);
    lv_obj_center(text_obj);
    lv_obj_add_event_cb(obj, action_cb, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(action)));
    return obj;
}

void refresh_home()
{
    if (page != Page::Home || !meta) return;
    const auto state = animal().get_state();
    lv_label_set_text_fmt(meta, "%s · Lv.%d · 饼干 %d", stage_name(animal().stage()), state.level, state.coins);
    const int values[] = {state.fullness, state.happiness, state.energy, state.health};
    const char *names[] = {"饱腹", "心情", "体力", "健康"};
    for (int i = 0; i < 4; ++i) {
        if (!bars[i]) continue;
        lv_bar_set_value(bars[i], values[i], LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bars[i], lv_color_hex(values[i] < 25 ? 0xF29B8F : ACCENT), LV_PART_INDICATOR);
        lv_label_set_text_fmt(stat_text[i], "%s %d", names[i], values[i]);
    }
    if (sleep_text) lv_label_set_text(sleep_text, animal().is_sleeping() ? "叫醒" : "睡觉");
    if (hint && now_ms() > hint_until) {
        if (!storage_ok) lv_label_set_text(hint, "存档不可用，请重启检查");
        else if (animal().stage() == pet::LifeStage::Tombstone) lv_label_set_text(hint, "我在这里，等你带我重生");
        else if (animal().is_sleeping()) lv_label_set_text(hint, "呼呼，体力正在慢慢恢复");
        else if (state.fullness < 25) lv_label_set_text(hint, "肚子有点饿，给我一点吃的吧");
        else if (state.energy < 15) lv_label_set_text(hint, "有点困了，我想睡一会");
        else lv_label_set_text(hint, "摸摸我，今天也一起好好生活");
    }
}

void open_home()
{
    reset_screen(Page::Home);
    label(lv_screen_active(), "口袋伙伴", 130, 37, 206, ACCENT);
    meta = label(lv_screen_active(), "", 103, 64, 260, MUTED);
    button("商店", 321, 99, 65, 30, Action::Shop);
    auto *touch_zone = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(touch_zone, 148, 99);
    lv_obj_set_size(touch_zone, 170, 151);
    lv_obj_remove_flag(touch_zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(touch_zone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(touch_zone, 0, 0);
    lv_obj_set_style_pad_all(touch_zone, 0, 0);
    lv_obj_add_event_cb(touch_zone, action_cb, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(Action::Pet)));
    // The imported sheet has stray annotation/border pixels near its margins.
    // Clip those margins in a viewport while preserving the original frame data.
    auto *viewport = lv_obj_create(touch_zone);
    lv_obj_set_pos(viewport, 22, 29);
    lv_obj_set_size(viewport, 126, 116);
    lv_obj_remove_flag(viewport, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(viewport, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(viewport, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(viewport, 0, 0);
    lv_obj_set_style_pad_all(viewport, 0, 0);
    lv_obj_set_style_radius(viewport, 50, 0);
    lv_obj_set_style_clip_corner(viewport, true, 0);
    face = lv_image_create(viewport);
    lv_image_set_src(face, pet_anim_frames[PET_ANIM_IDLE][0]);
    lv_image_set_scale(face, 384);
    lv_obj_set_pos(face, 15, -2);
    lv_obj_remove_flag(face, LV_OBJ_FLAG_CLICKABLE);
    hint = label(lv_screen_active(), "", 83, 252, 300, MUTED);
    for (int i = 0; i < 4; ++i) {
        const int x = i % 2 == 0 ? 85 : 247;
        const int y = 282 + (i / 2) * 35;
        stat_text[i] = label(lv_screen_active(), "", x, y, 134);
        bars[i] = lv_bar_create(lv_screen_active());
        lv_obj_set_pos(bars[i], x, y + 23);
        lv_obj_set_size(bars[i], 134, 5);
        lv_obj_set_style_bg_color(bars[i], lv_color_hex(PANEL), LV_PART_MAIN);
        lv_bar_set_range(bars[i], 0, 100);
    }
    if (animal().stage() == pet::LifeStage::Tombstone) {
        button("重新陪伴", 140, 361, 186, 40, Action::Revive, ACCENT);
    } else {
        button("喂食", 93, 357, 135, 38, Action::Feed, ACCENT);
        button("抚摸", 238, 357, 135, 38, Action::Pet);
        auto *sleep_button = button("睡觉", 126, 404, 104, 32, Action::Sleep);
        sleep_text = lv_obj_get_child(sleep_button, 0);
        button("游戏", 236, 404, 104, 32, Action::Game);
    }
    hint_until = 0;
    refresh_home();
}

void open_shop()
{
    reset_screen(Page::Shop);
    label(lv_screen_active(), "伙伴小商店", 108, 53, 250, ACCENT);
    meta = label(lv_screen_active(), "", 105, 91, 256, MUTED);
    lv_label_set_text_fmt(meta, "我的饼干：%d", animal().get_state().coins);
    button("营养餐 · 5 饼干", 102, 151, 262, 52, Action::Snack, ACCENT);
    label(lv_screen_active(), "饱腹 +35", 120, 214, 226, MUTED);
    button("恢复药 · 8 饼干", 102, 253, 262, 52, Action::Medicine);
    label(lv_screen_active(), "健康 +30", 120, 316, 226, MUTED);
    hint = label(lv_screen_active(), "玩游戏赚饼干，照顾小伙伴", 78, 351, 310, MUTED);
    if (!storage_ok) lv_label_set_text(hint, "存档不可用，请重启检查");
    button("回家", 161, 397, 144, 38, Action::Home);
}

void choose_target()
{
    if (!game_running) return;
    const int previous = game_target;
    game_target = static_cast<int>(esp_random() % 3);
    if (game_target == previous) game_target = (game_target + 1) % 3;
    target_end = now_ms() + 1100;
    for (int i = 0; i < 3; ++i) {
        lv_obj_set_style_bg_color(holes[i], lv_color_hex(i == game_target ? ACCENT : PANEL), 0);
        auto *text = lv_obj_get_child(holes[i], 0);
        lv_label_set_text(text, i == game_target ? "点我" : "等候");
        lv_obj_set_style_text_color(text, lv_color_hex(i == game_target ? BG : MUTED), 0);
    }
}

void hit_cb(lv_event_t *event)
{
    if (!game_running || now_ms() >= game_end) return;
    const int hit = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
    if (hit != game_target || now_ms() >= target_end) return;
    ++game_hits;
    choose_target();
    if (game_info) lv_label_set_text_fmt(game_info, "命中 %d 次 · 剩余 %lld 秒", game_hits, (game_end - now_ms() + 999) / 1000);
}

void open_game()
{
    reset_screen(Page::Game);
    animal().work_outcome("e", -5, 0);
    game_hits = 0;
    game_paid = false;
    game_running = true;
    game_target = -1;
    game_end = now_ms() + 20000;
    label(lv_screen_active(), "追光小游戏", 110, 56, 246, ACCENT);
    game_info = label(lv_screen_active(), "命中 0 次 · 剩余 20 秒", 90, 103, 286, MUTED);
    game_tip = label(lv_screen_active(), "点亮哪里，就点哪里", 90, 143, 286);
    for (int i = 0; i < 3; ++i) {
        const int x = i == 0 ? 107 : (i == 1 ? 255 : 181);
        const int y = i == 2 ? 278 : 190;
        holes[i] = button("等候", x, y, 104, 69, Action::Game);
        lv_obj_remove_event_cb(holes[i], action_cb);
        lv_obj_add_event_cb(holes[i], hit_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    }
    label(lv_screen_active(), "每次命中获得 2 饼干", 90, 360, 286, MUTED);
    button("结束回家", 148, 399, 170, 36, Action::Home);
    choose_target();
}

void tick(lv_timer_t *)
{
    animal().update();
    const int64_t now = now_ms();
    if (animal().consume_stage_changed() && page == Page::Home) open_home();
    if (page == Page::Game && animal().stage() == pet::LifeStage::Tombstone) {
        game_running = false;
        game_paid = true;
        open_home();
    }
    if (now - last_save >= 5000) save_now();
    if (page == Page::Home) refresh_home();
    if (page == Page::Game && game_running) {
        if (now >= game_end) finish_game();
        else {
            if (now >= target_end) choose_target();
            if (game_info) lv_label_set_text_fmt(game_info, "命中 %d 次 · 剩余 %lld 秒", game_hits, (game_end - now + 999) / 1000);
        }
    }
}

void animate_tick(lv_timer_t *)
{
    if (page != Page::Home || !face) return;
    pet_anim_state_t state = PET_ANIM_IDLE;
    const auto current = animal().get_state();
    if (animal().stage() == pet::LifeStage::Tombstone || current.health < 25) state = PET_ANIM_SICK;
    else if (animal().is_sleeping()) state = PET_ANIM_SLEEPING;
    else if (now_ms() < animation_until) state = action_animation;
    else if (current.happiness > 85) state = PET_ANIM_HAPPY;
    lv_image_set_src(face, pet_anim_frames[state][frame++ % 9]);
}
} // namespace

void pet_ui_start(bool storage_ready)
{
    storage_ok = storage_ready;
    last_save = now_ms();
    open_home();
    // All callbacks run on LVGL's task. There is no competing state writer.
    lv_timer_create(tick, 200, nullptr);
    lv_timer_create(animate_tick, 120, nullptr);
}
