#pragma once
#include "lvgl.h"

// Start under the official BSP display lock, after NVS and display setup.
void chameleon_ui_start(bool storage_ready);

// Called by the display worker under its lock, after a debounced key event.
void chameleon_ui_process_key_events();

// Preserve one sampling grid when a dirty area crosses a scaled sprite.
// Called under the display lock, before the panel's even-coordinate alignment.
void chameleon_ui_expand_dirty_area(lv_area_t *area);
