#pragma once

#include <cstdint>

// Nonblocking board services. All absent/stale data are explicitly invalid.
namespace ch_hw {
struct Status {
    // Retained status fields for compatibility. Tilt is disabled in this pet.
    bool imu_ready = false;
    bool audio_ready = false;
    bool time_valid = false;
    bool battery_valid = false;
    bool charging = false;
    bool wifi_connected = false;
    bool weather_valid = false;
    bool provisioning = false;
    float tilt_x = 0;
    float tilt_y = 0;
    float temp_c = 0;
    int battery_percent = 0;
    int64_t epoch_seconds = 0; // Actual synchronized UTC time; invalid after cold boot.
    int hour = 0;
    int minute = 0;
    int month = 0;
    int day = 0;
    int weekday = 0;
    int year = 0;
    int weather_code = -1;
    int64_t weather_updated_at = 0;
    char weather[48] = "未获取天气";
    char weather_city[48] = "汕头";
    char ip[20] = "";
    char message[100] = "硬件服务启动中";
    // Debounced short presses are counted on release. PWR long press remains
    // under AXP2101 hardware control; the application never drives these pins.
    uint32_t pwr_presses = 0;
    uint32_t boot_presses = 0;
};
void start();
Status snapshot();
// Called from the hardware task after it publishes debounced short presses.
// Registration is thread-safe; the callback must only queue a UI wake request.
void set_ui_wake_callback(void (*callback)());
// Queue an immediate percentage / charging / clock refresh, e.g. on screen wake.
void request_status_refresh();
// Zero turns the AMOLED off; positive settings are clamped to 10..100 percent.
void set_brightness(int percent);
// Serve queued panel requests. The caller must already hold the LVGL lock.
// Used by UI startup and its timer; set_brightness remains safe to queue anywhere.
void apply_display_requests();
void set_provisioning(bool enabled);
bool set_clock(int year, int month, int day, int hour, int minute);
void refresh_weather();
enum class Sound : uint8_t { Feed, Tap, Alarm };
// Cancel older feedback; each PCM block checks this setting's generation.
// final_tap confirms the mute button once while ordinary sounds stay muted.
void set_sound_enabled(bool enabled, bool final_tap = false);
// Separate 0..100 levels; codec writes occur only in the audio task.
void set_sound_volumes(int effects, int alarm);
void play_sound(Sound sound = Sound::Tap);
// Cancel queued/current alarm notes without cancelling ordinary button taps.
void cancel_alarm_sound();
} // namespace ch_hw
