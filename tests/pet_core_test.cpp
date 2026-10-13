#include "pet_state.h"
#include "pet_save.h"
#include "pet_frames.h"
#include <climits>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
TickType_t clock_tick = 0;
uint32_t random_value = 1;
std::map<std::string, std::vector<uint8_t>> storage;
esp_err_t commit_result = ESP_OK;
esp_err_t write_result = ESP_OK;
int writes = 0;

void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}
pet::Pet &fresh()
{
    clock_tick = 0;
    random_value = 1; // Skip stochastic stat changes; age always advances.
    storage.clear();
    commit_result = write_result = ESP_OK;
    writes = 0;
    auto &pet = pet::Pet::instance();
    pet.init();
    return pet;
}
void advance(uint32_t ms)
{
    clock_tick += pdMS_TO_TICKS(ms);
    pet::Pet::instance().update();
}
void restore_age(pet::Pet &p, int age, int fullness = 100, int happiness = 100,
                 int energy = 100, int stage = -1, int low_ticks = 0)
{
    p.apply_snapshot(fullness, happiness, energy, 100, 0, age / 30 + 1,
                     age, false, stage, 0, 0, low_ticks);
}
void same_stats(const pet::State &a, const pet::State &b)
{
    require(a.fullness == b.fullness && a.happiness == b.happiness &&
            a.energy == b.energy && a.health == b.health, "stats unexpectedly changed");
}
struct LegacyV1 {
    int32_t fullness, happiness, energy, health, coins, level, age;
    uint8_t sleeping;
} __attribute__((packed));
struct LegacyV2 {
    LegacyV1 state;
    uint8_t stage, pad0, pad1, pad2;
    int32_t entered, day;
} __attribute__((packed));
template <typename T> void store_blob(const char *key, const T &value)
{
    const auto *bytes = reinterpret_cast<const uint8_t *>(&value);
    storage[key] = std::vector<uint8_t>(bytes, bytes + sizeof(T));
}
} // namespace

TickType_t xTaskGetTickCount() { return clock_tick; }
uint32_t esp_random() { return random_value; }
esp_err_t nvs_open(const char *, int, nvs_handle_t *handle)
{
    *handle = 1;
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t, const char *key, void *out, size_t *size)
{
    auto item = storage.find(key);
    if (item == storage.end()) return ESP_ERR_NVS_NOT_FOUND;
    if (!out) { *size = item->second.size(); return ESP_OK; }
    if (*size < item->second.size()) return ESP_ERR_NVS_INVALID_LENGTH;
    std::memcpy(out, item->second.data(), item->second.size());
    *size = item->second.size();
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t, const char *key, const void *data, size_t size)
{
    if (write_result != ESP_OK) return write_result;
    ++writes;
    const auto *bytes = reinterpret_cast<const uint8_t *>(data);
    storage[key] = std::vector<uint8_t>(bytes, bytes + size);
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t) { return commit_result; }
esp_err_t nvs_erase_key(nvs_handle_t, const char *key)
{
    return storage.erase(key) ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}

