#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// These hooks never acquire the LVGL lock or read the controller.
void chameleon_touch_set_screen_off(bool screen_off);
// The timestamp belongs to the last sample consumed by the LVGL callback.
// Zero means no queued sample was consumed; LVGL supplies its current tick.
uint32_t chameleon_touch_sample_timestamp(void);

#ifdef __cplusplus
}
#endif
