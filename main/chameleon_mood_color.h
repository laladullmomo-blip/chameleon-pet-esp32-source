// SPDX-License-Identifier: MIT
#pragma once

#include "chameleon_state.h"
#include "lvgl.h"

// LVGL-thread-only runtime tinting. Approved flash assets remain read-only.
namespace ch_mood {
enum class Tone { Original, Happy, Unhappy, Hungry, Sick };

// Sleeping always retains the approved original colors, including sick sleep.
Tone choose(const ch::State &state);

// At most two tinted frames live in reusable PSRAM buffers. A returned tinted
// descriptor remains valid until its slot is reused by a later frame() call.
// Set it as the LVGL image source when the source, stage, or tone changes.
// stage is CH_STAGE_*; omission identifies approved sources automatically.
// Original, unsupported formats, and allocation failures return source intact.
const lv_image_dsc_t *frame(const lv_image_dsc_t *source, Tone tone, int stage = -1);

// Recover the approved asset behind one of this module's live descriptors.
const void *source_for(const void *source);

// Call only after LVGL images using tinted descriptors have been removed.
void clear();
} // namespace ch_mood
