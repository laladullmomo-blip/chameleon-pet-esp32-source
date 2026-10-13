// Derived from Ellenp2p/esp32-pet-tamagotchi.
// Copyright (c) 2026 ellenp2p. MIT License; see THIRD_PARTY_LICENSES.
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdint>

namespace pet {

// All four stats use the same convention: 0 = bad, 100 = good.
struct State {
    int fullness;   // 0 = starving, 100 = full
    int happiness;  // 0 = sad, 100 = happy
    int energy;     // 0 = exhausted, 100 = energetic
    int health;     // 0 = sick, 100 = healthy
    int coins;      // currency (biscuits), 0..INT_MAX, saturating
    int level;      // 1..N, derived from age_ticks
    int age_ticks;  // number of 120-second intervals while powered on
    int stage;      // LifeStage cast to int (0..5)
};

// v0.6: Tamagotchi-style 6-stage lifecycle. stage_ is derived from
// age_ticks_ + a tombstone rule (Adult + sustained low stats). UI reads
// pet.stage() after each decay_tick to display the current stage name.
enum class LifeStage : int {
    Egg = 0,        // 0-20 minutes (0..9 age ticks)
    Baby = 1,       // 20 minutes-3 hours (10..89 ticks)
    Child = 2,      // 3-6 hours (90..179 ticks)
    Teen = 3,       // 6-9 hours (180..269 ticks)
    Adult = 4,      // 9+ hours (270+ ticks)
    Tombstone = 5,  // Adult + 50 consecutive neglected ticks (100 minutes)
    Count
};
const char *life_stage_name(LifeStage s);

class Pet {
public:
    static constexpr uint32_t kAgeTickMs = 120000;
    static Pet &instance();

    void init();

    // Get current state
    State get_state() const;

    // Actions
    void feed();           // +25 fullness, +5 energy, +5 happiness
    void feed_with_amount(int amount);  // shop-bought snack: amount=fullness delta
    void drink_energy(int amount);     // shop-bought energy drink: amount=energy delta
    void take_medicine(int amount);    // shop-bought medicine: amount=health delta
    void play();
    void sleep();
    void wake_up();
    void pet();            // care / petting

    // v0.6: one-shot work outcome. The mini-games call this when a round
    // ends. stat_key is one of: "f" (fullness), "ha" (happiness), "e"
    // (energy), "he" (health); empty string means no stat change.
    // amount may be negative (cost) or positive (reward). coin_delta is
    // applied via add_coins (which floors at 0). The stat delta is
    // clamped to 0..100.
    void work_outcome(const char *stat_key, int amount, int coin_delta);

    // Currency
    void add_coins(int delta);
    bool spend_coins(int amount);  // returns false if insufficient

    // Update state based on elapsed time; called periodically
    void update();

    bool is_sleeping() const { return sleeping_; }
    LifeStage stage() const { return stage_; }
    int streak() const { return streak_; }

    // Mark the saved snapshot as dirty so the next pet_task tick flushes it.
    void mark_dirty() { dirty_ = true; }
    bool is_dirty() const { return dirty_; }
    void clear_dirty() { dirty_ = false; }

    // Called by the LVGL timer after update() to discover if the
    // LifeStage just transitioned; the UI pops a "Stage UP!" toast if
    // true. Clears the flag on read.
    bool consume_stage_changed();

    // Revive only a tombstoned pet. Reset age and level, retain coins.
    void revive_from_tombstone();

    // v0.6: Daily Streak. Called by pet_meta::check_streak() at boot.
    // Increments the streak if the user opens the pet on a new day,
    // resets if more than 1 day has passed, no-op if same day. last_open
    // is the epoch-day of the user's last open (0 = never). After this
    // call, get_streak() reflects the new value.
    void record_open_day(int today_epoch_day);

    // Restore a complete snapshot from persistent storage. Used by pet_save.
    // Daily-opening helpers remain for upstream API compatibility; v1 uses
    // no calendar clock and does not expose a daily streak feature.
    // Inputs are normalized; level and living stage are derived from age.
    // Reset the time anchor on restore: no decay during powered-off time.
    void apply_snapshot(int fullness, int happiness, int energy, int health,
                        int coins, int level, int age_ticks, bool sleeping,
                        int stage, int stage_entered_tick, int last_open_day,
                        int low_stats_ticks = 0);

    // Getters used by pet_save to write the v3 snapshot.
    int stage_entered_tick() const { return stage_entered_tick_; }
    int last_open_day() const { return last_open_day_; }
    int low_stats_ticks() const { return low_stats_ticks_; }

private:
    Pet() = default;

    int clamp(int64_t value) const;
    void decay_tick();
    void recompute_level();
    void recompute_stage();  // v0.6: replaces per-decay role of recompute_level

    int fullness_  = 70;
    int happiness_ = 70;
    int energy_    = 70;
    int health_    = 90;
    bool sleeping_ = false;

    int coins_     = 0;
    int level_     = 1;
    int age_ticks_ = 0;

    // v0.6: lifecycle + Daily Streak state
    LifeStage stage_ = LifeStage::Egg;
    int stage_entered_tick_ = 0;
    int last_open_day_ = 0;   // 0 = never opened; otherwise epoch day
    int streak_    = 0;
    bool just_changed_stage_ = false;

    // Internal: low-stats tick counter for tombstone rule.
    int low_stats_ticks_ = 0;

    bool dirty_ = false;

    TickType_t last_update_tick_ = 0;
};

} // namespace pet
