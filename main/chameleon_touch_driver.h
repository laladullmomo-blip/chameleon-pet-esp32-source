#pragma once

#include "chameleon_touch.h"
#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Register touch with default adapter IRQ ownership disabled and with the
// custom read callback below before starting this sampler.
esp_err_t chameleon_touch_start(esp_lcd_touch_handle_t touch, lv_indev_t *input);
esp_err_t chameleon_touch_read(esp_lcd_touch_handle_t touch,
                               esp_lcd_touch_point_data_t *points,
                               uint8_t *count, uint8_t max_count,
                               void *user_ctx);

#ifdef __cplusplus
}
#endif
