#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Project hooks compiled from pinned, generated adapter sources. The setter
// and touch processor require the LVGL lock; notifications never touch LVGL.
void chameleon_lv_adapter_set_max_wait(uint32_t milliseconds);
void chameleon_lv_adapter_notify(void);
bool chameleon_lv_adapter_notify_from_isr(void);
void chameleon_lv_adapter_process_pending_touch(void);

#ifdef __cplusplus
}
#endif
