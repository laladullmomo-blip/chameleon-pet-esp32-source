#include "chameleon_state.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace ch {
namespace {
constexpr uint32_t kMaxTickMs = 6u * 60u * 60u * 1000u;
constexpr uint64_t kWakeGraceMs = 30u * 60u * 1000u;
constexpr uint64_t kNapGraceMs = 30u * 60u * 1000u;
constexpr uint64_t kMaxNapMs = 60u * 60u * 1000u;
constexpr uint32_t kNeglectMs[3] = {60u * 60000u, 90u * 60000u, 120u * 60000u};
constexpr double kHourMs = 3600000.0;
// Board play should fit a normal day without constant naps. Rest still
// recovers at the established rate and uses the same care/growth thresholds.
constexpr double kAwakeEnergyPerHour = -2.5;
constexpr double kSleepEnergyPerHour = 20.0;
constexpr uint32_t kGrowthCheckpointMs = 5u * 60000u;
constexpr int kPrices[kItemCount] = {6, 7, 5, 3, 12, 8, 15};
constexpr int kFoodFullness[4] = {25, 30, 18, 12};
constexpr int kFoodMood[4] = {3, 2, 6, 4};
constexpr const char *kItemNames[kItemCount] = {
    "蟋蟀", "蟑螂", "面包虫", "苍蝇", "药水", "泡泡皂", "彩色球"
};

template <typename T> T bounded(T value, T low, T high) {
    return std::max(low, std::min(value, high));
}
uint64_t deadline(uint64_t now, uint64_t after) {
    return now > UINT64_MAX - after ? UINT64_MAX : now + after;
}
bool valid_item(Item item) { return static_cast<unsigned>(item) < kItemCount; }
bool valid_scene(Scene scene) { return static_cast<unsigned>(scene) <= static_cast<unsigned>(Scene::MeadowNight); }
bool valid_hour(int hour) { return hour >= 0 && hour < 24; }
bool valid_encounter(Encounter kind) { return static_cast<unsigned>(kind) <= static_cast<unsigned>(Encounter::Firefly); }
bool wet_weather(Weather weather) { return weather == Weather::Rain || weather == Weather::Storm; }
bool rainbow_weather(Weather weather) { return weather == Weather::Clear || weather == Weather::Cloudy; }
bool valid_growth_speed(GrowthSpeed speed) { return speed == GrowthSpeed::Normal || speed == GrowthSpeed::Fast; }
uint32_t growth_teen_ms(GrowthSpeed speed) { return speed == GrowthSpeed::Fast ? kGrowthFastTeenMs : kGrowthTeenMs; }
uint32_t growth_adult_ms(GrowthSpeed speed) { return speed == GrowthSpeed::Fast ? kGrowthFastAdultMs : kGrowthAdultMs; }
uint32_t counter_up(uint32_t value) { return std::min<uint32_t>(1000000u, value + (value < 1000000u ? 1u : 0u)); }
bool valid_day(uint32_t key) {
    const uint32_t year = key / 10000, month = key / 100 % 100, day = key % 100;
    if (year < 2020 || year > 9999 || month < 1 || month > 12 || day < 1) return false;
    constexpr int lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    return day <= static_cast<uint32_t>(lengths[month - 1] + (month == 2 && leap ? 1 : 0));
}

// A persisted label is untrusted. Keep complete UTF-8 code points, remove ASCII
// controls and limit it to the desktop's 12 characters without heap allocation.
void normalize_name(char (&out)[40], const char (&in)[40]) {
    char clean[40] = {};
    size_t used = 0, count = 0;
    for (size_t i = 0; i < 40 && in[i] && count < 12;) {
        const auto lead = static_cast<unsigned char>(in[i]);
        if (lead < 32 || lead == 127) { ++i; continue; }
        size_t length = lead < 128 ? 1 : lead >= 0xc2 && lead <= 0xdf ? 2
            : lead >= 0xe0 && lead <= 0xef ? 3 : lead >= 0xf0 && lead <= 0xf4 ? 4 : 0;
        bool good = length && i + length <= 40 && used + length < sizeof(clean);
        for (size_t j = 1; good && j < length; ++j) {
            const auto byte = static_cast<unsigned char>(in[i + j]);
            good = byte >= 0x80 && byte <= 0xbf;
        }
        if (good && length >= 3) {
            const auto second = static_cast<unsigned char>(in[i + 1]);
            good = !(lead == 0xe0 && second < 0xa0) && !(lead == 0xed && second >= 0xa0)
                && !(lead == 0xf0 && second < 0x90) && !(lead == 0xf4 && second >= 0x90);
        }
        if (!good) { ++i; continue; }
        std::memcpy(clean + used, in + i, length);
        used += length; i += length; ++count;
    }
    if (!used) std::strcpy(clean, "卷卷");
    std::memcpy(out, clean, sizeof(clean));
}
} // namespace

