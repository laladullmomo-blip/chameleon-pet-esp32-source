#pragma once
#include <cstddef>
#include <cstdlib>
constexpr int MALLOC_CAP_SPIRAM = 1;
constexpr int MALLOC_CAP_8BIT = 2;
constexpr int MALLOC_CAP_INTERNAL = 4;
inline void *heap_caps_malloc(size_t bytes, int) { return std::malloc(bytes); }
inline void heap_caps_free(void *data) { std::free(data); }
inline size_t heap_caps_get_free_size(int) { return 0; } // Native host is not an ESP32 heap measurement.
