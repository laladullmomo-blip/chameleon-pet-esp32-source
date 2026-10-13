#include "chameleon_power.h"
#include "esp_pm.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
unsigned checks = 0;
void expect(bool condition, const char *what)
{
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL %s\n", what);
        std::exit(1);
    }
}

struct Lock { bool live = false; unsigned references = 0; } lock;
unsigned creates = 0, acquires = 0, releases = 0, deletes = 0, configurations = 0, warnings = 0;
bool fail_create = false, fail_acquire = false, fail_release = false, fail_configure = false;
std::string operations;
bool take_failure(bool &value)
{
    const bool result = value;
    value = false;
    return result;
}
void valid_lock(void *handle)
{
    expect(handle == &lock && lock.live, "driver gets a live owned lock");
}
} // namespace

void ch_power_test_warning(const char *tag, const char *format, ...)
{
    expect(std::strcmp(tag, "ch_power") == 0 && format, "PM failures remain diagnosable");
    ++warnings;
}

esp_err_t esp_pm_lock_create(esp_pm_lock_type_t type, int argument, const char *name, void **handle)
{
    expect(type == ESP_PM_CPU_FREQ_MAX && argument == 0, "visible display requests CPU maximum");
    expect(name && std::strcmp(name, "pet_display") == 0, "display lock is identifiable");
    expect(!lock.live, "failed initialization cannot leak a lock");
    ++creates;
    operations += 'C';
    if (take_failure(fail_create)) return ESP_FAIL;
    lock.live = true;
    *handle = &lock;
    return ESP_OK;
}

esp_err_t esp_pm_lock_acquire(void *handle)
{
    valid_lock(handle);
    ++acquires;
    operations += 'A';
    if (take_failure(fail_acquire)) return ESP_FAIL;
    ++lock.references;
    expect(lock.references == 1, "repeated UI wake cannot recursively hold the lock");
    return ESP_OK;
}

esp_err_t esp_pm_lock_release(void *handle)
{
    valid_lock(handle);
    expect(lock.references == 1, "screen-off only releases its held display lock");
    ++releases;
    operations += 'R';
    if (take_failure(fail_release)) return ESP_FAIL;
    --lock.references;
    return ESP_OK;
}

esp_err_t esp_pm_lock_delete(void *handle)
{
    valid_lock(handle);
    expect(lock.references == 0, "cleanup releases before deleting");
    ++deletes;
    operations += 'D';
    lock.live = false;
    return ESP_OK;
}

esp_err_t esp_pm_configure(const void *value)
{
    expect(lock.references == 1, "display is protected before DFS begins");
    const auto *configuration = static_cast<const esp_pm_config_t *>(value);
    expect(configuration->max_freq_mhz == 240 && configuration->min_freq_mhz == 80,
           "DFS preserves 240 MHz performance and conservative 80 MHz idle floor");
    expect(!configuration->light_sleep_enable, "automatic light sleep remains disabled");
    ++configurations;
    operations += 'F';
    return take_failure(fail_configure) ? ESP_FAIL : ESP_OK;
}

int main(int argc, char **argv)
{
    expect(argc == 2, "one scenario per process preserves initialization isolation");
    const std::string scenario = argv[1];
    expect(scenario == "normal" || scenario == "create-failure" || scenario == "acquire-failure" ||
           scenario == "configure-failure" || scenario == "transition-failure", "known PM scenario");

    chameleon_power_set_screen_off(true);
    chameleon_power_set_screen_off(false);
    expect(operations.empty(), "uninitialized fallback needs no lock operations");

    if (scenario == "create-failure") {
        fail_create = true;
        expect(chameleon_power_init() == ESP_FAIL, "allocation failure is reported");
        expect(!lock.live && deletes == 0 && configurations == 0, "allocation failure has no side effects");
    } else if (scenario == "acquire-failure") {
        fail_acquire = true;
        expect(chameleon_power_init() == ESP_FAIL, "initial lock failure is reported");
        expect(!lock.live && deletes == 1 && configurations == 0, "lock failure frees memory before any DFS");
    } else if (scenario == "configure-failure") {
        fail_configure = true;
        expect(chameleon_power_init() == ESP_FAIL, "configuration failure is reported");
        expect(!lock.live && deletes == 1 && releases == 1, "configuration failure releases and deletes");
    }

    operations.clear();
    expect(chameleon_power_init() == ESP_OK, "initialization succeeds after any transient failure");
    expect(operations == "CAF" && lock.references == 1, "startup acquires before configuring, once");
    const unsigned created_count = creates, acquired_count = acquires;
    expect(chameleon_power_init() == ESP_OK, "initialization is idempotent");
    expect(creates == created_count && acquires == acquired_count, "reinitialization does not leak or nest locks");

    if (scenario == "transition-failure") {
        fail_release = true;
        chameleon_power_set_screen_off(true);
        expect(lock.references == 1 && warnings == 1, "failed release retains known state and warns");
    }
    chameleon_power_set_screen_off(true);
    expect(lock.references == 0, "screen-off allows DFS idle periods");
    const unsigned released_count = releases;
    chameleon_power_set_screen_off(true);
    expect(releases == released_count, "repeated screen-off does not underflow");

    if (scenario == "transition-failure") {
        fail_acquire = true;
        chameleon_power_set_screen_off(false);
        expect(lock.references == 0 && warnings == 2, "failed wake is observable and retryable");
    }
    chameleon_power_set_screen_off(false);
    expect(lock.references == 1, "wake restores CPU maximum");
    const unsigned wake_count = acquires;
    chameleon_power_set_screen_off(false);
    expect(acquires == wake_count, "repeated wake does not nest locks");

    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        chameleon_power_set_screen_off(true);
        expect(lock.references == 0, "repeated sleep cycles leave no display reference");
        chameleon_power_set_screen_off(false);
        expect(lock.references == 1, "repeated wake cycles keep exactly one display reference");
    }
    std::printf("PASS PM %s: %u checks\n", scenario.c_str(), checks);
}
