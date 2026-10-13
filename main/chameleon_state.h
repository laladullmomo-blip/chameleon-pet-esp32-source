#pragma once

#include <cstdint>
#include <cstddef>

// Dependency-free care and forest-life rules shared with the desktop design.
// Times in the public API are monotonic milliseconds, never calendar epochs.
namespace ch {

enum class Scene : uint8_t { Forest = 0, Clearing, Night, Meadow, MeadowNight };
enum class Item : uint8_t { Cricket = 0, Cockroach, Mealworm, Fly, Medicine, Soap, Ball, ItemCount };
enum class Pose : uint8_t {
    Idle = 0, Happy, Eat, Sleep, Walk, Blink, Stretch, Wave, Dizzy, Angry,
    Sick, Bath, Pet, Grip, Shelter
};
enum class SleepReason : uint8_t { None = 0, Manual, Routine, Nap };
enum class Weather : uint8_t { Unknown = 0, Clear, Cloudy, Rain, Storm, Fog, Snow };
enum class Phase : uint8_t { Dawn = 0, Day, Dusk, Night };
enum class SicknessCause : uint8_t { None = 0, Hunger, Dirt, Exhaustion, Weakness };
enum class GrowthStage : uint8_t { Baby = 0, Teen, Adult };
enum class GrowthSpeed : uint8_t { Normal = 0, Fast };
enum class Encounter : uint8_t { Dew = 0, Rainbow, Firefly };
enum Need : uint8_t { NeedFood = 1, NeedMedicine = 2, NeedClean = 4, NeedRest = 8, NeedCompany = 16 };
enum DailyCare : uint8_t { DailyFeed = 1, DailyClean = 2, DailyPlay = 4 };

constexpr uint32_t kFeedDurationMs = 1400;
constexpr uint32_t kGameDurationMs = 20000;
constexpr int kGameEnergyCost = 2;
constexpr uint32_t kDigestionMs = 120000;
constexpr uint32_t kRecoveryMs = 30000;
constexpr uint32_t kGrowthTeenMs = 7u * 24u * 60u * 60u * 1000u;
constexpr uint32_t kGrowthAdultMs = 14u * 24u * 60u * 60u * 1000u;
constexpr uint32_t kGrowthFastTeenMs = 30u * 60u * 1000u;
constexpr uint32_t kGrowthFastAdultMs = 120u * 60u * 1000u;
constexpr int kItemCount = static_cast<int>(Item::ItemCount);
constexpr uint32_t kAffectionCooldownMs = 60000u;
constexpr uint64_t kCompanionDayMs = 24ull * 60ull * 60ull * 1000ull;
constexpr uint64_t kMaxCompanionMs = 36500ull * kCompanionDayMs;
constexpr uint32_t kRainbowWindowMs = 45u * 60000u;

struct State {
    int health = 82, fullness = 70, energy = 65, mood = 90, cleanliness = 80;
    int coins = 30;
    int inventory[kItemCount] = {3, 3, 2, 4, 1, 2, 1};
    bool sleeping = false;
    SleepReason sleep_reason = SleepReason::None;
    Scene scene = Scene::Forest, awake_scene = Scene::Forest;
    bool auto_life = true, auto_world = true, sound = true;
    int brightness = 80, idle_minutes = 1;
    char name[40] = "卷卷";
    uint32_t meals = 0, strokes = 0;

    // Appended for versioned-save migration; keep the original fields above in order.
    bool scene_locked = false, sick = false;
    SicknessCause sickness_cause = SicknessCause::None;
    uint32_t treatment_ms = 0;
    uint8_t poop_count = 0;
    uint32_t digestion_ms[3] = {}, neglect_ms[3] = {};
    uint32_t care_total = 0, day_key = 0;
    uint8_t daily_mask = 0;
    bool daily_claimed = false;
    uint32_t daily_claims = 0;
    // Monotonic, well-cared-for time. Saved V3; calendar changes never age a pet.
    uint32_t growth_ms = 0;
    GrowthSpeed growth_speed = GrowthSpeed::Normal;
    // V3 ended with three bytes of padding. Reserve them explicitly so its
    // 176-byte prefix cannot overwrite the new V4 values during migration.
    uint8_t v3_reserved[3] = {};
    uint16_t affinity = 0;
    uint32_t affection_cooldown_ms = 0;
    // Actual powered-on companionship includes rest and screen-off time;
    // restores never infer elapsed time from a wall-clock date.
    uint64_t companion_ms = 0;
    uint8_t anniversary_claimed = 0, encounter_claimed = 0;
    uint32_t encounter_total = 0;
    // Retired V4 photo fields retain their byte positions for old-save migration.
    // These are inert layout placeholders: no photo transactions or validation.
    uint8_t retired_photo_receipt = 0;
    uint32_t retired_photo_ids[2] = {};
};
static_assert(offsetof(State, affinity) == 176, "V4 must preserve the complete V3 save prefix");

struct Life {
    Pose pose = Pose::Idle;
    uint64_t start = 0;
    uint32_t duration = 0;
    float x = 0;
    bool has_action = false, gripping = false;
    const char *note = "静静地观察森林";
};

bool is_branch(Scene scene);
bool is_food(Item item);
int item_price(Item item);
const char *item_name(Item item);
const char *sickness_name(SicknessCause cause);
const char *affinity_name(uint16_t affinity);
const char *encounter_name(Encounter kind);
Phase phase_for_hour(int hour);
Scene effective_scene(const State &state, int valid_local_hour);

class Model {
public:
    State state;