bool is_branch(Scene scene) { return scene == Scene::Forest || scene == Scene::Night; }
bool is_food(Item item) { return static_cast<unsigned>(item) < 4; }
int item_price(Item item) { return valid_item(item) ? kPrices[static_cast<unsigned>(item)] : 0; }
const char *item_name(Item item) { return valid_item(item) ? kItemNames[static_cast<unsigned>(item)] : "未知道具"; }
const char *sickness_name(SicknessCause cause) {
    switch (cause) {
        case SicknessCause::Hunger: return "长时间饥饿";
        case SicknessCause::Dirt: return "环境太脏";
        case SicknessCause::Exhaustion: return "过度疲劳";
        case SicknessCause::Weakness: return "健康虚弱";
        default: return "健康";
    }
}
const char *affinity_name(uint16_t affinity) {
    return affinity >= 80 ? "形影不离" : affinity >= 50 ? "亲近" : affinity >= 20 ? "熟悉" : "初识";
}
const char *encounter_name(Encounter kind) {
    switch (kind) {
        case Encounter::Dew: return "清晨露珠";
        case Encounter::Rainbow: return "雨后彩虹";
        case Encounter::Firefly: return "夜间萤火虫";
        default: return "森林偶遇";
    }
}
Phase phase_for_hour(int hour) {
    if (!valid_hour(hour)) return Phase::Day;
    return hour >= 6 && hour < 8 ? Phase::Dawn : hour >= 8 && hour < 17 ? Phase::Day
        : hour >= 17 && hour < 20 ? Phase::Dusk : Phase::Night;
}
Scene effective_scene(const State &state, int hour) {
    const auto stored = valid_scene(state.scene) ? state.scene : Scene::Forest;
    if (!state.auto_world || state.scene_locked || !valid_hour(hour)) return stored;
    const auto base = stored;
    const bool night = phase_for_hour(hour) == Phase::Night;
    if (base == Scene::Forest || base == Scene::Night) return night ? Scene::Night : Scene::Forest;
    if (base == Scene::Meadow || base == Scene::MeadowNight) return night ? Scene::MeadowNight : Scene::Meadow;
    return Scene::Clearing;
}

