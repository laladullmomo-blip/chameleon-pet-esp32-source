#pragma once
#include <cstdint>
#include "FreeRTOS.h"
using TaskFunction_t = void (*)(void *);
using BaseType_t = int;
using UBaseType_t = unsigned;
using TaskHandle_t = void *;
constexpr BaseType_t pdPASS = 1;
constexpr BaseType_t pdFAIL = 0;
inline BaseType_t xTaskCreate(TaskFunction_t, const char *, uint32_t, void *, UBaseType_t, TaskHandle_t *) { return pdFAIL; }
inline void vTaskDelete(TaskHandle_t) {}
