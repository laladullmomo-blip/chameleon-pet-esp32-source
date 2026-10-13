// Derived from Ellenp2p/esp32-pet-tamagotchi.
// Copyright (c) 2026 ellenp2p. MIT License; see THIRD_PARTY_LICENSES.
#include "pet_state.h"
#include "esp_log.h"
#include "esp_random.h"
#include <climits>
#include <cstring>

namespace pet {
namespace {
constexpr int kLevelTicks = 30; // One level per powered-on hour.
constexpr int kLowStatTicksForTombstone = 50;
constexpr const char *TAG = "pet_state";
LifeStage stage_for_age_ticks(int ticks)
{
    if (ticks < 10) return LifeStage::Egg;
    if (ticks < 90) return LifeStage::Baby;
    if (ticks < 180) return LifeStage::Child;
    if (ticks < 270) return LifeStage::Teen;
    return LifeStage::Adult;
}
bool neglected(int fullness, int happiness, int energy)
{
    return fullness < 20 || happiness < 20 || energy < 10;
}
bool p50() { return (esp_random() & 1) == 0; }
int nonnegative(int value) { return value < 0 ? 0 : value; }
} // namespace

const char *life_stage_name(LifeStage stage)
{
    switch (stage) {
        case LifeStage::Egg: return "Egg";
        case LifeStage::Baby: return "Baby";
        case LifeStage::Child: return "Child";
        case LifeStage::Teen: return "Teen";
        case LifeStage::Adult: return "Adult";
        case LifeStage::Tombstone: return "Tombstone";
        case LifeStage::Count: return "?";
    }
    return "?";
}
Pet &Pet::instance()
{
    static Pet instance;
    return instance;
}
void Pet::init()
{
    fullness_ = happiness_ = energy_ = 70;
    health_ = 90;
    sleeping_ = false;
    coins_ = 0;
    level_ = 1;
    age_ticks_ = 0;
    stage_ = LifeStage::Egg;
    stage_entered_tick_ = last_open_day_ = streak_ = low_stats_ticks_ = 0;
    just_changed_stage_ = false;
    dirty_ = false;
    last_update_tick_ = xTaskGetTickCount();
    ESP_LOGI(TAG, "Pet initialized");
}
int Pet::clamp(int64_t value) const
{
    return value < 0 ? 0 : value > 100 ? 100 : static_cast<int>(value);
}
void Pet::apply_snapshot(int fullness, int happiness, int energy, int health,
                         int coins, int level, int age_ticks, bool sleeping,
                         int stage, int stage_entered_tick, int last_open_day,
                         int low_stats_ticks)
{
    fullness_ = clamp(fullness);
    happiness_ = clamp(happiness);
    energy_ = clamp(energy);
    health_ = clamp(health);
    coins_ = nonnegative(coins);
    age_ticks_ = nonnegative(age_ticks);
    level_ = age_ticks_ / kLevelTicks + 1;
    const LifeStage living_stage = stage_for_age_ticks(age_ticks_);
    stage_ = stage == static_cast<int>(LifeStage::Tombstone) &&
             living_stage == LifeStage::Adult ? LifeStage::Tombstone : living_stage;
    sleeping_ = sleeping && stage_ != LifeStage::Tombstone;
    stage_entered_tick_ = nonnegative(stage_entered_tick);
    if (stage_entered_tick_ > age_ticks_) stage_entered_tick_ = age_ticks_;
    last_open_day_ = nonnegative(last_open_day);
    streak_ = 0;
    low_stats_ticks_ = nonnegative(low_stats_ticks);
    if (low_stats_ticks_ >= kLowStatTicksForTombstone)
        low_stats_ticks_ = kLowStatTicksForTombstone - 1;
    if (stage_ != LifeStage::Adult || !neglected(fullness_, happiness_, energy_))
        low_stats_ticks_ = 0;
    if (stage_ == LifeStage::Tombstone) low_stats_ticks_ = kLowStatTicksForTombstone;
    just_changed_stage_ = false;
    dirty_ = fullness_ != fullness || happiness_ != happiness || energy_ != energy ||
             health_ != health || coins_ != coins || level_ != level || age_ticks_ != age_ticks ||
             sleeping_ != sleeping || static_cast<int>(stage_) != stage ||
             stage_entered_tick_ != stage_entered_tick || last_open_day_ != last_open_day ||
             low_stats_ticks_ != low_stats_ticks;
    last_update_tick_ = xTaskGetTickCount();
}
State Pet::get_state() const
{
    return State{fullness_, happiness_, energy_, health_, coins_, level_, age_ticks_,
                 static_cast<int>(stage_)};
}
void Pet::feed()
{
    if (stage_ == LifeStage::Tombstone) return;
    fullness_ = clamp(fullness_ + 25);
    energy_ = clamp(energy_ + 5);
    happiness_ = clamp(happiness_ + 5);
    dirty_ = true;
}
void Pet::feed_with_amount(int amount)
{
    if (stage_ == LifeStage::Tombstone || amount <= 0) return;
    fullness_ = clamp(static_cast<int64_t>(fullness_) + amount);
    happiness_ = clamp(happiness_ + 2);
    dirty_ = true;
}
void Pet::drink_energy(int amount)
{
    if (stage_ == LifeStage::Tombstone || amount <= 0) return;
    energy_ = clamp(static_cast<int64_t>(energy_) + amount);
    happiness_ = clamp(happiness_ + 1);
    dirty_ = true;
}
void Pet::take_medicine(int amount)
{
    if (stage_ == LifeStage::Tombstone || amount <= 0) return;
    health_ = clamp(static_cast<int64_t>(health_) + amount);
    fullness_ = clamp(fullness_ - 2);
    dirty_ = true;
}
void Pet::play()
{
    if (stage_ == LifeStage::Tombstone) return;
    if (sleeping_) { wake_up(); return; }
    fullness_ = clamp(fullness_ - 10);
    energy_ = clamp(energy_ - 15);
    happiness_ = clamp(happiness_ + 20);
    health_ = clamp(health_ + 2);
    dirty_ = true;
}
void Pet::sleep()
{
    if (stage_ == LifeStage::Tombstone || sleeping_) return;
    sleeping_ = true;
    dirty_ = true;
}
void Pet::wake_up()
{
    if (stage_ == LifeStage::Tombstone || !sleeping_) return;
    sleeping_ = false;
    happiness_ = clamp(happiness_ - 5);
    dirty_ = true;
}
void Pet::pet()
{
    if (stage_ == LifeStage::Tombstone) return;
    happiness_ = clamp(happiness_ + 10);
    health_ = clamp(health_ + 2);
    dirty_ = true;
}
void Pet::work_outcome(const char *stat_key, int amount, int coin_delta)
{
    if (stage_ == LifeStage::Tombstone) return;
    int *stat = nullptr;
    if (stat_key) {
        if (std::strcmp(stat_key, "f") == 0) stat = &fullness_;
        else if (std::strcmp(stat_key, "ha") == 0) stat = &happiness_;
        else if (std::strcmp(stat_key, "he") == 0) stat = &health_;
        else if (std::strcmp(stat_key, "e") == 0) stat = &energy_;
    }
    if (stat && amount != 0) {
        *stat = clamp(static_cast<int64_t>(*stat) + amount);
        dirty_ = true;
    }
    add_coins(coin_delta);
}
void Pet::add_coins(int delta)
{
    if (delta == 0) return;
    const int64_t sum = static_cast<int64_t>(coins_) + delta;
    coins_ = sum < 0 ? 0 : sum > INT_MAX ? INT_MAX : static_cast<int>(sum);
    dirty_ = true;
}
bool Pet::spend_coins(int amount)
{
    if (amount < 0 || coins_ < amount) return false;
    if (amount == 0) return true;
    coins_ -= amount;
    dirty_ = true;
    return true;
}
void Pet::recompute_level()
{
    level_ = age_ticks_ / kLevelTicks + 1;
}
void Pet::recompute_stage()
{
    if (stage_ == LifeStage::Tombstone) return;
    recompute_level();
    LifeStage next = stage_for_age_ticks(age_ticks_);
    if (next == LifeStage::Adult && neglected(fullness_, happiness_, energy_)) {
        ++low_stats_ticks_;
        if (low_stats_ticks_ >= kLowStatTicksForTombstone) next = LifeStage::Tombstone;
    } else low_stats_ticks_ = 0;
    if (next != stage_) {
        stage_ = next;
        stage_entered_tick_ = age_ticks_;
        just_changed_stage_ = true;
        if (stage_ == LifeStage::Tombstone) sleeping_ = false;
        ESP_LOGI(TAG, "Stage changed to %s", life_stage_name(stage_));
    }
}
bool Pet::consume_stage_changed()
{
    const bool changed = just_changed_stage_;
    just_changed_stage_ = false;
    return changed;
}
void Pet::revive_from_tombstone()
{
    if (stage_ != LifeStage::Tombstone) return;
    fullness_ = happiness_ = energy_ = health_ = 50;
    age_ticks_ = 0;
    level_ = 1;
    stage_ = LifeStage::Egg;
    stage_entered_tick_ = low_stats_ticks_ = 0;
    sleeping_ = false;
    just_changed_stage_ = true;
    dirty_ = true;
    last_update_tick_ = xTaskGetTickCount();
}
void Pet::record_open_day(int today_epoch_day)
{
    if (today_epoch_day <= 0 || today_epoch_day < last_open_day_) return;
    if (today_epoch_day == last_open_day_) {
        if (streak_ == 0) streak_ = 1;
        return;
    }
    if (last_open_day_ > 0 && today_epoch_day - last_open_day_ == 1)
        streak_ = streak_ < INT_MAX ? streak_ + 1 : INT_MAX;
    else streak_ = 1;
    last_open_day_ = today_epoch_day;
    dirty_ = true;
}
void Pet::decay_tick()
{
    // A dead pet cannot silently become Adult again on the next tick.
    if (stage_ == LifeStage::Tombstone) return;
    if (p50()) fullness_ = clamp(fullness_ - 1);
    if (!sleeping_ && p50()) happiness_ = clamp(happiness_ - 1);
    if (p50()) energy_ = clamp(energy_ + (sleeping_ ? 5 : -1));
    if (neglected(fullness_, happiness_, energy_)) {
        if (p50()) health_ = clamp(health_ - 1);
    } else if (health_ < 100 && p50()) health_ = clamp(health_ + 1);
    if (age_ticks_ < INT_MAX) ++age_ticks_;
    recompute_stage();
    dirty_ = true;
}
void Pet::update()
{
    // Unsigned tick subtraction supports the FreeRTOS counter wrapping.
    const TickType_t now = xTaskGetTickCount();
    constexpr TickType_t interval = pdMS_TO_TICKS(kAgeTickMs);
    static_assert(interval > 0, "FreeRTOS tick interval must be positive");
    TickType_t elapsed = now - last_update_tick_;
    while (elapsed >= interval) {
        decay_tick();
        elapsed -= interval;
        last_update_tick_ += interval;
    }
}
} // namespace pet
