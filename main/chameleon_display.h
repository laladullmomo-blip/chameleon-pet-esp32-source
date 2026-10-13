#pragma once
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Screen transitions run under the LVGL lock. Life timers stay running.
void chameleon_display_bind(lv_display_t *display);
void chameleon_display_set_screen_off(bool screen_off);
// Hardware tasks only latch input and notify; the worker consumes it under lock.
void chameleon_display_notify_input(void);
void chameleon_display_process_pending_input(void);

#ifdef __cplusplus
}
#endif
