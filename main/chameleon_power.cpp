#include "chameleon_power.h"

#include "esp_log.h"
#include "esp_pm.h"

namespace {
constexpr const char *TAG = "ch_power";
esp_pm_lock_handle_t display_lock = nullptr;
bool display_lock_held = false;
} // namespace

esp_err_t chameleon_power_init(void)
{
    if (display_lock) return ESP_OK;

    esp_pm_lock_handle_t lock = nullptr;
    esp_err_t error = esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "pet_display", &lock);
    if (error != ESP_OK) return error;

    // Pin startup/display initialization to 240 MHz before enabling DFS.
    error = esp_pm_lock_acquire(lock);
    if (error != ESP_OK) {
        esp_pm_lock_delete(lock);
        return error;
    }

    esp_pm_config_t configuration{};
    configuration.max_freq_mhz = 240;
    configuration.min_freq_mhz = 80;
    configuration.light_sleep_enable = false;
    error = esp_pm_configure(&configuration);
    if (error != ESP_OK) {
        esp_pm_lock_release(lock);
        esp_pm_lock_delete(lock);
        return error;
    }

    display_lock = lock;
    display_lock_held = true;
    return ESP_OK;
}

void chameleon_power_set_screen_off(bool screen_off)
{
    // The UI serializes transitions. Repeated notifications must not increase
    // the recursive PM lock's reference count or release an unheld lock.
    if (!display_lock || display_lock_held == !screen_off) return;

    const esp_err_t error = screen_off ? esp_pm_lock_release(display_lock)
                                      : esp_pm_lock_acquire(display_lock);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "Display frequency lock transition failed: %s", esp_err_to_name(error));
        return;
    }
    display_lock_held = !screen_off;
}
