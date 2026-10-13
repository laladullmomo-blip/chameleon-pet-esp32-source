// SPDX-License-Identifier: MIT
// Board startup follows Waveshare's 02_lvgl_demo_v9 example (Apache-2.0).
#include "bsp/esp-bsp.h"
#include "bsp/touch.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "chameleon_hardware.h"
#include "chameleon_ui.h"
#include "chameleon_display.h"
#include "chameleon_power.h"
#include "chameleon_touch_driver.h"
#include <inttypes.h>

namespace {
constexpr const char *TAG = "round_pet";
// Waveshare's 1.75C schematic connects LCD_TE to GPIO13. The BSP already
// enables the panel's TE output with command 0x35, but its default LVGL setup
// does not consume that signal.
constexpr gpio_num_t LCD_TE = GPIO_NUM_13;
constexpr size_t DRAW_BUFFER_BYTES = BSP_LCD_H_RES * BSP_LCD_V_RES * 2;
// BSP SPI IO stages PSRAM data in internal DMA memory. Limit each queued
// transaction rather than reserving a 424 KiB internal copy for a full frame.
// IDF rounds to whole 4092-byte DMA descriptors: four stay below 16 KiB.
constexpr int SPI_TRANSFER_BYTES = 4 * 4092;
constexpr uint32_t TOUCH_READ_PERIOD_MS = 10;
// The generated adapter uses esp_timer's monotonic clock directly. This field
// stays nonzero for upstream config validation; no periodic tick is created.
constexpr uint32_t LVGL_TICK_PERIOD_MS = 5;

void align_display_area(lv_event_t *event)
{
    auto *area = static_cast<lv_area_t *>(lv_event_get_param(event));
    chameleon_ui_expand_dirty_area(area);
    // CO5300 requires even starts and odd ends. Keep signed coordinates until
    // after clipping so a transformed image at an edge cannot wrap to 65535.
    area->x1 = LV_MAX(0, area->x1) & ~1;
    area->y1 = LV_MAX(0, area->y1) & ~1;
    area->x2 = LV_MIN(BSP_LCD_H_RES - 1, area->x2 | 1);
    area->y2 = LV_MIN(BSP_LCD_V_RES - 1, area->y2 | 1);
}

#if CONFIG_ESP_LVGL_ADAPTER_ENABLE_FPS_STATS
void report_display_fps(lv_timer_t *timer)
{
    auto *display = static_cast<lv_display_t *>(lv_timer_get_user_data(timer));
    uint32_t fps = 0;
    if (esp_lv_adapter_get_fps(display, &fps) == ESP_OK) {
        // These values come from completed refreshes on the running board;
        // they are not the sprite animation's intentionally lower pose FPS.
        ESP_LOGI(TAG, "Display completed refreshes: %" PRIu32 " fps; free PSRAM: %u bytes",
                 fps, static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    }
}
#endif

esp_err_t start_display(lv_display_t **out_display)
{
    esp_lv_adapter_config_t adapter_cfg = {};
    adapter_cfg.task_stack_size = ESP_LV_ADAPTER_DEFAULT_STACK_SIZE;
    adapter_cfg.task_priority = ESP_LV_ADAPTER_DEFAULT_TASK_PRIORITY;
    // Wi-Fi's fixed board configuration uses core 0. Keep input / rendering
    // on core 1 instead of migrating alongside the network task.
    adapter_cfg.task_core_id = 1;
    adapter_cfg.tick_period_ms = LVGL_TICK_PERIOD_MS;
    adapter_cfg.task_min_delay_ms = ESP_LV_ADAPTER_DEFAULT_TASK_MIN_DELAY_MS;
    adapter_cfg.task_max_delay_ms = ESP_LV_ADAPTER_DEFAULT_TASK_MAX_DELAY_MS;
    adapter_cfg.auto_sleep.mode = ESP_LV_ADAPTER_AUTO_SLEEP_MODE_DISABLED;
    adapter_cfg.auto_sleep.idle_timeout_ms = ESP_LV_ADAPTER_DEFAULT_AUTO_SLEEP_TIMEOUT_MS;
    ESP_RETURN_ON_ERROR(esp_lv_adapter_init(&adapter_cfg), TAG, "LVGL initialization failed");

    // Keep Waveshare's panel initialization, QSPI pins, 40 MHz bus, power and
    // touch mapping. Only the LVGL registration is application-controlled.
    const bsp_display_config_t panel_cfg = {.max_transfer_sz = SPI_TRANSFER_BYTES};
    esp_lcd_panel_handle_t panel = nullptr;
    esp_lcd_panel_io_handle_t io = nullptr;
    ESP_RETURN_ON_ERROR(bsp_display_new(&panel_cfg, &panel, &io), TAG, "Panel initialization failed");

    gpio_config_t te_gpio = {};
    te_gpio.pin_bit_mask = 1ULL << LCD_TE;
    te_gpio.mode = GPIO_MODE_INPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&te_gpio), TAG, "TE input initialization failed");

