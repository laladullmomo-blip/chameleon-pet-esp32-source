#pragma once

#include <cstdint>
#include <type_traits>

namespace ch_alarm {

// Store Config and Receipt in a versioned, CRC-protected NVS record. Never persist
// monotonic timestamps, the observation baseline, ringing or a snooze deadline.
struct Config {
    uint8_t enabled = 0;
    uint8_t hour = 8;
    uint8_t minute = 0;
    uint8_t reserved = 0;
};
struct Receipt {
    // Highest UTC+8 local day on which the daily alarm fired; -1 means never.
    // This high-water mark survives clock rollback, config changes and reboots.
    int32_t last_trigger_day = -1;
};
static_assert(sizeof(Config) == 4 && sizeof(Receipt) == 4,
    "alarm persistence layout changed");
static_assert(std::is_trivially_copyable<Config>::value
    && std::is_trivially_copyable<Receipt>::value, "alarm records must stay POD");

enum class Event : uint8_t {
    None, RingStarted, Stopped, Snoozed, TimedOut, ClockLost, ClockChanged
};

class Scheduler {
public:
    static constexpr int64_t kUtcOffsetSeconds = 8 * 60 * 60;
    static constexpr uint64_t kRingTimeoutMs = 60 * 1000;
    static constexpr int64_t kSnoozeSeconds = 5 * 60;
    // Lit 40 ms and dark 1 Hz are both continuous. A missed polling gap above
    // five seconds or a wall/monotonic discrepancy above two seconds is a new
    // time baseline, never a request to replay an old alarm.
    static constexpr uint64_t kMaxObservationGapMs = 5 * 1000;
    static constexpr int64_t kMaxClockSkewMs = 2 * 1000;

    static bool valid_config(const Config &config);
    // Reject invalid input without changing anything. A real configuration
    // change stops a running alarm and pending snooze; it preserves Receipt.
    bool configure(const Config &config);
    bool restore_receipt(const Receipt &receipt);
    Event observe(int64_t epoch_seconds, bool clock_valid, uint64_t monotonic_ms);
    Event stop(uint64_t monotonic_ms);
    Event snooze(uint64_t monotonic_ms);

    const Config &config() const { return config_; }
    const Receipt &receipt() const { return receipt_; }
    bool ringing() const { return ringing_; }
    bool snoozed() const { return snoozed_; }
    bool clock_ready() const { return observed_; }
    int64_t snooze_epoch_seconds() const { return snoozed_ ? snooze_epoch_ : 0; }

private:
    Config config_{};
    Receipt receipt_{};
    bool observed_ = false;
    bool ringing_ = false;
    bool snoozed_ = false;
    int64_t previous_epoch_ = 0;
    uint64_t previous_monotonic_ = 0;
    uint64_t ringing_started_ = 0;
    int64_t snooze_epoch_ = 0;

    void cancel_runtime();
    Event ring(uint64_t monotonic_ms);
};

}  // namespace ch_alarm
