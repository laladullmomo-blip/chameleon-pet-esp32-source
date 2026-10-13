// Derived from Ellenp2p/esp32-pet-tamagotchi.
// Copyright (c) 2026 ellenp2p. MIT License; see THIRD_PARTY_LICENSES.
#pragma once
#include <lvgl.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PET_ANIM_IDLE = 0, PET_ANIM_HAPPY, PET_ANIM_EATING,
    PET_ANIM_SLEEPING, PET_ANIM_PLAYING, PET_ANIM_SICK, PET_ANIM_COUNT
} pet_anim_state_t;

enum { PET_FRAME_COUNT = 9, PET_FRAME_SIZE = 96 };
extern const lv_image_dsc_t * pet_idle_frames[PET_FRAME_COUNT];
extern const lv_image_dsc_t * pet_happy_frames[PET_FRAME_COUNT];
extern const lv_image_dsc_t * pet_eating_frames[PET_FRAME_COUNT];
extern const lv_image_dsc_t * pet_sleeping_frames[PET_FRAME_COUNT];
extern const lv_image_dsc_t * pet_playing_frames[PET_FRAME_COUNT];
extern const lv_image_dsc_t * pet_sick_frames[PET_FRAME_COUNT];
extern const lv_image_dsc_t * const * pet_anim_frames[PET_ANIM_COUNT];

#ifdef __cplusplus
}
#endif
