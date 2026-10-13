#include "chameleon_alarm.h"

#include <limits>

namespace ch_alarm {
namespace {
constexpr int64_t kSecondsPerDay = 24 * 60 * 60;
// Bound the epoch before arithmetic so an invalid time source cannot overflow
// either the UTC+8 addition or the persisted signed day number.
constexpr int64_t kMaximumEpoch =
    static_cast<int64_t>(std::numeric_limits<int32_t>::max()) * kSecondsPerDay
    - Scheduler::kUtcOffsetSeconds;
bool valid_epoch(int64_t epoch) { return epoch >= 0 && epoch <= kMaximumEpoch; }
bool same_config(const Config &a, const Config &b) {
    return a.enabled == b.enabled && a.hour == b.hour && a.minute == b.minute;
}
}  // namespace

bool Scheduler::valid_config(const Config &config) {
    return config.enabled <= 1 && config.hour < 24 && config.minute < 60
        && config.reserved == 0;
}

bool Scheduler::configure(const Config &config) {
    if (!valid_config(config)) return false;
    if (!same_config(config_, config)) {
        cancel_runtime();
        observed_ = false;
    }
    config_ = config;
    return true;
}

bool Scheduler::restore_receipt(const Receipt &receipt) {
    if (receipt.last_trigger_day < -1) return false;
    // Never lower an already observed day. Loading stale data into a running
    // scheduler must not enable a duplicate alarm.
    if (receipt.last_trigger_day > receipt_.last_trigger_day) receipt_ = receipt;
    return true;
}

void Scheduler::cancel_runtime() {
    ringing_ = false;
    snoozed_ = false;
    ringing_started_ = 0;
    snooze_epoch_ = 0;
}

Event Scheduler::ring(uint64_t monotonic_ms) {
    ringing_ = true;
    ringing_started_ = monotonic_ms;
    snoozed_ = false;
    snooze_epoch_ = 0;
    return Event::RingStarted;
}

Event Scheduler::stop(uint64_t) {
    if (!ringing_ && !snoozed_) return Event::None;
    cancel_runtime();
    return Event::Stopped;
}

Event Scheduler::snooze(uint64_t monotonic_ms) {
    if (!ringing_ || !observed_ || !config_.enabled
        || monotonic_ms < previous_monotonic_
        || monotonic_ms - previous_monotonic_ > kMaxObservationGapMs) {
        return Event::None;
    }
    // The button event may fall just after the last UI sample. Account for its
    // whole elapsed seconds so five minutes are measured from this operation.
    const int64_t seconds_after_sample =
        static_cast<int64_t>((monotonic_ms - previous_monotonic_) / 1000);
    if (previous_epoch_ > kMaximumEpoch - kSnoozeSeconds - seconds_after_sample)
        return Event::None;
    ringing_ = false;
    ringing_started_ = 0;
    snoozed_ = true;
    snooze_epoch_ = previous_epoch_ + seconds_after_sample + kSnoozeSeconds;
    return Event::Snoozed;
}

Event Scheduler::observe(int64_t epoch_seconds, bool clock_valid, uint64_t monotonic_ms) {
    if (!clock_valid || !valid_epoch(epoch_seconds)) {
        const bool active = ringing_ || snoozed_;
        cancel_runtime();
        observed_ = false;
        return active ? Event::ClockLost : Event::None;
    }

    const bool had_baseline = observed_;
    bool continuous = false;
    if (had_baseline && monotonic_ms >= previous_monotonic_) {
        const uint64_t elapsed = monotonic_ms - previous_monotonic_;
        const int64_t wall_seconds = epoch_seconds - previous_epoch_;
        // Reject negative and large wall differences before multiplication.
        if (elapsed <= kMaxObservationGapMs && wall_seconds >= 0
            && wall_seconds <= 7) {
            const int64_t discrepancy = wall_seconds * 1000 - static_cast<int64_t>(elapsed);
            continuous = discrepancy >= -kMaxClockSkewMs && discrepancy <= kMaxClockSkewMs;
        }
    }
    const int64_t previous_epoch = previous_epoch_;
    previous_epoch_ = epoch_seconds;
    previous_monotonic_ = monotonic_ms;
    observed_ = true;

    // A delayed observer still ends the sound after its absolute monotonic
    // limit, even when this polling gap is too long to trigger a daily alarm.
    if (ringing_ && monotonic_ms >= ringing_started_
        && monotonic_ms - ringing_started_ >= kRingTimeoutMs) {
        cancel_runtime();
        return Event::TimedOut;
    }
    if (!continuous) {
        const bool active = ringing_ || snoozed_;
        cancel_runtime();
        return had_baseline && active ? Event::ClockChanged : Event::None;
    }
    if (!config_.enabled) return Event::None;

    if (ringing_) return Event::None;
    if (snoozed_) {
        if (previous_epoch < snooze_epoch_ && epoch_seconds >= snooze_epoch_)
            return ring(monotonic_ms);
        return Event::None;
    }

    const int64_t local_day = (epoch_seconds + kUtcOffsetSeconds) / kSecondsPerDay;
    const int64_t due_epoch = local_day * kSecondsPerDay
        + static_cast<int64_t>(config_.hour) * 3600
        + static_cast<int64_t>(config_.minute) * 60 - kUtcOffsetSeconds;
    if (local_day > receipt_.last_trigger_day && previous_epoch < due_epoch
        && epoch_seconds >= due_epoch) {
        receipt_.last_trigger_day = static_cast<int32_t>(local_day);
        return ring(monotonic_ms);
    }
    return Event::None;
}

}  // namespace ch_alarm
