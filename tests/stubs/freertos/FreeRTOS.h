#pragma once
#include <cstdint>
#ifndef HOST_TICK_MS
#define HOST_TICK_MS 1
#endif
using TickType_t = uint32_t;
#define portTICK_PERIOD_MS HOST_TICK_MS
#define pdMS_TO_TICKS(ms) ((TickType_t)((ms) / HOST_TICK_MS))
