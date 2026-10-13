// Native LVGL rendering and event tests. No device emulation or hardware assertions.
#include "lvgl.h"
#include "pet_state.h"
#include "pet_save.h"
#include "pet_ui.h"
#include "esp_timer.h"
#include "nvs.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <filesystem>

static uint64_t milliseconds = 0;
static std::map<std::string, std::vector<uint8_t>> storage;
static std::vector<uint8_t> pixels(466 * 466 * 4);
int64_t esp_timer_get_time() { return milliseconds * 1000; }
TickType_t xTaskGetTickCount() { return static_cast<TickType_t>(milliseconds); }
uint32_t esp_random() { static uint32_t n = 0; return n++; }
esp_err_t nvs_open(const char *, int, nvs_handle_t *handle) { *handle = 1; return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t, const char *key, void *data, size_t *size) {
    const auto it = storage.find(key);
    if (it == storage.end()) return ESP_ERR_NVS_NOT_FOUND;
    if (*size < it->second.size()) { *size = it->second.size(); return ESP_ERR_NVS_INVALID_LENGTH; }
    *size = it->second.size();
    std::memcpy(data, it->second.data(), *size);
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t, const char *key, const void *data, size_t size) {
    const auto *bytes = static_cast<const uint8_t *>(data);
    storage[key] = std::vector<uint8_t>(bytes, bytes + size);
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t) { return ESP_OK; }
esp_err_t nvs_erase_key(nvs_handle_t, const char *key) { storage.erase(key); return ESP_OK; }

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data) {
    const int width = lv_area_get_width(area);
    for (int y = area->y1; y <= area->y2; ++y) {
        std::memcpy(&pixels[(y * 466 + area->x1) * 4],
                    data + (y - area->y1) * width * 4, width * 4);
    }
    lv_display_flush_ready(display);
}
static void advance(unsigned ms) {
    for (unsigned t = 0; t < ms; t += 20) {
        milliseconds += 20;
        lv_tick_inc(20);
        lv_timer_handler();
    }
}
static lv_obj_t *find_text(lv_obj_t *root, const char *text) {
    if (lv_obj_check_type(root, &lv_label_class) && std::strcmp(lv_label_get_text(root), text) == 0) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
        if (auto *found = find_text(lv_obj_get_child(root, i), text)) return found;
    }
    return nullptr;
}
static void click(const char *text) {
    auto *object = find_text(lv_screen_active(), text);
    assert(object && "UI action label missing");
    lv_obj_send_event(lv_obj_get_parent(object), LV_EVENT_CLICKED, nullptr);
    advance(40);
}
static void export_ppm(const std::filesystem::path &directory, const char *name) {
    lv_refr_now(nullptr);
    FILE *file = std::fopen((directory / name).string().c_str(), "wb");
    assert(file);
    std::fprintf(file, "P6\n466 466\n255\n");
    for (size_t i = 0; i < pixels.size(); i += 4) {
        const uint8_t rgb[] = {pixels[i + 2], pixels[i + 1], pixels[i]};
        std::fwrite(rgb, 1, 3, file);
    }
    std::fclose(file);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    const std::filesystem::path output(argv[1]);
    std::filesystem::create_directories(output);
    lv_init();
    auto *display = lv_display_create(466, 466);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_ARGB8888);
    static uint8_t draw_buffer[466 * 466 * 4];
    lv_display_set_buffers(display, draw_buffer, nullptr, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush);
    auto &pet = pet::Pet::instance();
    pet.init();
    assert(pet::PetSave::instance().init() == ESP_OK);
    pet_ui_start(true);
    advance(240);
    export_ppm(output, "home.ppm");
    click("喂食"); assert(pet.get_state().fullness == 95);
    click("抚摸"); assert(pet.get_state().happiness == 85);
    click("睡觉"); assert(pet.is_sleeping());
    click("游戏"); assert(!find_text(lv_screen_active(), "追光小游戏"));
    click("叫醒"); assert(!pet.is_sleeping());
    click("游戏");
    assert(find_text(lv_screen_active(), "追光小游戏"));
    export_ppm(output, "game.ppm");
    click("点我");
    click("结束回家");
    assert(pet.get_state().coins == 2);
    advance(5000); assert(pet.get_state().coins == 2); // no duplicate completion reward
    click("游戏"); click("点我"); advance(20100);
    assert(pet.get_state().coins == 4);
    click("结束回家"); assert(pet.get_state().coins == 4);
    click("商店"); click("营养餐 · 5 饼干");
    assert(pet.get_state().coins == 4); // insufficient balance cannot buy
    pet.add_coins(10); click("回家"); click("商店");
    export_ppm(output, "shop.ppm");
    click("恢复药 · 8 饼干"); assert(pet.get_state().coins == 6);
    click("回家");
    for (int i = 0; i < 25; ++i) { click("商店"); click("回家"); }
    assert(pet.get_state().coins == 6); // repeated screen teardown remains safe
    assert(pet::PetSave::instance().save_if_dirty(pet, true) == ESP_OK);
    std::puts("Native LVGL UI checks passed: care, sleep gate, timed game, settlement, shop, page teardown.");
}