int main()
{
    const std::vector<std::pair<const char *, std::function<void()>>> cases = {
        {"action bounds and extreme input", [] {
            auto &p = fresh();
            for (int i = 0; i < 10; ++i) { p.feed(); p.pet(); }
            auto s = p.get_state();
            require(s.fullness == 100 && s.happiness == 100 && s.energy == 100 &&
                    s.health == 100, "care must clamp stats at 100");
            p.feed_with_amount(INT_MAX); p.drink_energy(INT_MAX); p.take_medicine(INT_MAX);
            p.work_outcome("f", INT_MIN, 0);
            p.work_outcome("ha", INT_MIN, 0);
            p.work_outcome("e", INT_MIN, 0);
            p.work_outcome("he", INT_MIN, 0);
            s = p.get_state();
            require(s.fullness == 0 && s.happiness == 0 && s.energy == 0 && s.health == 0,
                    "extreme negative rewards must clamp without overflow");
            p.work_outcome("ha_extra", INT_MAX, 0);
            p.work_outcome(nullptr, INT_MAX, 0);
            same_stats(s, p.get_state());
            p.feed_with_amount(-1); p.drink_energy(0); p.take_medicine(INT_MIN);
            same_stats(s, p.get_state());
        }},
        {"currency saturation and invalid purchases", [] {
            auto &p = fresh();
            p.add_coins(INT_MAX); p.add_coins(50);
            require(p.get_state().coins == INT_MAX, "currency overflowed");
            require(!p.spend_coins(-5), "negative purchases must be rejected");
            require(p.spend_coins(INT_MAX), "exact balance purchase failed");
            require(!p.spend_coins(1) && p.spend_coins(0), "empty balance handling failed");
            p.add_coins(INT_MIN);
            require(p.get_state().coins == 0, "currency must floor at zero");
            p.work_outcome("", 0, INT_MAX); p.work_outcome("", 0, INT_MAX);
            require(p.get_state().coins == INT_MAX, "game reward overflowed");
            p.work_outcome("", 0, INT_MIN);
            require(p.get_state().coins == 0, "game cost overflowed");
        }},
        {"120 second timing and remainder", [] {
            auto &p = fresh();
            advance(119990);
            require(p.get_state().age_ticks == 0, "pet aged before 120 seconds");
            advance(10);
            require(p.get_state().age_ticks == 1, "120 second age tick missing");
            advance(240010);
            require(p.get_state().age_ticks == 3, "elapsed catch-up incorrect");
            p.update();
            require(p.get_state().age_ticks == 3, "same clock tick aged twice");
            advance(119990);
            require(p.get_state().age_ticks == 4, "partial interval discarded");
        }},
        {"FreeRTOS tick wrap", [] {
            auto &p = fresh();
            clock_tick = UINT32_MAX - pdMS_TO_TICKS(10000);
            p.init();
            advance(pet::Pet::kAgeTickMs);
            require(p.get_state().age_ticks == 1, "clock wrapping lost age tick");
        }},
        {"stage thresholds and one-shot notification", [] {
            auto &p = fresh();
            const int before[] = {9, 89, 179, 269};
            const pet::LifeStage after[] = {pet::LifeStage::Baby, pet::LifeStage::Child,
                                          pet::LifeStage::Teen, pet::LifeStage::Adult};
            for (int i = 0; i < 4; ++i) {
                restore_age(p, before[i]);
                require(!p.consume_stage_changed(), "restore must not replay a stage event");
                advance(pet::Pet::kAgeTickMs);
                require(p.stage() == after[i], "stage boundary failed");
                require(p.consume_stage_changed() && !p.consume_stage_changed(),
                        "stage notification must be consumed once");
                require(p.stage_entered_tick() == before[i] + 1, "stage entry age incorrect");
            }
            restore_age(p, 29);
            advance(pet::Pet::kAgeTickMs);
            require(p.get_state().level == 2, "one level per 30 age ticks failed");
        }},
        {"snapshot normalization", [] {
            auto &p = fresh();
            p.apply_snapshot(-5, INT_MAX, -1, 101, -20, INT_MAX, -1,
                             true, 999, INT_MAX, -5, INT_MAX);
            auto s = p.get_state();
            require(s.fullness == 0 && s.happiness == 100 && s.energy == 0 && s.health == 100,
                    "invalid stats survived restore");
            require(s.coins == 0 && s.level == 1 && s.age_ticks == 0 &&
                    p.stage() == pet::LifeStage::Egg, "invalid progression survived restore");
            require(p.stage_entered_tick() == 0 && p.last_open_day() == 0 &&
                    p.low_stats_ticks() == 0 && p.is_dirty(), "normalized save must be dirty");
            restore_age(p, INT_MAX);
            advance(pet::Pet::kAgeTickMs);
            require(p.get_state().age_ticks == INT_MAX && p.get_state().level == INT_MAX / 30 + 1,
                    "maximum age overflowed");
            restore_age(p, 9, 100, 100, 100, static_cast<int>(pet::LifeStage::Tombstone));
            require(p.stage() == pet::LifeStage::Egg, "juvenile snapshot must not be tombstoned");
        }},
        {"restore excludes powered-off time", [] {
            auto &p = fresh();
            clock_tick = pdMS_TO_TICKS(86400000);
            restore_age(p, 30);
            p.update();
            require(p.get_state().age_ticks == 30, "off time counted as age");
            advance(119990);
            require(p.get_state().age_ticks == 30, "restore time anchor incorrect");
            advance(10);
            require(p.get_state().age_ticks == 31, "active time after restore ignored");
        }},
        {"sleep regeneration and idempotent wake", [] {
            auto &p = fresh();
            random_value = 0;
            p.sleep(); p.sleep();
            advance(pet::Pet::kAgeTickMs);
            require(p.is_sleeping() && p.get_state().energy == 75 && p.get_state().happiness == 70,
                    "sleep recovery or happiness decay incorrect");
            p.wake_up();
            const auto awake = p.get_state();
            p.wake_up();
            same_stats(awake, p.get_state());
            require(!p.is_sleeping(), "wake failed");
            p.sleep(); p.play();
            require(!p.is_sleeping(), "play should wake a sleeping pet");
        }},
        {"persistent tombstone and explicit revival", [] {
            auto &p = fresh();
            restore_age(p, 270, 0, 100, 100);
            advance(49 * pet::Pet::kAgeTickMs);
            require(p.stage() == pet::LifeStage::Adult && p.low_stats_ticks() == 49,
                    "tombstone happened before 50 neglected ticks");
            advance(pet::Pet::kAgeTickMs);
            require(p.stage() == pet::LifeStage::Tombstone, "neglected adult did not tombstone");
            const auto dead = p.get_state();
            advance(100 * pet::Pet::kAgeTickMs);
            p.feed(); p.pet(); p.play(); p.sleep(); p.wake_up();
            p.feed_with_amount(100); p.drink_energy(100); p.take_medicine(100);
            p.work_outcome("f", 100, 5);
            same_stats(dead, p.get_state());
            require(p.stage() == pet::LifeStage::Tombstone &&
                    p.get_state().age_ticks == dead.age_ticks && !p.is_sleeping(),
                    "dead pet changed without revival");
            p.add_coins(10);
            p.revive_from_tombstone();
            auto revived = p.get_state();
            require(p.stage() == pet::LifeStage::Egg && revived.age_ticks == 0 &&
                    revived.level == 1 && revived.coins == 10 && revived.fullness == 50,
                    "revival must restart the lifecycle and retain currency");
            advance(pet::Pet::kAgeTickMs);
            require(p.stage() == pet::LifeStage::Egg && p.get_state().age_ticks == 1,
                    "revived egg skipped back to adult");
            p.revive_from_tombstone();
            require(p.get_state().age_ticks == 1, "living pet must not restart by reviving");
        }},
        {"care resets neglected streak", [] {
            auto &p = fresh();
            restore_age(p, 270, 0, 100, 100);
            advance(49 * pet::Pet::kAgeTickMs);
            p.feed();
            advance(pet::Pet::kAgeTickMs);
            require(p.stage() == pet::LifeStage::Adult && p.low_stats_ticks() == 0,
                    "care did not reset continuous neglect counter");
        }},
        {"NVS v3 round trip and dirty writes", [] {
            auto &p = fresh();
            auto &save = pet::PetSave::instance();
            require(save.load(p) == ESP_OK && save.save_if_dirty(p) == ESP_OK && writes == 0,
                    "fresh state must not trigger redundant writes");
            restore_age(p, 90); p.feed(); p.add_coins(42); p.sleep();
            const auto expected = p.get_state();
            require(save.save_if_dirty(p) == ESP_OK && !p.is_dirty(), "successful save did not clear dirty");
            const int saved_writes = writes;
            require(storage["state_v3"].size() == 52 && save.save_if_dirty(p) == ESP_OK &&
                    writes == saved_writes, "save format or dirty gating incorrect");
            p.init(); clock_tick = pdMS_TO_TICKS(86400000);
            require(save.load(p) == ESP_OK, "v3 restore failed");
            same_stats(expected, p.get_state());
            require(p.get_state().coins == 42 && p.get_state().age_ticks == 90 &&
                    p.stage() == pet::LifeStage::Child && p.is_sleeping(), "v3 lost state");
            p.update();
            require(p.get_state().age_ticks == 90, "saved pet decayed while powered off");
        }},
        {"NVS write and commit failures preserve dirty flag", [] {
            auto &p = fresh(); auto &save = pet::PetSave::instance();
            p.feed(); write_result = ESP_FAIL;
            require(save.save_if_dirty(p) == ESP_FAIL && p.is_dirty(), "write failure lost unsaved changes");
            write_result = ESP_OK; commit_result = ESP_FAIL;
            require(save.save_if_dirty(p) == ESP_FAIL && p.is_dirty(), "commit failure lost unsaved changes");
            commit_result = ESP_OK;
            require(save.save_if_dirty(p) == ESP_OK && !p.is_dirty(), "save retry failed");
        }},
        {"NVS corrupt payload and wrong size are rejected", [] {
            auto &p = fresh(); auto &save = pet::PetSave::instance();
            p.feed(); require(save.save_if_dirty(p) == ESP_OK, "setup save failed");
            storage["state_v3"][8] ^= 1;
            p.init();
            require(save.load(p) == ESP_ERR_INVALID_CRC && p.get_state().fullness == 70,
                    "corrupt snapshot changed current state");
            storage["state_v3"].resize(53);
            require(save.load(p) == ESP_ERR_INVALID_SIZE, "wrong sized snapshot accepted");
            storage["state_v3"].resize(52); storage["state_v3"][0] ^= 1;
            require(save.load(p) == ESP_ERR_INVALID_STATE, "unknown save header accepted");
        }},
        {"legacy v1 and v2 migration", [] {
            auto &p = fresh(); auto &save = pet::PetSave::instance();
            LegacyV1 v1{80, 60, 40, 90, 123, 999, 90, 1};
            store_blob("state", v1);
            require(save.load(p) == ESP_OK && p.stage() == pet::LifeStage::Child &&
                    p.get_state().level == 4 && p.get_state().coins == 123 && p.is_sleeping(),
                    "legacy v1 migration lost state or trusted corrupt level");
            require(storage.count("state_v3") == 1, "migration did not create v3");
            fresh();
            LegacyV2 v2{{0, 0, 0, 0, 2, 10, 320, 0}, 5, 0, 0, 0, 320, 20000};
            store_blob("state_v2", v2);
            require(save.load(p) == ESP_OK && p.stage() == pet::LifeStage::Tombstone &&
                    p.last_open_day() == 20000, "legacy v2 migration lost tombstone");
            advance(pet::Pet::kAgeTickMs);
            require(p.stage() == pet::LifeStage::Tombstone, "migrated tombstone revived itself");
        }},
        {"neglect counter survives a save and reboot", [] {
            auto &p = fresh(); auto &save = pet::PetSave::instance();
            restore_age(p, 270, 0, 100, 100);
            advance(49 * pet::Pet::kAgeTickMs);
            require(save.save_if_dirty(p) == ESP_OK, "setup save failed");
            p.init();
            require(save.load(p) == ESP_OK && p.low_stats_ticks() == 49, "reboot reset neglect counter");
            advance(pet::Pet::kAgeTickMs);
            require(p.stage() == pet::LifeStage::Tombstone, "restored neglect did not accumulate");
            require(save.save_if_dirty(p) == ESP_OK, "dead pet save failed");
            p.init();
            require(save.load(p) == ESP_OK && p.stage() == pet::LifeStage::Tombstone,
                    "reboot revived dead pet");
            require(save.clear() == ESP_OK && storage.empty(), "clear left stale legacy saves");
        }},
        {"daily API rejects absent or backward clocks", [] {
            auto &p = fresh();
            p.record_open_day(10); p.record_open_day(10);
            require(p.streak() == 1, "same day double-counted");
            p.record_open_day(11);
            require(p.streak() == 2, "consecutive opening not counted");
            p.record_open_day(0); p.record_open_day(9);
            require(p.streak() == 2 && p.last_open_day() == 11, "invalid clock erased streak");
            p.record_open_day(14);
            require(p.streak() == 1, "gap did not reset streak");
        }},
        {"54 real RGB565A8 frames have valid descriptor and data", [] {
            for (int state = 0; state < PET_ANIM_COUNT; ++state) {
                std::set<uint32_t> hashes;
                for (int frame = 0; frame < PET_FRAME_COUNT; ++frame) {
                    const auto *img = pet_anim_frames[state][frame];
                    require(img && img->header.w == 96 && img->header.h == 96 &&
                            img->header.stride == 192 && img->header.cf == LV_COLOR_FORMAT_RGB565A8 &&
                            img->data_size == 96 * 96 * 3, "frame descriptor invalid");
                    uint32_t hash = 2166136261u;
                    for (uint32_t i = 0; i < img->data_size; ++i) hash = (hash ^ img->data[i]) * 16777619u;
                    hashes.insert(hash);
                }
                require(hashes.size() > 1, "animation must contain distinct real frames");
            }
        }},
    };
    for (const auto &test : cases) {
        try { test.second(); }
        catch (const std::exception &error) {
            std::cerr << "FAIL: " << test.first << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cout << "PASS: " << cases.size() << " core/storage/resource cases; FreeRTOS tick="
              << HOST_TICK_MS << "ms; undefined-behavior sanitizer enabled\n";
    return 0;
}