Model::Model() { reset(); }
void Model::reset() {
    state = State{};
    day_verified_ = false;
    recovery_ = 0; published_recovery_ = 0;
    growth_ = 0; published_growth_ = 0;
    for (int i = 0; i < 3; ++i) {
        neglect_[i] = digestion_[i] = 0;
        published_neglect_[i] = published_digestion_[i] = 0;
    }
    clock_ = 0; feeding_ = game_running_ = has_hit_ = false;
    feed_started_ = game_started_ = last_hit_ = 0;
    feed_item_ = Item::Cricket; game_target_ = 1; game_hits_ = 0;
    reaction_pose_ = Pose::Idle; reaction_until_ = 0;
    weather_seen_ = false; previous_weather_ = Weather::Unknown;
    weather_clock_ = rainbow_until_ = 0;
    for (auto &value : published_) value = -1;
    sync_stats(); reset_life(); dirty_ = true;
}
void Model::restore(const State &saved) {
    const State original = saved; // restore(state) must survive reset's write to state.
    reset();
    state = original;
    state.health = bounded(state.health, 0, 100);
    state.fullness = bounded(state.fullness, 0, 100);
    state.energy = bounded(state.energy, 0, 100);
    state.mood = bounded(state.mood, 0, 100);
    state.cleanliness = bounded(state.cleanliness, 0, 100);
    state.coins = bounded(state.coins, 0, 999999);
    for (auto &stock : state.inventory) stock = bounded(stock, 0, 99);
    state.brightness = bounded(state.brightness, 10, 100);
    state.idle_minutes = bounded(state.idle_minutes, 0, 60);
    if (!valid_scene(state.scene)) state.scene = Scene::Forest;
    if (!valid_scene(state.awake_scene)) state.awake_scene = state.scene;
    if (!state.sleeping) state.sleep_reason = SleepReason::None;
    else {
        if (state.sleep_reason != SleepReason::Routine && state.sleep_reason != SleepReason::Nap)
            state.sleep_reason = SleepReason::Manual;
    }
    normalize_name(state.name, original.name);
    state.meals = std::min<uint32_t>(state.meals, 1000000u);
    state.strokes = std::min<uint32_t>(state.strokes, 1000000u);
    state.poop_count = std::min<uint8_t>(state.poop_count, 3);
    int pending = state.poop_count;
    for (int i = 0; i < 3; ++i) {
        state.digestion_ms[i] = std::min(state.digestion_ms[i], kDigestionMs);
        if (state.digestion_ms[i] && ++pending > 3) state.digestion_ms[i] = 0;
        state.neglect_ms[i] = std::min(state.neglect_ms[i], kNeglectMs[i]);
    }
    state.treatment_ms = state.sick ? std::min(state.treatment_ms, kRecoveryMs) : 0;
    if (!state.sick) state.sickness_cause = SicknessCause::None;
    else if (state.sickness_cause < SicknessCause::Hunger || state.sickness_cause > SicknessCause::Weakness)
        state.sickness_cause = SicknessCause::Weakness;
    state.care_total = std::min<uint32_t>(state.care_total, 1000000u);
    state.daily_claims = std::min<uint32_t>(state.daily_claims, 1000000u);
    if (!valid_day(state.day_key)) {
        state.day_key = 0; state.daily_mask = 0; state.daily_claimed = false;
        state.encounter_claimed = 0;
    }
    state.daily_mask &= 7;
    state.growth_ms = std::min(state.growth_ms, kGrowthAdultMs);
    if (!valid_growth_speed(state.growth_speed)) state.growth_speed = GrowthSpeed::Normal;
    std::memset(state.v3_reserved, 0, sizeof(state.v3_reserved));
    state.affinity = std::min<uint16_t>(state.affinity, 100);
    state.affection_cooldown_ms = std::min(state.affection_cooldown_ms, kAffectionCooldownMs);
    state.companion_ms = std::min(state.companion_ms, kMaxCompanionMs);
    state.anniversary_claimed &= 3;
    state.encounter_claimed &= 7;
    state.encounter_total = std::min<uint32_t>(state.encounter_total, 1000000u);
    day_verified_ = false;
    for (auto &value : published_) value = -1;
    sync_stats(); sync_timers(); update_sickness(); dirty_ = false;
}
void Model::sync_stats() {
    int *values[] = {&state.health, &state.fullness, &state.energy, &state.mood, &state.cleanliness};
    for (size_t i = 0; i < 5; ++i) {
        if (*values[i] != published_[i]) {
            *values[i] = bounded(*values[i], 0, 100);
            stats_[i] = *values[i]; published_[i] = *values[i];
        }
    }
}
void Model::publish_stats() {
    int *values[] = {&state.health, &state.fullness, &state.energy, &state.mood, &state.cleanliness};
    for (size_t i = 0; i < 5; ++i) {
        stats_[i] = bounded(stats_[i], 0.0, 100.0);
        // Decimal care rates can land a few ULPs below an exact half point.
        // Stabilise display rounding across long and finely split intervals.
        const int value = static_cast<int>(std::lround(stats_[i] + 1e-9));
        if (*values[i] != value) dirty_ = true;
        *values[i] = published_[i] = value;
    }
}
void Model::add_stats(double health, double fullness, double energy, double mood, double cleanliness) {
    sync_stats();
    const double changes[] = {health, fullness, energy, mood, cleanliness};
    for (size_t i = 0; i < 5; ++i) stats_[i] += changes[i];
    publish_stats();
}
void Model::sync_timers() {
    if (!valid_growth_speed(state.growth_speed)) state.growth_speed = GrowthSpeed::Normal;
    if (state.growth_ms != published_growth_) {
        state.growth_ms = std::min(state.growth_ms, kGrowthAdultMs);
        growth_ = state.growth_ms; published_growth_ = state.growth_ms;
    }
    for (int i = 0; i < 3; ++i) {
        if (state.neglect_ms[i] != published_neglect_[i]) {
            state.neglect_ms[i] = std::min(state.neglect_ms[i], kNeglectMs[i]);
            neglect_[i] = state.neglect_ms[i]; published_neglect_[i] = state.neglect_ms[i];
        }
        if (state.digestion_ms[i] != published_digestion_[i]) {
            state.digestion_ms[i] = std::min(state.digestion_ms[i], kDigestionMs);
            digestion_[i] = state.digestion_ms[i]; published_digestion_[i] = state.digestion_ms[i];
        }
    }
    if (state.treatment_ms != published_recovery_) {
        state.treatment_ms = state.sick ? std::min(state.treatment_ms, kRecoveryMs) : 0;
        recovery_ = state.treatment_ms; published_recovery_ = state.treatment_ms;
    }
}
void Model::publish_timers() {
    const auto growth = static_cast<uint32_t>(std::lround(bounded(growth_, 0.0, static_cast<double>(kGrowthAdultMs))));
    // Publish precise progress every tick, but request a periodic flash save
    // only at five-minute growth checkpoints. Care changes keep their own dirty
    // flags, and forced saves always include this up-to-date millisecond value.
    if (state.growth_ms / kGrowthCheckpointMs != growth / kGrowthCheckpointMs) dirty_ = true;
    state.growth_ms = published_growth_ = growth;
    for (int i = 0; i < 3; ++i) {
        const auto neglect = static_cast<uint32_t>(std::lround(bounded(neglect_[i], 0.0, static_cast<double>(kNeglectMs[i]))));
        const auto digestion = static_cast<uint32_t>(std::lround(bounded(digestion_[i], 0.0, static_cast<double>(kDigestionMs))));
        if (state.neglect_ms[i] != neglect || state.digestion_ms[i] != digestion) dirty_ = true;
        state.neglect_ms[i] = published_neglect_[i] = neglect;
        state.digestion_ms[i] = published_digestion_[i] = digestion;
    }
    const auto remaining = static_cast<uint32_t>(std::lround(bounded(recovery_, 0.0, static_cast<double>(kRecoveryMs))));
    if (state.treatment_ms != remaining) dirty_ = true;
    state.treatment_ms = published_recovery_ = remaining;
}
void Model::update_sickness() {
    if (state.sick) return;
    SicknessCause cause = SicknessCause::None;
    if (neglect_[0] >= kNeglectMs[0] - 1e-6) cause = SicknessCause::Hunger;
    else if (neglect_[1] >= kNeglectMs[1] - 1e-6) cause = SicknessCause::Dirt;
    else if (neglect_[2] >= kNeglectMs[2] - 1e-6) cause = SicknessCause::Exhaustion;
    else if (stats_[0] < 40 - 1e-9) cause = SicknessCause::Weakness;
    if (cause != SicknessCause::None) {
        state.sick = true; state.sickness_cause = cause; dirty_ = true;
    }
}
GrowthStage Model::stage() const {
    if (state.growth_ms >= growth_adult_ms(state.growth_speed)) return GrowthStage::Adult;
    return state.growth_ms >= growth_teen_ms(state.growth_speed) ? GrowthStage::Teen : GrowthStage::Baby;
}
uint32_t Model::growth_remaining_ms() const {
    const auto current = stage();
    if (current == GrowthStage::Adult) return 0;
    const uint32_t next = current == GrowthStage::Baby ? growth_teen_ms(state.growth_speed) : growth_adult_ms(state.growth_speed);
    return next - state.growth_ms;
}
bool Model::growth_paused() const {
    if (stage() == GrowthStage::Adult) return false;
    const int values[] = {state.health, state.fullness, state.energy, state.mood, state.cleanliness};
    const auto precise = [&](int index) { return values[index] == published_[index] ? stats_[index] : static_cast<double>(values[index]); };
    return state.sick || precise(0) < 50 - 1e-9 || precise(1) <= 25 + 1e-9
        || precise(4) <= 30 + 1e-9 || state.poop_count >= 3 || precise(2) <= 15 + 1e-9;
}
bool Model::set_growth_speed(GrowthSpeed speed) {
    if (!valid_growth_speed(speed) || state.growth_ms || growth_ > 1e-7) return false;
    if (state.growth_speed != speed) { state.growth_speed = speed; dirty_ = true; }
    return true;
}
void Model::tick(uint32_t elapsed_ms) {
    sync_stats(); sync_timers(); update_sickness();
    const uint32_t elapsed = std::min(elapsed_ms, kMaxTickMs);
    const auto previous_companion = state.companion_ms;
    state.companion_ms = std::min(kMaxCompanionMs, deadline(state.companion_ms, elapsed));
    if (previous_companion / kGrowthCheckpointMs != state.companion_ms / kGrowthCheckpointMs) dirty_ = true;
    // Persist the reward cooldown on the care action and when it expires,
    // without turning every display tick into a flash write request.
    const auto previous_cooldown = state.affection_cooldown_ms;
    state.affection_cooldown_ms -= std::min(state.affection_cooldown_ms, elapsed);
    if (previous_cooldown && !state.affection_cooldown_ms) dirty_ = true;
    clock_ = deadline(clock_, elapsed);
    double remaining = elapsed;
    // Advance exactly to changes in care rates and timers. A long absence and
    // many small UI timer ticks therefore produce the same neglect/recovery.
    while (remaining > 1e-7) {
        // Points per hour; short interactions never imply
        // minutes of neglect or require constant supervision.
        const double fullness_rate = state.sleeping ? -2.0 : -6.0;
        const double cleanliness_rate = (state.sleeping ? -1.0 : -3.0) - .5 * state.poop_count;
        const double energy_rate = state.sleeping ? kSleepEnergyPerHour : kAwakeEnergyPerHour;
        const auto below = [](double value, double threshold, double rate) {
            return value < threshold - 1e-9 || (std::abs(value - threshold) <= 1e-9 && rate < 0);
        };
        const bool hungry = below(stats_[1], 15, fullness_rate);
        const bool dirty = below(stats_[4], 20, cleanliness_rate);
        const bool neglecting[3] = {below(stats_[1], 10, fullness_rate) || stats_[1] == 10,
            dirty || stats_[4] == 20 || state.poop_count >= 3,
            energy_rate <= 0 && (below(stats_[2], 10, energy_rate) || stats_[2] == 10)};
        const bool rested = stats_[2] > 15 || (std::abs(stats_[2] - 15) <= 1e-9 && energy_rate > 0);
        const bool recovering = state.sick && recovery_ > 0
            && stats_[1] > 25 && stats_[4] > 30 && state.poop_count < 3 && rested;
        double rates[5] = {(hungry ? -1.5 : 0) + (dirty ? -1.0 : 0) + (state.poop_count >= 3 ? -.5 : 0)
                + (state.sick ? -2.0 : state.sleeping && !hungry && !dirty ? .5 : 0),
            fullness_rate, energy_rate, state.sleeping ? 1.0 : -3.0, cleanliness_rate};
        if (!state.sick && stats_[0] <= 40 + 1e-9 && rates[0] < 0) {
            state.sick = true; state.sickness_cause = SicknessCause::Weakness;
            rates[0] -= 2.0; dirty_ = true;
        }
        double step = remaining;
        const auto cut = [&step](double until) { if (until > 1e-7 && until < step) step = until; };
        const bool growing = !state.sick && stats_[0] >= 50 - 1e-9 && stats_[1] > 25 + 1e-9
            && stats_[4] > 30 + 1e-9 && state.poop_count < 3 && rested
            && growth_ < growth_adult_ms(state.growth_speed);
        constexpr double boundaries[5][5] = {{0,40,50,100,100}, {0,10,15,25,100},
            {0,10,15,100,100}, {0,100,100,100,100}, {0,20,30,100,100}};
        for (int stat = 0; stat < 5; ++stat) {
            if (!rates[stat]) continue;
            for (const double bound : boundaries[stat]) cut((bound - stats_[stat]) / rates[stat] * kHourMs);
        }
        for (int i = 0; i < 3; ++i) {
            if (digestion_[i] > 0) cut(digestion_[i]);
            if (!state.sick && neglecting[i]) cut(kNeglectMs[i] - neglect_[i]);
        }
        if (recovering) cut(recovery_);
        if (growing) {
            cut(growth_ < growth_teen_ms(state.growth_speed)
                ? growth_teen_ms(state.growth_speed) - growth_ : growth_adult_ms(state.growth_speed) - growth_);
            growth_ = std::min<double>(growth_adult_ms(state.growth_speed), growth_ + step);
        }
        for (int stat = 0; stat < 5; ++stat)
            stats_[stat] = bounded(stats_[stat] + rates[stat] * step / kHourMs, 0.0, 100.0);
        for (int i = 0; i < 3; ++i) {
            neglect_[i] = neglecting[i] ? std::min<double>(kNeglectMs[i], neglect_[i] + step) : 0;
            if (digestion_[i] > 0) {
                digestion_[i] = std::max(0.0, digestion_[i] - step);
                if (digestion_[i] <= 1e-7) {
                    digestion_[i] = 0;
                    state.poop_count = std::min<uint8_t>(3, state.poop_count + 1); dirty_ = true;
                }
            }
        }
        if (recovering) {
            recovery_ = std::max(0.0, recovery_ - step);
            if (recovery_ <= 1e-7) {
                recovery_ = 0; state.sick = false; state.sickness_cause = SicknessCause::None;
                stats_[0] = std::max(45.0, stats_[0]);
                for (auto &exposure : neglect_) exposure = 0;
                dirty_ = true;
            }
        }
        remaining -= step;
        update_sickness();
    }
    publish_stats(); publish_timers();
    if (reaction_until_ <= clock_) { reaction_pose_ = Pose::Idle; reaction_until_ = 0; }
    if (game_running_ && clock_ >= deadline(game_started_, kGameDurationMs)) end_game();
}
void Model::set_reaction(Pose pose, uint32_t duration, uint64_t now) {
    clock_ = std::max(clock_, now); reaction_pose_ = pose; reaction_until_ = deadline(now, duration);
}
bool Model::awake_ready() const { return !state.sleeping && !feeding_ && !game_running_; }
bool Model::busy(uint64_t now) const { return feeding_ || game_running_ || reaction_until_ > now; }
bool Model::feed_start(Item item, uint64_t now) {
    if (!awake_ready() || !is_food(item) || state.inventory[static_cast<unsigned>(item)] <= 0 || state.fullness >= 100) return false;
    clock_ = std::max(clock_, now); feeding_ = true; feed_item_ = item; feed_started_ = now;
    reaction_pose_ = Pose::Idle; reaction_until_ = 0;
    return true;
}
bool Model::feed_finish(uint64_t now) {
    if (!feeding_ || now < feed_started_ || now - feed_started_ < kFeedDurationMs || state.sleeping) return false;
    const auto item = static_cast<unsigned>(feed_item_);
    if (state.inventory[item] <= 0) return false;
    --state.inventory[item]; feeding_ = false;
    add_stats(0, kFoodFullness[item], 0, kFoodMood[item], 0);
    state.meals = counter_up(state.meals); dirty_ = true;
    sync_timers();
    int occupied = state.poop_count;
    for (const auto waiting : digestion_) if (waiting > 0) ++occupied;
    if (occupied < 3) {
        for (auto &waiting : digestion_) if (waiting <= 0) { waiting = kDigestionMs; break; }
        publish_timers();
    }
    record_care(DailyFeed);
    add_affinity(2);
    set_reaction(Pose::Happy, 1600, now);
    return true;
}
bool Model::cancel_feed() {
    if (!feeding_) return false;
    feeding_ = false; return true;
}
bool Model::pet(uint64_t now) {
    if (feeding_ || game_running_) return false;
    add_stats(0, 0, 0, state.sleeping ? 2 : 6, 0);
    state.strokes = counter_up(state.strokes); dirty_ = true;
    clock_ = std::max(clock_, now);
    if (!state.affection_cooldown_ms) {
        add_affinity(1); state.affection_cooldown_ms = kAffectionCooldownMs;
    }
    if (!state.sleeping) set_reaction(Pose::Happy, 1600, now);
    return true;
}
bool Model::sleep(SleepReason reason) {
    if (state.sleeping || feeding_ || game_running_) return false;
    state.awake_scene = state.scene;
    state.sleep_reason = reason == SleepReason::Routine || reason == SleepReason::Nap ? reason : SleepReason::Manual;
    state.sleeping = true; reaction_pose_ = Pose::Idle; reaction_until_ = 0; dirty_ = true;
    return true;
}
bool Model::wake() {
    if (!state.sleeping) return false;
    if (state.sleep_reason == SleepReason::Nap) nap_grace_until_ = deadline(clock_, kNapGraceMs);
    wake_grace_until_ = std::max(wake_grace_until_, deadline(clock_, kWakeGraceMs));
    state.sleeping = false; state.sleep_reason = SleepReason::None;
    set_reaction(Pose::Happy, 1200, clock_); dirty_ = true;
    return true;
}
bool Model::clean(uint64_t now) {
    if (!awake_ready() || state.inventory[static_cast<unsigned>(Item::Soap)] <= 0 || state.cleanliness >= 100) return false;
    --state.inventory[static_cast<unsigned>(Item::Soap)];
    add_stats(0, 0, 0, 3, 35); set_reaction(Pose::Happy, 1800, now); dirty_ = true;
    add_affinity(1);
    return true;
}
bool Model::clear_poop(uint64_t now) {
    if (feeding_ || game_running_ || !state.poop_count) return false;
    state.poop_count = 0;
    sync_timers(); neglect_[1] = 0; publish_timers();
    add_stats(0, 0, 0, 2, 8); record_care(DailyClean);
    add_affinity(1);
    if (!state.sleeping) set_reaction(Pose::Happy, 1200, now);
    dirty_ = true;
    return true;
}
bool Model::medicine(uint64_t now) {
    sync_stats(); sync_timers(); update_sickness();
    if (!awake_ready() || !state.sick || recovery_ > 0 || state.inventory[static_cast<unsigned>(Item::Medicine)] <= 0) return false;
    --state.inventory[static_cast<unsigned>(Item::Medicine)];
    add_stats(20, 0, 0, 0, 0); recovery_ = kRecoveryMs; publish_timers();
    set_reaction(Pose::Happy, 1800, now); dirty_ = true;
    add_affinity(2);
    return true;
}
bool Model::select_scene(Scene scene) {
    if (!valid_scene(scene) || feeding_ || game_running_) return false;
    state.scene = state.awake_scene = scene;
    state.auto_world = false; state.scene_locked = true; reset_life(clock_); dirty_ = true;
    return true;
}
void Model::set_world_auto(bool enabled) {
    state.auto_world = enabled; state.scene_locked = !enabled; dirty_ = true;
}
void Model::sync_day(uint32_t key) {
    day_verified_ = valid_day(key) && (!state.day_key || key >= state.day_key);
    if (!day_verified_) {
        weather_seen_ = false; previous_weather_ = Weather::Unknown; rainbow_until_ = 0;
        return;
    }
    if (key != state.day_key) {
        state.day_key = key; state.daily_mask = 0; state.daily_claimed = false; dirty_ = true;
        state.encounter_claimed = 0;
        weather_seen_ = false; previous_weather_ = Weather::Unknown; rainbow_until_ = 0;
    }
}
void Model::add_affinity(uint16_t amount) {
    const auto affinity = static_cast<uint16_t>(std::min<unsigned>(100, state.affinity + amount));
    if (affinity != state.affinity) { state.affinity = affinity; dirty_ = true; }
}
void Model::observe_weather(Weather weather, uint64_t now) {
    if (!day_verified_ || weather == Weather::Unknown || weather > Weather::Snow) {
        weather_seen_ = false; previous_weather_ = Weather::Unknown; rainbow_until_ = 0;
        weather_clock_ = now; return;
    }
    // A backwards monotonic sample is stale and must not create a new window.
    if (weather_seen_ && now < weather_clock_) return;
    if (weather_seen_ && wet_weather(previous_weather_) && rainbow_weather(weather))
        rainbow_until_ = deadline(now, kRainbowWindowMs);
    else if (!rainbow_weather(weather)) rainbow_until_ = 0;
    previous_weather_ = weather; weather_seen_ = true; weather_clock_ = now;
}
bool Model::encounter_available(Encounter kind, int hour, Weather weather, uint64_t now, bool manual_busy) const {
    if (!valid_encounter(kind) || !day_verified_ || !valid_hour(hour) || state.sleeping || state.sick
        || state.health < 40 || !awake_ready() || busy(now) || manual_busy
        || (state.encounter_claimed & (1u << static_cast<unsigned>(kind)))) return false;
    switch (kind) {
        case Encounter::Dew:
            return hour >= 6 && hour < 10 && (rainbow_weather(weather) || weather == Weather::Fog);
        case Encounter::Rainbow:
            return hour >= 6 && hour < 20 && rainbow_weather(weather) && weather_seen_
                && now >= weather_clock_ && rainbow_until_ && now < rainbow_until_;
        case Encounter::Firefly:
            return (hour >= 20 || hour < 6) && (rainbow_weather(weather) || weather == Weather::Fog);
        default: return false;
    }
}
bool Model::complete_encounter(Encounter kind, int hour, Weather weather, uint64_t now, bool manual_busy) {
    if (!encounter_available(kind, hour, weather, now, manual_busy)) return false;
    constexpr int rewards[] = {1, 3, 2};
    state.encounter_claimed |= 1u << static_cast<unsigned>(kind);
    state.encounter_total = counter_up(state.encounter_total);
    state.coins = std::min(999999, state.coins + rewards[static_cast<unsigned>(kind)]);
    add_stats(0, 0, 0, 2, 0); add_affinity(1);
    set_reaction(Pose::Happy, 1600, now); life_interaction(now); dirty_ = true;
    return true;
}
uint32_t Model::companion_days() const { return static_cast<uint32_t>(state.companion_ms / kCompanionDayMs); }
uint8_t Model::pending_anniversary() const {
    if (stage() != GrowthStage::Adult) return 0;
    if (!(state.anniversary_claimed & 1) && state.companion_ms >= 21ull * kCompanionDayMs) return 21;
    if (!(state.anniversary_claimed & 2) && state.companion_ms >= 30ull * kCompanionDayMs) return 30;
    return 0;
}
bool Model::complete_anniversary(uint8_t day) {
    if ((day != 21 && day != 30) || pending_anniversary() != day) return false;
    state.anniversary_claimed |= day == 21 ? 1 : 2;
    state.coins = std::min(999999, state.coins + (day == 21 ? 10 : 15));
    add_affinity(3); dirty_ = true; return true;
}
void Model::record_care(uint8_t kind) {
    state.care_total = counter_up(state.care_total);
    if (day_verified_) state.daily_mask |= kind & 7;
    dirty_ = true;
}
bool Model::daily_ready() const { return day_verified_ && (state.daily_mask & 7) == 7 && !state.daily_claimed; }
bool Model::claim_daily(uint64_t now) {
    if (!daily_ready()) return false;
    state.daily_claimed = true; state.daily_claims = counter_up(state.daily_claims);
    state.coins = std::min(999999, state.coins + 10);
    add_stats(0, 0, 0, 3, 0);
    if (!state.sleeping && !feeding_ && !game_running_) set_reaction(Pose::Happy, 1600, now);
    dirty_ = true; return true;
}
uint8_t Model::badge_mask() const {
    return (state.care_total >= 10 ? 1 : 0) | (state.care_total >= 50 ? 2 : 0) | (state.care_total >= 100 ? 4 : 0);
}
uint8_t Model::needs() const {
    return (hungry() ? NeedFood : 0) | (state.sick ? NeedMedicine : 0)
        | (state.poop_count || state.cleanliness <= 30 ? NeedClean : 0)
        | (state.energy <= 20 ? NeedRest : 0) | (state.mood <= 35 ? NeedCompany : 0);
}
bool Model::buy(Item item) {
    if (!valid_item(item)) return false;
    const auto index = static_cast<unsigned>(item);
    if (state.inventory[index] >= 99 || state.coins < kPrices[index]) return false;
    state.coins -= kPrices[index]; ++state.inventory[index]; dirty_ = true;
    return true;
}
bool Model::start_game(uint64_t now, bool needs_ball) {
    sync_stats(); sync_timers(); update_sickness();
    if (!awake_ready() || state.sick || starving()
        || (needs_ball && state.inventory[static_cast<unsigned>(Item::Ball)] <= 0) || state.energy < 10) return false;
    add_stats(0, 0, -kGameEnergyCost, 0, 0); game_running_ = true; game_started_ = now;
    game_target_ = 1; game_hits_ = 0; last_hit_ = 0; has_hit_ = false;
    reaction_pose_ = Pose::Idle; reaction_until_ = 0; clock_ = std::max(clock_, now);
    return true;
}
bool Model::award_game_hit(uint64_t now) {
    if (!game_running_ || now < game_started_ || now - game_started_ >= kGameDurationMs
        || game_hits_ >= 40 || (has_hit_ && (now < last_hit_ || now - last_hit_ < 250))) return false;
    ++game_hits_; last_hit_ = now; has_hit_ = true;
    return true;
}
bool Model::hit_game(int target, uint64_t now) {
    if (target != game_target_ || !award_game_hit(now)) return false;
    // Alternate increments of one and two cancel modulo three, making one
    // insect impossible to catch. Visit all three targets without repeats.
    game_target_ = (game_target_ + 1) % 3;
    return true;
}
bool Model::end_game() {
    if (!game_running_) return false;
    game_running_ = false; state.coins = std::min(999999, state.coins + game_hits_ * 2);
    add_stats(0, 0, 0, std::min(10, game_hits_ * 2), 0); dirty_ = true;
    if (game_hits_) record_care(DailyPlay);
    if (game_hits_ && clock_ >= game_started_ && clock_ - game_started_ >= kGameDurationMs) add_affinity(2);
    set_reaction(game_hits_ ? Pose::Happy : Pose::Idle, 1600, clock_);
    return true;
}
int Model::game_seconds_left(uint64_t now) const {
    if (!game_running_) return 0;
    if (now <= game_started_) return kGameDurationMs / 1000;
    const auto elapsed = now - game_started_;
    return elapsed >= kGameDurationMs ? 0 : static_cast<int>((kGameDurationMs - elapsed + 999) / 1000);
}
bool Model::walk(uint64_t now) {
    if (!awake_ready() || state.energy < 2) return false;
    add_stats(0, 0, -2, 2, 0); set_reaction(Pose::Walk, 2200, now); return true;
}
bool Model::shake(uint64_t now) {
    if (!awake_ready()) return false;
    add_stats(0, 0, -2, -5, 0); set_reaction(Pose::Dizzy, 2500, now); return true;
}

