// Derived from Ellenp2p/esp32-pet-tamagotchi.
// Copyright (c) 2026 ellenp2p. MIT License; see THIRD_PARTY_LICENSES.
#include "pet_save.h"
#include "pet_state.h"
#include "esp_log.h"
#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace pet {
namespace {
constexpr const char *TAG = "pet_save";
constexpr const char *kNamespace = "pet_save";
constexpr const char *kKeyV1 = "state";
constexpr const char *kKeyV2 = "state_v2";
constexpr const char *kKeyV3 = "state_v3";
constexpr uint32_t kMagic = 0x33544550; // PET3 in little endian.

// Preserve the exact upstream formats for migration.
struct SnapshotV1 {
    int32_t fullness, happiness, energy, health, coins, level, age_ticks;
    uint8_t sleeping;
} __attribute__((packed));
struct SnapshotV2 {
    SnapshotV1 state;
    uint8_t stage, pad0, pad1, pad2;
    int32_t stage_entered_tick, last_open_day;
} __attribute__((packed));
struct SnapshotV3 {
    uint32_t magic;
    uint16_t version;
    uint8_t sleeping, stage;
    int32_t fullness, happiness, energy, health, coins, level, age_ticks;
    int32_t stage_entered_tick, last_open_day, low_stats_ticks;
    uint32_t crc32;
};
static_assert(sizeof(SnapshotV1) == 29, "Upstream v1 snapshot layout changed");
static_assert(sizeof(SnapshotV2) == 41, "Upstream v2 snapshot layout changed");
static_assert(sizeof(SnapshotV3) == 52, "v3 snapshot layout changed");

uint32_t checksum(const SnapshotV3 &snapshot)
{
    const auto *bytes = reinterpret_cast<const uint8_t *>(&snapshot);
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < offsetof(SnapshotV3, crc32); ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

template <typename T>
esp_err_t read_exact(nvs_handle_t handle, const char *key, T &snapshot)
{
    size_t size = 0;
    esp_err_t err = nvs_get_blob(handle, key, nullptr, &size);
    if (err != ESP_OK) return err;
    if (size != sizeof(T)) return ESP_ERR_INVALID_SIZE;
    return nvs_get_blob(handle, key, &snapshot, &size);
}
} // namespace

PetSave &PetSave::instance() noexcept
{
    static PetSave save;
    return save;
}
esp_err_t PetSave::init() noexcept
{
    if (open_) return ESP_OK;
    const esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }
    open_ = true;
    return ESP_OK;
}
esp_err_t PetSave::load(Pet &pet) noexcept
{
    esp_err_t err = init();
    if (err != ESP_OK) return err;
    SnapshotV3 v3{};
    err = read_exact(handle_, kKeyV3, v3);
    if (err == ESP_OK) {
        if (v3.magic != kMagic || v3.version != 3) return ESP_ERR_INVALID_STATE;
        if (v3.crc32 != checksum(v3)) return ESP_ERR_INVALID_CRC;
        if (v3.sleeping > 1) return ESP_ERR_INVALID_STATE;
        pet.apply_snapshot(v3.fullness, v3.happiness, v3.energy, v3.health,
                           v3.coins, v3.level, v3.age_ticks, v3.sleeping != 0,
                           v3.stage, v3.stage_entered_tick, v3.last_open_day,
                           v3.low_stats_ticks);
        ESP_LOGI(TAG, "Restored v3 snapshot (no powered-off decay)");
        return ESP_OK;
    }
    // A corrupt current save is reported, never silently replaced with an
    // older save that might resurrect a pet or duplicate spent currency.
    if (err != ESP_ERR_NVS_NOT_FOUND) return err;

    SnapshotV2 v2{};
    err = read_exact(handle_, kKeyV2, v2);
    if (err == ESP_OK) {
        if (v2.state.sleeping > 1) return ESP_ERR_INVALID_STATE;
        const auto &s = v2.state;
        pet.apply_snapshot(s.fullness, s.happiness, s.energy, s.health,
                           s.coins, s.level, s.age_ticks, s.sleeping != 0,
                           v2.stage, v2.stage_entered_tick, v2.last_open_day);
        ESP_LOGI(TAG, "Migrating upstream v2 snapshot");
        return save_if_dirty(pet, true);
    }
    if (err != ESP_ERR_NVS_NOT_FOUND) return err;

    SnapshotV1 v1{};
    err = read_exact(handle_, kKeyV1, v1);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    if (v1.sleeping > 1) return ESP_ERR_INVALID_STATE;
    pet.apply_snapshot(v1.fullness, v1.happiness, v1.energy, v1.health,
                       v1.coins, v1.level, v1.age_ticks, v1.sleeping != 0,
                       0, 0, 0);
    ESP_LOGI(TAG, "Migrating upstream v1 snapshot");
    return save_if_dirty(pet, true);
}
esp_err_t PetSave::save_if_dirty(Pet &pet, bool force) noexcept
{
    if (!force && !pet.is_dirty()) return ESP_OK;
    esp_err_t err = init();
    if (err != ESP_OK) return err;
    const State state = pet.get_state();
    SnapshotV3 snapshot{};
    snapshot.magic = kMagic;
    snapshot.version = 3;
    snapshot.sleeping = pet.is_sleeping() ? 1 : 0;
    snapshot.stage = static_cast<uint8_t>(state.stage);
    snapshot.fullness = state.fullness;
    snapshot.happiness = state.happiness;
    snapshot.energy = state.energy;
    snapshot.health = state.health;
    snapshot.coins = state.coins;
    snapshot.level = state.level;
    snapshot.age_ticks = state.age_ticks;
    snapshot.stage_entered_tick = pet.stage_entered_tick();
    snapshot.last_open_day = pet.last_open_day();
    snapshot.low_stats_ticks = pet.low_stats_ticks();
    snapshot.crc32 = checksum(snapshot);
    err = nvs_set_blob(handle_, kKeyV3, &snapshot, sizeof(snapshot));
    if (err != ESP_OK) return err;
    err = nvs_commit(handle_);
    if (err == ESP_OK) pet.clear_dirty();
    return err;
}
esp_err_t PetSave::clear() noexcept
{
    esp_err_t err = init();
    if (err != ESP_OK) return err;
    for (const char *key : {kKeyV1, kKeyV2, kKeyV3}) {
        err = nvs_erase_key(handle_, key);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    }
    return nvs_commit(handle_);
}
} // namespace pet
