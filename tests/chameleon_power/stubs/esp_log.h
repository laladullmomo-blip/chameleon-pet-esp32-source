#pragma once

void ch_power_test_warning(const char *tag, const char *format, ...);
#define ESP_LOGW(tag, ...) ch_power_test_warning(tag, __VA_ARGS__)