    esp_lv_adapter_display_config_t display_cfg = {};
    display_cfg.panel = panel;
    display_cfg.panel_io = io;
    display_cfg.profile.interface = ESP_LV_ADAPTER_PANEL_IF_OTHER;
    display_cfg.profile.rotation = ESP_LV_ADAPTER_ROTATE_0;
    display_cfg.profile.hor_res = BSP_LCD_H_RES;
    display_cfg.profile.ver_res = BSP_LCD_V_RES;
    // Reserve full-screen capacity so each dirty region fits in one transfer.
    // TE_SYNC holds the single buffer until its DMA transfer has completed.
    display_cfg.profile.buffer_height = BSP_LCD_V_RES;
    display_cfg.profile.use_psram = true;
    display_cfg.profile.require_double_buffer = false;
    display_cfg.tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TE_SYNC;
    display_cfg.te_sync.gpio_num = LCD_TE;
    display_cfg.te_sync.bus_freq_hz = 40 * 1000 * 1000;
    display_cfg.te_sync.data_lines = 4;
    display_cfg.te_sync.bits_per_pixel = BSP_LCD_BITS_PER_PIXEL;
    // Leave edge/window selection to the adapter's TE detection and timing.
    auto *display = esp_lv_adapter_register_display(&display_cfg);
    ESP_RETURN_ON_FALSE(display, ESP_FAIL, TAG, "Display registration failed");
    // The adapter defaults TE_SYNC to FULL, but its flush path accepts the
    // actual dirty area. Select PARTIAL explicitly; automatic sleep/rebinding
    // is disabled so that initialization default cannot be restored later.
    lv_display_set_render_mode(display, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_add_event_cb(display, align_display_area, LV_EVENT_INVALIDATE_AREA, nullptr);

    bsp_display_cfg_t touch_cfg = {};
    touch_cfg.touch_flags.mirror_x = 1;
    touch_cfg.touch_flags.mirror_y = 1;
    esp_lcd_touch_handle_t touch = nullptr;
    ESP_RETURN_ON_ERROR(bsp_touch_new(&touch_cfg, &touch), TAG, "Touch initialization failed");
    // Capture controller reports independently of LVGL's TE/DMA waits.
    // The sampler owns the hardware IRQ; the adapter consumes queued reports.
    ESP_RETURN_ON_ERROR(esp_lv_adapter_touch_set_default_idf_interrupt_callback_registration_enabled(false),
                        TAG, "External touch IRQ ownership failed");
    esp_lv_adapter_touch_config_t input_cfg = {};
    input_cfg.disp = display;
    input_cfg.handle = touch;
    input_cfg.scale = {1.0f, 1.0f};
    input_cfg.multi_touch.mode = ESP_LV_ADAPTER_TOUCH_MODE_SINGLE;
    input_cfg.multi_touch.pointers = 1;
    input_cfg.callbacks.custom_touch_read = chameleon_touch_read;
    auto *input = esp_lv_adapter_register_touch(&input_cfg);
    ESP_RETURN_ON_FALSE(input, ESP_FAIL, TAG, "Touch registration failed");
    ESP_RETURN_ON_ERROR(chameleon_touch_start(touch, input), TAG, "Touch sampler initialization failed");
    // EVENT input drains each queued Down/Up in order. This timer preserves
    // long presses and dragging while the sampler captures hardware reports.
    // This is before adapter_start, and the LVGL lock also protects the timer
    // if the adapter's startup ordering changes in a future implementation.
    ESP_RETURN_ON_ERROR(esp_lv_adapter_lock(-1), TAG, "Touch timer lock failed");
    auto *read_timer = lv_indev_get_read_timer(input);
    if (read_timer) lv_timer_set_period(read_timer, TOUCH_READ_PERIOD_MS);
    esp_lv_adapter_unlock();
    ESP_RETURN_ON_FALSE(read_timer, ESP_FAIL, TAG, "Touch read timer unavailable");
    ESP_RETURN_ON_ERROR(bsp_display_brightness_init(), TAG, "Brightness initialization failed");
    ESP_RETURN_ON_ERROR(esp_lv_adapter_start(), TAG, "Display task initialization failed");
    *out_display = display;
    ESP_LOGI(TAG, "QSPI TE sync on GPIO%d; RGB565 partial buffer: %u bytes in PSRAM",
             LCD_TE, static_cast<unsigned>(DRAW_BUFFER_BYTES));
    ESP_LOGI(TAG, "Touch IRQ GPIO%d; input timer: %" PRIu32 " ms; LVGL core: 1",
             BSP_LCD_TOUCH_INT, TOUCH_READ_PERIOD_MS);
    ESP_LOGI(TAG, "Independent touch capture; queued transitions; separate DMA notification");
    return ESP_OK;
}
} // namespace

extern "C" void app_main(void)
{
    const auto power = chameleon_power_init();
    if (power != ESP_OK) ESP_LOGW(TAG, "DFS unavailable, retaining fixed CPU frequency: %s", esp_err_to_name(power));
    esp_err_t storage = nvs_flash_init();
    if (storage != ESP_OK) {
        // Keep the pet usable and show the failure; never silently erase a save.
        ESP_LOGE("round_pet", "Save initialization failed: %s", esp_err_to_name(storage));
    }
    lv_display_t *display = nullptr;
    if (start_display(&display) != ESP_OK) {
        ESP_LOGE("round_pet", "Display initialization failed");
        return;
    }
    ch_hw::start();
    if (bsp_display_lock(UINT32_MAX) != ESP_OK) return;
    chameleon_display_bind(display);
    chameleon_ui_start(storage == ESP_OK);
    ch_hw::set_ui_wake_callback(chameleon_display_notify_input);
#if CONFIG_ESP_LVGL_ADAPTER_ENABLE_FPS_STATS
    if (esp_lv_adapter_fps_stats_enable(display, true) == ESP_OK) {
        lv_timer_create(report_display_fps, 5000, display);
    }
#endif
    bsp_display_unlock();
}
