#pragma once
#include <cstdio>
#define ESP_LOGE(tag, ...) do { std::fprintf(stderr, "E %s: ", tag); std::fprintf(stderr, __VA_ARGS__); std::fputc('\n', stderr); } while (0)
#define ESP_LOGW(tag, ...) do { std::fprintf(stderr, "W %s: ", tag); std::fprintf(stderr, __VA_ARGS__); std::fputc('\n', stderr); } while (0)
#define ESP_LOGI(tag, ...) do { std::fprintf(stdout, "I %s: ", tag); std::fprintf(stdout, __VA_ARGS__); std::fputc('\n', stdout); } while (0)
