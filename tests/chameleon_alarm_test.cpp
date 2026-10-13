#include "chameleon_alarm.h"

#include <cstddef>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using namespace ch_alarm;
constexpr int32_t kDay = 20000;
int64_t at(int32_t day, int hour, int minute, int second = 0) {
    return static_cast<int64_t>(day) * 86400 + hour * 3600 + minute * 60 + second
        - Scheduler::kUtcOffsetSeconds;
}
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void arm(Scheduler &alarm, int hour = 8, int minute = 0, int32_t day = kDay) {
    require(alarm.configure({1, static_cast<uint8_t>(hour), static_cast<uint8_t>(minute), 0}),
        "cannot configure valid alarm");
    require(alarm.observe(at(day, hour, minute) - 1, true, 1000) == Event::None,
        "first observation rang early");
    require(alarm.observe(at(day, hour, minute), true, 2000) == Event::RingStarted
        && alarm.ringing(), "daily boundary did not ring");
}
}  // namespace

int main() {
    using namespace ch_alarm;
    const std::vector<std::pair<const char *, std::function<void()>>> cases = {
        {"config and receipt have stable validated persistence", [] {
            require(sizeof(Config) == 4 && sizeof(Receipt) == 4
                && offsetof(Config, enabled) == 0 && offsetof(Config, hour) == 1
                && offsetof(Config, minute) == 2 && offsetof(Config, reserved) == 3,
                "storage byte layout changed");
            Scheduler alarm;
            require(!alarm.config().enabled && alarm.config().hour == 8,
                "new alarm was enabled or lacks a usable default");
            for (Config config : {Config{2, 8, 0, 0}, Config{1, 24, 0, 0},
                     Config{1, 8, 60, 0}, Config{1, 8, 0, 1}})
                require(!alarm.configure(config) && !alarm.config().enabled,
                    "invalid config changed runtime");
            require(!alarm.restore_receipt({-2}) && alarm.restore_receipt({kDay}),
                "receipt validation failed");
            require(alarm.restore_receipt({-1}) && alarm.receipt().last_trigger_day == kDay,
                "stale receipt reset its high-water mark");
            Config source{1, 23, 59, 0}, target{};
            std::memcpy(&target, &source, sizeof(source));
            require(alarm.configure(target) && alarm.config().hour == 23
                && alarm.config().minute == 59, "POD record did not round-trip");
        }},
        {"first valid sample never replays today's missed alarm", [] {
            for (int second : {0, 1, 30, 59, 60, 3600}) {
                Scheduler alarm;
                alarm.configure({1, 8, 0, 0});
                require(alarm.observe(at(kDay, 8, 0) + second, true, 1000) == Event::None,
                    "boot replayed an old minute");
                require(alarm.observe(at(kDay, 8, 0) + second + 1, true, 2000) == Event::None,
                    "second boot sample replayed an old minute");
            }
        }},
        {"dark one hertz polling fires on actual local wall time", [] {
            Scheduler alarm;
            alarm.configure({1, 8, 30, 0});
            const int64_t due = at(kDay, 8, 30);
            alarm.observe(due - 3, true, 1000);
            require(alarm.observe(due - 2, true, 2000) == Event::None
                && alarm.observe(due - 1, true, 3000) == Event::None
                && alarm.observe(due, true, 4000) == Event::RingStarted,
                "one hertz polling missed wall time");
            require(alarm.receipt().last_trigger_day == kDay, "wrong UTC+8 receipt");
        }},
        {"lit polling and repeated ticks emit one daily event", [] {
            Scheduler alarm;
            const int64_t due = at(kDay, 8, 0);
            alarm.configure({1, 8, 0, 0});
            alarm.observe(due - 1, true, 1000);
            for (uint64_t ms = 1040; ms < 2000; ms += 40)
                require(alarm.observe(due - 1, true, ms) == Event::None,
                    "quantised seconds fired early");
            require(alarm.observe(due, true, 2000) == Event::RingStarted,
                "forty millisecond polling missed ring");
            for (uint64_t ms = 2000; ms <= 2800; ms += 40)
                require(alarm.observe(due, true, ms) == Event::None,
                    "same epoch emitted duplicate event");
            require(alarm.stop(2800) == Event::Stopped && !alarm.ringing()
                && alarm.observe(due + 1, true, 3000) == Event::None,
                "stop replayed same minute");
        }},
        {"small normal scheduling jitter still crosses the minute", [] {
            Scheduler alarm;
            alarm.configure({1, 8, 0, 0});
            const int64_t due = at(kDay, 8, 0);
            alarm.observe(due - 2, true, 1000);
            require(alarm.observe(due + 1, true, 3980) == Event::RingStarted,
                "normal three second catch-up lost scheduled minute");
        }},
        {"midnight follows UTC plus eight and permits the next local day", [] {
            Scheduler alarm;
            arm(alarm, 0, 0);
            alarm.stop(2000);
            alarm.observe(at(kDay + 1, 0, 0) - 1, true, 86401000);
            require(alarm.observe(at(kDay + 1, 0, 0), true, 86402000) == Event::RingStarted
                && alarm.receipt().last_trigger_day == kDay + 1,
                "next UTC+8 midnight did not ring");
        }},
        {"receipt survives reboot and protects same-day and earlier rollback", [] {
            Scheduler first;
            arm(first);
            Scheduler second;
            second.configure(first.config());
            second.restore_receipt(first.receipt());
            const int64_t due = at(kDay, 8, 0);
            second.observe(due - 1, true, 1000);
            require(second.observe(due, true, 2000) == Event::None,
                "reboot ignored daily receipt");
            second.observe(at(kDay - 1, 8, 0) - 1, true, 3000);
            require(second.observe(at(kDay - 1, 8, 0), true, 4000) == Event::None,
                "rollback replayed an earlier day");
            second.observe(at(kDay + 1, 8, 0) - 1, true, 5000);
            require(second.observe(at(kDay + 1, 8, 0), true, 6000) == Event::RingStarted,
                "receipt prevented future day");
        }},
        {"same-day time edits cannot create extra daily rings", [] {
            Scheduler alarm;
            arm(alarm);
            require(alarm.configure({1, 8, 1, 0}) && !alarm.ringing(),
                "editing alarm did not stop old sound");
            alarm.observe(at(kDay, 8, 0, 59), true, 61000);
            require(alarm.observe(at(kDay, 8, 1), true, 62000) == Event::None,
                "same-day config edit bypassed daily receipt");
            alarm.configure({1, 8, 0, 0});
            alarm.observe(at(kDay, 7, 59, 59), true, 63000);
            require(alarm.observe(at(kDay, 8, 0), true, 64000) == Event::None,
                "config plus rollback duplicated daily ring");
        }},
        {"large forward adjustment and long missing polls do not catch up", [] {
            for (uint64_t mono : {2000ULL, 11000ULL, 60000ULL}) {
                Scheduler alarm;
                alarm.configure({1, 8, 0, 0});
                const int64_t due = at(kDay, 8, 0);
                alarm.observe(due - 10, true, 1000);
                require(alarm.observe(due + 1, true, mono) == Event::None
                    && !alarm.ringing(), "adjustment or missing polls replayed alarm");
                require(alarm.observe(due + 2, true, mono + 1000) == Event::None,
                    "new baseline replayed old alarm on next tick");
            }
        }},
        {"backward adjustment stops active ring without permitting duplicates", [] {
            Scheduler alarm;
            arm(alarm);
            const int64_t due = at(kDay, 8, 0);
            require(alarm.observe(due - 1, true, 3000) == Event::ClockChanged
                && !alarm.ringing(), "active sound survived clock rollback");
            require(alarm.observe(due, true, 4000) == Event::None,
                "clock rollback duplicated already completed day");
        }},
        {"invalid and extreme clock samples remain silent and cancel runtime", [] {
            for (int64_t epoch : {std::numeric_limits<int64_t>::min(), int64_t{-1},
                     std::numeric_limits<int64_t>::max()}) {
                Scheduler alarm;
                alarm.configure({1, 8, 0, 0});
                require(alarm.observe(epoch, true, 1000) == Event::None
                    && !alarm.clock_ready(), "invalid epoch became ready");
            }
            Scheduler alarm;
            arm(alarm);
            const int64_t due = at(kDay, 8, 0);
            require(alarm.observe(due + 1, false, 3000) == Event::ClockLost
                && !alarm.ringing() && !alarm.clock_ready(), "invalid clock retained ringing");
            require(alarm.observe(due + 2, true, 4000) == Event::None,
                "clock recovery replayed old alarm");
        }},
        {"monotonic rollback and large uptime values cannot overflow", [] {
            Scheduler alarm;
            arm(alarm);
            const int64_t due = at(kDay, 8, 0);
            require(alarm.observe(due + 1, true, 100) == Event::ClockChanged
                && !alarm.ringing(), "monotonic rollback was not rejected");
            Scheduler high;
            high.configure({1, 8, 0, 0});
            const uint64_t start = std::numeric_limits<uint64_t>::max() - 2000;
            high.observe(due - 1, true, start);
            require(high.observe(due, true, start + 1000) == Event::RingStarted,
                "large monotonic uptime overflowed");
            require(high.observe(due + 1, true, 1) == Event::ClockChanged,
                "wrapped monotonic uptime was accepted");
        }},
        {"ring timeout is exactly sixty monotonic seconds and fires once", [] {
            Scheduler alarm;
            arm(alarm);
            const int64_t due = at(kDay, 8, 0);
            for (uint64_t second = 1; second < 60; ++second)
                require(alarm.observe(due + static_cast<int64_t>(second), true,
                        2000 + second * 1000) == Event::None && alarm.ringing(),
                    "ring timed out early");
            require(alarm.observe(due + 60, true, 62000) == Event::TimedOut
                && !alarm.ringing(), "sixty second timeout was not exact");
            require(alarm.observe(due + 61, true, 63000) == Event::None
                && alarm.stop(63000) == Event::None, "timeout was not idempotent");
            Scheduler delayed;
            arm(delayed);
            require(delayed.observe(due + 60, true, 62000) == Event::TimedOut,
                "a delayed poll left an expired alarm sounding");
        }},
        {"snooze lasts five real minutes from the button operation", [] {
            Scheduler alarm;
            arm(alarm);
            const int64_t due = at(kDay, 8, 0);
            require(alarm.snooze(3500) == Event::Snoozed && !alarm.ringing()
                && alarm.snoozed() && alarm.snooze_epoch_seconds() == due + 301,
                "snooze did not account for the button timestamp");
            for (int second = 2; second < 301; ++second)
                require(alarm.observe(due + second, true, 2000 + second * 1000)
                        == Event::None && alarm.snoozed(), "snooze rang before five minutes");
            require(alarm.observe(due + 301, true, 303000) == Event::RingStarted
                && alarm.ringing() && !alarm.snoozed()
                && alarm.receipt().last_trigger_day == kDay, "snooze did not ring once");
            require(alarm.observe(due + 301, true, 303040) == Event::None,
                "snooze ring repeated on same epoch");
        }},
        {"snooze can repeat and crosses local midnight without new receipt", [] {
            Scheduler alarm;
            arm(alarm, 23, 58);
            const int64_t due = at(kDay, 23, 58);
            alarm.snooze(2000);
            for (int second = 1; second <= 300; ++second) {
                const Event event = alarm.observe(due + second, true, 2000 + second * 1000);
                require(event == (second == 300 ? Event::RingStarted : Event::None),
                    "snooze failed over local midnight");
            }
            require(alarm.receipt().last_trigger_day == kDay
                && alarm.snooze(302000) == Event::Snoozed,
                "midnight snooze advanced daily receipt or cannot repeat");
            for (int second = 301; second <= 600; ++second)
                require(alarm.observe(due + second, true, 2000 + second * 1000)
                    == (second == 600 ? Event::RingStarted : Event::None),
                    "repeat snooze failed");
        }},
        {"stop and disable cancel both sound and snooze without rewards", [] {
            Scheduler alarm;
            require(alarm.stop(0) == Event::None && alarm.snooze(0) == Event::None,
                "inactive button produced an alarm event");
            arm(alarm);
            alarm.snooze(2000);
            require(alarm.stop(2000) == Event::Stopped && !alarm.snoozed(),
                "stop failed to cancel pending snooze");
            alarm.observe(at(kDay + 1, 8, 0) - 1, true, 86401000);
            require(alarm.observe(at(kDay + 1, 8, 0), true, 86402000) == Event::RingStarted,
                "stop disabled future daily alarm");
            alarm.snooze(86402000);
            require(alarm.configure({0, 8, 0, 0}) && !alarm.ringing() && !alarm.snoozed(),
                "disable retained pending alarm");
            alarm.observe(at(kDay + 2, 8, 0) - 1, true, 172801000);
            require(alarm.observe(at(kDay + 2, 8, 0), true, 172802000) == Event::None,
                "disabled alarm rang");
        }},
        {"clock loss or correction cancels snooze rather than replaying", [] {
            const int64_t due = at(kDay, 8, 0);
            for (bool valid : {false, true}) {
                Scheduler alarm;
                arm(alarm);
                alarm.snooze(2000);
                require(alarm.observe(due + 301, valid, 3000)
                        == (valid ? Event::ClockChanged : Event::ClockLost)
                    && !alarm.ringing() && !alarm.snoozed(),
                    "clock failure replayed or retained old snooze");
                require(alarm.observe(due + 302, true, 4000) == Event::None,
                    "clock recovery replayed snooze");
            }
            Scheduler gap;
            arm(gap); gap.snooze(2000);
            require(gap.observe(due + 300, true, 302000) == Event::ClockChanged,
                "five minute polling gap replayed snooze");
        }},
        {"reboot restores only config and daily receipt, never a snooze", [] {
            Scheduler first;
            arm(first); first.snooze(2000);
            Scheduler second;
            second.configure(first.config()); second.restore_receipt(first.receipt());
            const int64_t due = at(kDay, 8, 0);
            second.observe(due + 299, true, 1000);
            require(second.observe(due + 300, true, 2000) == Event::None
                && !second.ringing() && !second.snoozed(), "reboot resurrected old snooze");
        }},
        {"unchanged valid config and rejected config preserve active sound", [] {
            Scheduler alarm;
            arm(alarm);
            require(alarm.configure(alarm.config()) && alarm.ringing(),
                "saving unchanged config canceled sound");
            require(!alarm.configure({1, 99, 0, 0}) && alarm.ringing(),
                "invalid config canceled sound");
            require(alarm.snooze(0) == Event::None && alarm.ringing()
                && alarm.snooze(10000) == Event::None && alarm.ringing(),
                "stale or backward button timestamp set snooze");
        }},
    };
    unsigned passed = 0;
    for (const auto &entry : cases) {
        try { entry.second(); ++passed; std::cout << "PASS " << entry.first << '\n'; }
        catch (const std::exception &error) {
            std::cerr << "FAIL " << entry.first << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cout << passed << " alarm rule groups passed\n";
    return 0;
}
