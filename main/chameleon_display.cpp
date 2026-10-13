#include "chameleon_display.h"
#include "chameleon_adapter.h"
#include "chameleon_power.h"
#include "chameleon_ui.h"
#include "chameleon_touch.h"
#include <atomic>

namespace {
lv_display_t *display = nullptr;
std::atomic<bool> key_pending{false};
}

void chameleon_display_bind(lv_display_t *value)
{
    display = value;
}

void chameleon_display_notify_input(void)
{
    key_pending.store(true);
    chameleon_lv_adapter_notify();
}

void chameleon_display_process_pending_input(void)
{
    if (key_pending.exchange(false)) chameleon_ui_process_key_events();
}

void chameleon_display_set_screen_off(bool off)
{
    chameleon_power_set_screen_off(off);
    chameleon_touch_set_screen_off(off);
    chameleon_lv_adapter_set_max_wait(off ? 1000 : 15);
    if (!display) return;
    auto *refresh = lv_display_get_refr_timer(display);
    auto *animation = lv_anim_get_timer();
    if (off) {
        // Invalidation/scroll creation can resume these; the standby model
        // tick reapplies the gate. TE/DMA and PARTIAL registration stay intact.
        if (refresh) lv_timer_pause(refresh);
        if (animation) lv_timer_pause(animation);
    } else {
        if (animation && lv_anim_count_running()) lv_timer_resume(animation);
        lv_obj_invalidate(lv_screen_active());
        if (refresh) { lv_timer_resume(refresh); lv_timer_ready(refresh); }
    }
}