    Model();
    void reset();
    void restore(const State &saved);
    void tick(uint32_t elapsed_ms);
    bool feed_start(Item item, uint64_t now);
    bool feed_finish(uint64_t now);
    bool cancel_feed();
    bool pet(uint64_t now);
    bool sleep(SleepReason reason = SleepReason::Manual);
    bool wake();
    bool clean(uint64_t now);
    bool clear_poop(uint64_t now);
    bool medicine(uint64_t now);
    bool select_scene(Scene scene);
    void set_world_auto(bool enabled);
    // Only pass a verified local calendar date. Zero marks an unsynchronised clock.
    void sync_day(uint32_t day_key);
    bool day_verified() const { return day_verified_; }
    bool daily_ready() const;
    bool claim_daily(uint64_t now);
    // UI supplies a verified, fresh weather sample. Rain history is session
    // local: rebooting or losing the clock cannot manufacture a rainbow.
    void observe_weather(Weather weather, uint64_t now);
    bool encounter_available(Encounter kind, int valid_local_hour, Weather weather,
                             uint64_t now, bool manual_busy = false) const;
    bool complete_encounter(Encounter kind, int valid_local_hour, Weather weather,
                            uint64_t now, bool manual_busy = false);
    uint32_t companion_days() const;
    uint8_t pending_anniversary() const;
    bool complete_anniversary(uint8_t day);
    uint8_t badge_mask() const;
    GrowthStage stage() const;
    uint32_t growth_remaining_ms() const;
    bool growth_paused() const;
    // A new life may choose a speed; once growth starts it cannot be changed.
    bool set_growth_speed(GrowthSpeed speed);
    bool hungry() const { return state.fullness <= 25; }
    bool starving() const { return state.fullness <= 10; }
    uint8_t needs() const;
    bool buy(Item item);
    // Only the ball game requires the reusable toy; the default preserves V4 callers.
    bool start_game(uint64_t now, bool needs_ball = true);
    // The play session verifies its rules before awarding a shared, bounded hit.
    bool award_game_hit(uint64_t now);
    bool hit_game(int target, uint64_t now);
    bool end_game();
    bool walk(uint64_t now);
    bool shake(uint64_t now);
    bool busy(uint64_t now) const;
    bool feeding() const { return feeding_; }
    Item feed_item() const { return feed_item_; }
    uint64_t feed_start_time() const { return feed_started_; }
    bool game_running() const { return game_running_; }
    int game_target() const { return game_target_; }
    int game_hits() const { return game_hits_; }
    int game_seconds_left(uint64_t now) const;
    Pose reaction_pose() const { return reaction_pose_; }
    uint64_t reaction_until() const { return reaction_until_; }
    bool dirty() const { return dirty_; }
    void mark_dirty() { dirty_ = true; }
    void clear_dirty() { dirty_ = false; }
    void set_seed(uint32_t seed);

    // Calendar hour -1 means unsynchronised. Such a clock cannot schedule sleep.
    // Weather must already be validated for freshness by the hardware layer.
    // active controls visible autonomous animations, never sleeping/waking.
    Life life_step(uint64_t now, int valid_local_hour, Weather weather,
                   float tilt_degrees, Scene effective_scene, bool active = true,
                   uint64_t last_interaction = 0, bool manual_busy = false);
    void life_interaction(uint64_t now);
    void reset_life(uint64_t now = 0);

private:
    void sync_stats();
    void publish_stats();
    void add_stats(double health, double fullness, double energy, double mood, double cleanliness);
    void sync_timers();
    void publish_timers();
    void update_sickness();
    void record_care(uint8_t kind);
    void add_affinity(uint16_t amount);
    void set_reaction(Pose pose, uint32_t duration, uint64_t now);
    bool awake_ready() const;
    double random_unit();
    void clear_life_pose();
    void stop_grip();
    void update_position(uint64_t now);
    Life life_output(const char *note) const;

    double stats_[5] = {};
    int published_[5] = {};
    double neglect_[3] = {}, digestion_[3] = {}, recovery_ = 0;
    double growth_ = 0;
    uint32_t published_growth_ = 0;
    uint32_t published_neglect_[3] = {}, published_digestion_[3] = {}, published_recovery_ = 0;
    bool day_verified_ = false;
    bool dirty_ = false;
    uint64_t clock_ = 0;
    bool feeding_ = false;
    Item feed_item_ = Item::Cricket;
    uint64_t feed_started_ = 0;
    bool game_running_ = false;
    int game_target_ = 1, game_hits_ = 0;
    uint64_t game_started_ = 0, last_hit_ = 0;
    bool has_hit_ = false;
    Pose reaction_pose_ = Pose::Idle;
    uint64_t reaction_until_ = 0;
    uint32_t rng_ = 0x4a55414eu;

    Life life_;
    uint64_t life_clock_ = 0, last_interaction_ = 0, seen_interaction_ = 0;
    uint64_t next_action_ = 0, wake_grace_until_ = 0, nap_grace_until_ = 0;
    uint64_t nap_started_ = 0, tilt_stable_at_ = 0;
    uint32_t quiet_ms_ = 10000;
    float walk_from_ = 0, walk_to_ = 0;
    int last_choice_ = -1;
    bool gripping_ = false, tilt_stable_ = false, nap_tracked_ = false;
    bool friendly_approach_ = false;
    bool last_sleeping_ = false;
    SleepReason last_sleep_reason_ = SleepReason::None;
    bool weather_seen_ = false;
    Weather previous_weather_ = Weather::Unknown;
    uint64_t weather_clock_ = 0, rainbow_until_ = 0;
};

} // namespace ch