void Model::set_seed(uint32_t seed) { rng_ = seed ? seed : 1; }
double Model::random_unit() {
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return rng_ / 4294967296.0;
}
void Model::clear_life_pose() {
    life_.has_action = false; life_.pose = Pose::Idle; life_.duration = 0; friendly_approach_ = false;
}
void Model::stop_grip() { gripping_ = false; tilt_stable_ = false; }
void Model::update_position(uint64_t now) {
    if (life_.has_action && life_.pose == Pose::Walk && life_.duration) {
        const double progress = bounded((now >= life_.start ? now - life_.start : 0) / static_cast<double>(life_.duration), 0.0, 1.0);
        life_.x = bounded(static_cast<float>(walk_from_ + (walk_to_ - walk_from_) * progress), -24.f, 24.f);
    }
}
Life Model::life_output(const char *note) const {
    auto result = life_; result.note = note; result.gripping = gripping_; return result;
}
void Model::reset_life(uint64_t now) {
    life_ = Life{}; life_clock_ = last_interaction_ = seen_interaction_ = now;
    quiet_ms_ = 8000 + static_cast<uint32_t>(random_unit() * 4000);
    next_action_ = deadline(now, quiet_ms_); wake_grace_until_ = nap_grace_until_ = 0;
    nap_started_ = tilt_stable_at_ = 0; walk_from_ = walk_to_ = 0; last_choice_ = -1;
    gripping_ = tilt_stable_ = nap_tracked_ = last_sleeping_ = false;
    friendly_approach_ = false;
    last_sleep_reason_ = SleepReason::None;
}
void Model::life_interaction(uint64_t now) {
    now = std::max(now, life_clock_); update_position(now); life_clock_ = now;
    last_interaction_ = now; seen_interaction_ = std::max(seen_interaction_, now);
    quiet_ms_ = 8000 + static_cast<uint32_t>(random_unit() * 4000);
    next_action_ = deadline(now, quiet_ms_); wake_grace_until_ = deadline(now, kWakeGraceMs);
    clear_life_pose(); stop_grip();
}
Life Model::life_step(uint64_t now, int hour, Weather weather, float tilt, Scene scene,
                      bool active, uint64_t last_interaction, bool manual_busy) {
    (void)tilt; // Legacy call signature retained; tilting no longer affects life.
    sync_stats(); sync_timers(); update_sickness();
    now = std::max(now, life_clock_); update_position(now); life_clock_ = now;
    clock_ = std::max(clock_, now);
    observe_weather(weather, now);
    if (last_interaction > seen_interaction_) life_interaction(last_interaction);
    if (last_sleeping_ && !state.sleeping) {
        if (last_sleep_reason_ == SleepReason::Nap) nap_grace_until_ = deadline(now, kNapGraceMs);
        wake_grace_until_ = std::max(wake_grace_until_, deadline(now, kWakeGraceMs));
    }
    if (state.sleeping && state.sleep_reason == SleepReason::Nap && (!last_sleeping_ || last_sleep_reason_ != SleepReason::Nap || !nap_tracked_)) {
        nap_started_ = now; nap_tracked_ = true;
    }
    if (!state.sleeping) nap_tracked_ = false;
    last_sleeping_ = state.sleeping; last_sleep_reason_ = state.sleep_reason;
    if (manual_busy || busy(now)) {
        clear_life_pose(); stop_grip(); next_action_ = std::max(next_action_, deadline(now, quiet_ms_));
        return life_output("等当前互动完成");
    }
    if (!state.auto_life) {
        clear_life_pose(); stop_grip();
        if (state.sleeping && state.sleep_reason != SleepReason::Manual) wake();
        return life_output("自主生活已暂停");
    }
    if (state.sleeping) {
        clear_life_pose(); stop_grip();
        const bool morning = state.auto_world && valid_hour(hour) && hour >= 7 && hour < 21;
        const bool rested = state.sleep_reason == SleepReason::Nap && (stats_[2] >= 35 - 1e-9 || (now >= nap_started_ && now - nap_started_ >= kMaxNapMs));
        if ((state.sleep_reason == SleepReason::Routine && morning) || rested) {
            wake(); return life_output("休息好了，醒来看看森林");
        }
        return life_output(state.sleep_reason == SleepReason::Nap ? "在原来的栖息地打个小盹" : "安静睡觉，恢复精力");
    }
    if (state.sick || state.health < 40 || state.mood < 35 || starving()) {
        clear_life_pose(); stop_grip(); next_action_ = std::max(next_action_, deadline(now, quiet_ms_));
        return life_output("先休息一下，等你来照顾");
    }
    stop_grip();
    const bool quiet = now >= last_interaction_ && now - last_interaction_ >= quiet_ms_;
    const bool night = state.auto_world && valid_hour(hour) && (hour >= 21 || hour < 7);
    if (quiet) {
        if (night && now >= wake_grace_until_) {
            clear_life_pose(); sleep(SleepReason::Routine); return life_output("夜深了，慢慢睡着");
        }
        if (state.energy < 18 && now >= nap_grace_until_) {
            clear_life_pose(); sleep(SleepReason::Nap); return life_output("有点困，在这里打个小盹");
        }
    }
    // Rest is a physiological rule even while a menu is open or the screen
    // is off. Only visible wandering/gestures depend on a displayed scene.
    if (!active) {
        clear_life_pose(); next_action_ = std::max(next_action_, deadline(now, quiet_ms_));
        return life_output("正在查看菜单");
    }
    if (weather == Weather::Rain || weather == Weather::Storm) {
        if (!life_.has_action || life_.pose != Pose::Shelter) {
            clear_life_pose(); life_.has_action = true; life_.pose = Pose::Shelter; life_.start = now; life_.duration = 1800;
        }
        next_action_ = deadline(now, quiet_ms_);
        return life_output(weather == Weather::Storm ? "伏低身体，在叶子下躲雷雨" : "在叶子下面静静避雨");
    }
    if (life_.has_action && (life_.pose == Pose::Grip || life_.pose == Pose::Shelter)) {
        clear_life_pose(); next_action_ = deadline(now, quiet_ms_);
    }
    if (life_.has_action && now >= deadline(life_.start, life_.duration)) {
        clear_life_pose(); next_action_ = deadline(now, 8000 + static_cast<uint32_t>(random_unit() * 8000));
    }
    if (!quiet) { clear_life_pose(); return life_output("安静地陪着你"); }
    const bool friendly = state.affinity >= 50 && rainbow_weather(weather);
    if (!life_.has_action && now >= next_action_) {
        // Familiar pets greet their companion more often. Preserve the exact
        // original five-way choice and RNG consumption below the threshold.
        int choice = static_cast<int>(random_unit() * (friendly ? 7 : 5));
        if (choice >= 5) choice = 3; // Three of seven tickets are a wave.
        if (choice == last_choice_) choice = (choice + 1) % 5;
        last_choice_ = choice;
        constexpr Pose poses[5] = {Pose::Walk, Pose::Stretch, Pose::Blink, Pose::Wave, Pose::Idle};
        constexpr uint32_t durations[5] = {2400, 1800, 800, 1600, 2600};
        life_.has_action = true; life_.pose = poses[choice]; life_.duration = durations[choice]; life_.start = now;
        if (choice == 0) {
            walk_from_ = life_.x;
            friendly_approach_ = friendly && state.affinity >= 80 && std::abs(life_.x) >= 4 && random_unit() < .75;
            if (friendly_approach_) walk_to_ = 0;
            else {
                const float direction = life_.x >= 12 ? -1 : life_.x <= -12 ? 1 : random_unit() < .5 ? -1 : 1;
                walk_to_ = bounded(walk_from_ + direction * static_cast<float>(12 + random_unit() * 12), -24.f, 24.f);
            }
        }
    }
    if (life_.has_action) {
        switch (life_.pose) {
            case Pose::Walk:
                return life_output(friendly_approach_
                    ? is_branch(scene) ? "想靠近你，慢慢走到树枝中间" : "想靠近你，慢慢走到草地中间"
                    : is_branch(scene) ? "沿着树枝慢慢走几步" : "在草地上慢慢走几步");
            case Pose::Stretch: return life_output("伸个懒腰，舒展一下身体");
            case Pose::Blink: return life_output("眨眨眼，享受小森林");
            case Pose::Wave: return life_output(friendly ? "熟悉你后，挥挥小爪子" : "向你挥挥小爪子");
            default: return life_output("抬头看看飞过的小虫");
        }
    }
    return life_output(valid_hour(hour) ? "静静地观察森林里的小动静" : "静静观察森林；时钟尚未校准");
}
} // namespace ch
