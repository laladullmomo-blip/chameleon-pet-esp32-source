#pragma once

#include <cstdint>

namespace ch_hw::detail {
// Keep the service responsive even if an external event cannot notify it.
// Event notifications wake it immediately; this limit is only the fallback.
constexpr uint32_t kNetworkMaxWaitMs = 30000;
constexpr uint32_t kProvisioningMaxWaitMs = 1000;
constexpr uint32_t kBatteryPeriodMs = 30000;
constexpr uint32_t kChargingPeriodMs = 5000;
constexpr uint32_t kKeyDebounceMs = 40;

// The hardware task sleeps until an edge's debounce deadline. Keeping edge
// timestamps allows a complete short press to survive a slow PMU transaction.
struct DebouncedKey {
    bool raw=false,stable=false,armed=false,eligible=false;
    int64_t changed_at=0,pressed_at=0;

    void init(bool pressed,int64_t now) {
        raw=stable=pressed; armed=!pressed; eligible=false;
        changed_at=pressed_at=now;
    }
    void observe(bool pressed,int64_t now) {
        if(pressed!=raw) { raw=pressed; changed_at=now; }
    }
    bool settle(int64_t now) {
        if(stable==raw||now-changed_at<kKeyDebounceMs)return false;
        stable=raw;
        const int64_t transition_at=changed_at+kKeyDebounceMs;
        if(stable) { pressed_at=transition_at;eligible=armed;return false; }
        const int64_t duration=transition_at-pressed_at;
        const bool short_press=eligible&&duration>=kKeyDebounceMs&&duration<1000;
        eligible=false;armed=true;return short_press;
    }
    int64_t deadline() const { return stable!=raw?changed_at+kKeyDebounceMs:0; }
};

struct ReconnectBackoff {
    uint32_t delay_ms=30000;
    int64_t at=0;
    void reset(int64_t now) { delay_ms=30000;at=now; }
    void attempted(int64_t now) {
        at=now+delay_ms;
        delay_ms=delay_ms<150000?delay_ms*2:300000;
    }
};

// A connected access point may have no Internet. Failed weather requests must
// back off independently of Wi-Fi association; explicit refreshes reset this.
struct WeatherBackoff {
    uint8_t failures=0;
    void reset() { failures=0; }
    uint32_t failed_delay_ms() {
        constexpr uint32_t delays[]{60000,120000,300000,900000};
        const auto delay=delays[failures];
        if(failures<3)++failures;
        return delay;
    }
};

inline uint32_t wait_until(int64_t now, int64_t deadline, uint32_t current_wait) {
    if(deadline<=now) return 1;
    const auto remaining=static_cast<uint64_t>(deadline-now);
    return remaining<current_wait ? static_cast<uint32_t>(remaining) : current_wait;
}

struct NetworkSchedule {
    bool station_configured=false;
    bool connected=false;
    bool work_pending=false;
    bool provisioning=false;
    int64_t reconnect_at=0;
    int64_t weather_at=0;
    int64_t weather_expires_at=0;
};

inline uint32_t network_wait_ms(int64_t now, const NetworkSchedule &schedule) {
    if(schedule.work_pending) return 1;
    uint32_t wait=schedule.provisioning?kProvisioningMaxWaitMs:kNetworkMaxWaitMs;
    if(schedule.connected) wait=wait_until(now,schedule.weather_at,wait);
    else if(schedule.station_configured) wait=wait_until(now,schedule.reconnect_at,wait);
    // Expiry is an upcoming one-shot deadline. An already expired timestamp
    // must not turn an offline service into a one-millisecond polling loop.
    if(schedule.weather_expires_at>0&&schedule.weather_expires_at>=now)
        wait=wait_until(now,schedule.weather_expires_at,wait);
    return wait;
}
} // namespace ch_hw::detail
