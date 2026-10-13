#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Configure 240/80 MHz DFS, with automatic light sleep disabled. Call once
// from app_main before starting display/UI tasks; failure leaves startup's
// fixed-frequency configuration in place.
esp_err_t chameleon_power_init(void);

// Call from the serialized UI task, never an ISR. The display holds a CPU_MAX
// lock while visible; screen-off releases it so idle periods can use 80 MHz.
// RTOS and driver locks can still request 240 MHz for background work.
void chameleon_power_set_screen_off(bool screen_off);

#ifdef __cplusplus
}
#endif
