// Derived from Ellenp2p/esp32-pet-tamagotchi.
// Copyright (c) 2026 ellenp2p. MIT License; see THIRD_PARTY_LICENSES.
#pragma once

#include "esp_err.h"
#include "nvs.h"

namespace pet {

class Pet;

class PetSave {
public:
    static PetSave &instance() noexcept;

    esp_err_t init() noexcept;
    esp_err_t load(Pet &pet) noexcept;
    esp_err_t save_if_dirty(Pet &pet, bool force = false) noexcept;
    esp_err_t clear() noexcept;

private:
    PetSave() = default;

    nvs_handle_t handle_ = 0;
    bool         open_   = false;
};

} // namespace pet
