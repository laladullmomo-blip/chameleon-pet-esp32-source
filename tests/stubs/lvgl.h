#pragma once
#include <stdint.h>
#include <stddef.h>
#define LV_IMAGE_HEADER_MAGIC 0x19
#define LV_COLOR_FORMAT_RGB565A8 0x14
// Minimal compile-time descriptor declarations for resource integrity checks.
// They provide no rendering implementation.
typedef struct {
    uint32_t magic, cf, flags, w, h, stride, reserved_2;
} lv_image_header_t;
typedef struct {
    lv_image_header_t header;
    uint32_t data_size;
    const uint8_t *data;
    void *reserved;
} lv_image_dsc_t;
