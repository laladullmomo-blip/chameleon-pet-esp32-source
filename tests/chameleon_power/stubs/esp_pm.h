#pragma once

#include "esp_err.h"

// Match the IDF 6.0.2 API used by the application. These stubs validate lock
// ownership/error handling; they do not model CPU timing or device power.
struct esp_pm_config_t {
    int max_freq_mhz;
    int min_freq_mhz;
    bool light_sleep_enable;
};
using esp_pm_lock_handle_t = void *;
enum esp_pm_lock_type_t { ESP_PM_CPU_FREQ_MAX };

esp_err_t esp_pm_lock_create(esp_pm_lock_type_t, int, const char *, esp_pm_lock_handle_t *);
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t);
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t);
esp_err_t esp_pm_lock_delete(esp_pm_lock_handle_t);
esp_err_t esp_pm_configure(const void *);
