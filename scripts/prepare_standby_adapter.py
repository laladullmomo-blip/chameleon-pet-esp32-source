#!/usr/bin/env python3
"""Generate the pinned adapter integration without modifying managed files.

Original generated C source remains Apache-2.0, as labelled by Espressif.
Reject changed dependencies rather than silently applying a partial patch.
"""
import argparse
import hashlib
from pathlib import Path

ADAPTER = "src/adapter/esp_lv_adapter.c"
TOUCH = "src/input/esp_lv_adapter_input_touch.c"
DISPLAY = "src/display/display_manager.c"
HASHES = {
    ADAPTER: "3aaf0e1b79c8f89afb1224f5f08957aee3e76e1ae2d9d3ae626d717a6b42b2f5",
    TOUCH: "213e44b7ac3190a1c0c493d24934aa7ea9d38b9d0a1280a58e6dd85219b3cd7c",
    DISPLAY: "f058caf78a25003c60f7a86751cb25ae94606563c424cbe841fd024163a5ece7",
}


def replace_once(source, before, after):
    if source.count(before) != 1:
        raise ValueError(f"standby integration anchor is not unique: {before[:80]!r}")
    return source.replace(before, after, 1)


def prepare(component, output):
    sources = {}
    for name, expected in HASHES.items():
        data = (component / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError(f"unsupported esp_lvgl_adapter source: {name}; review dependency change")
        sources[name] = data.decode("utf-8")

    source = replace_once(sources[ADAPTER], '#include "adapter_internal.h"',
                          '#include "adapter_internal.h"\n#include "chameleon_adapter.h"\n#include "chameleon_display.h"\n'
                          '/* Notification 0 belongs to display DMA completion. */\n'
                          '#define CHAMELEON_LV_WORKER_NOTIFICATION_INDEX 1U\n'
                          '#if configTASK_NOTIFICATION_ARRAY_ENTRIES < 2\n'
                          '#error "Chameleon LVGL worker requires a separate notification slot"\n'
                          '#endif')
    source = replace_once(source, "            next_delay_ms_raw = lv_timer_handler();", """            chameleon_display_process_pending_input();
            chameleon_lv_adapter_process_pending_touch();
            next_delay_ms_raw = lv_timer_handler();""")
    start = source.index("/**\n * @brief LVGL tick increment callback")
    end = source.index("#if CONFIG_ESP_LVGL_ADAPTER_ENABLE_FS", start)
    source = source[:start] + """/* Real monotonic time: no 5 ms periodic timer and no lost/coarse LVGL ticks.
 * This product does not use adapter pause/sleep or reset its monotonic clock. */
static uint32_t chameleon_lvgl_tick(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static esp_err_t tick_init(void)
{
    lv_tick_set_cb(chameleon_lvgl_tick);
    return ESP_OK;
}

void chameleon_lv_adapter_notify(void)
{
    if (s_ctx.task) xTaskNotifyGive(s_ctx.task);
}

bool IRAM_ATTR chameleon_lv_adapter_notify_from_isr(void)
{
    BaseType_t woken = pdFALSE;
    if (s_ctx.task) vTaskNotifyGiveFromISR(s_ctx.task, &woken);
    return woken == pdTRUE;
}

void chameleon_lv_adapter_set_max_wait(uint32_t milliseconds)
{
    /* Caller owns the LVGL lock. Worker is the only runtime writer. */
    const uint32_t wait = milliseconds ? milliseconds : 1;
    if (s_ctx.config.task_max_delay_ms == wait) return;
    s_ctx.config.task_max_delay_ms = wait;
    chameleon_lv_adapter_notify();
}

""" + source[end:]
    # TE_SYNC blocks this same task on notification 0 until the panel DMA is
    # finished. Input/resume/exit notifications must never satisfy that wait,
    # or be cleared by it. Keep every adapter-worker wake on slot 1, including
    # the paused branch so resume/deinit retain their upstream semantics.
    source = source.replace("xTaskNotifyGive(s_ctx.task);",
                            "xTaskNotifyGiveIndexed(s_ctx.task, CHAMELEON_LV_WORKER_NOTIFICATION_INDEX);")
    source = source.replace("xTaskNotifyGive(task);",
                            "xTaskNotifyGiveIndexed(task, CHAMELEON_LV_WORKER_NOTIFICATION_INDEX);")
    source = source.replace("vTaskNotifyGiveFromISR(s_ctx.task, &high_prio_woken);",
                            "vTaskNotifyGiveIndexedFromISR(s_ctx.task, CHAMELEON_LV_WORKER_NOTIFICATION_INDEX, &high_prio_woken);")
    source = source.replace("vTaskNotifyGiveFromISR(s_ctx.task, &woken);",
                            "vTaskNotifyGiveIndexedFromISR(s_ctx.task, CHAMELEON_LV_WORKER_NOTIFICATION_INDEX, &woken);")
    source = replace_once(source, "ulTaskNotifyTake(pdTRUE, portMAX_DELAY);",
                          "ulTaskNotifyTakeIndexed(CHAMELEON_LV_WORKER_NOTIFICATION_INDEX, pdTRUE, portMAX_DELAY);")
    source = replace_once(source, "ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(task_delay_ms));",
                          "ulTaskNotifyTakeIndexed(CHAMELEON_LV_WORKER_NOTIFICATION_INDEX, pdTRUE, pdMS_TO_TICKS(task_delay_ms));")
    sources[ADAPTER] = source

    source = replace_once(sources[DISPLAY], '#include "adapter_internal.h"',
                          '#include "adapter_internal.h"\n#include "chameleon_adapter.h"')
    # External LVGL invalidation also wakes the worker, never the display's
    # DMA completion wait. The helper owns the dedicated notification slot.
    source = replace_once(source, "        xTaskNotifyGive(ctx->task);",
                          "        chameleon_lv_adapter_notify();")
    sources[DISPLAY] = source

    source = replace_once(sources[TOUCH], '#include "adapter_internal.h"',
                          '#include "adapter_internal.h"\n#include "chameleon_adapter.h"\n#include "chameleon_touch.h"')
    source = replace_once(source, "    touch_ctx->indev = indev;\n\n    esp_lv_adapter_unlock();", """    touch_ctx->indev = indev;
    /* Idle IRQ inputs have no read timer. LVGL resumes the original timer
     * while pressed, preserving long presses, dragging and release handling. */
    if (with_irq) lv_indev_set_mode(indev, LV_INDEV_MODE_EVENT);

    esp_lv_adapter_unlock();""")
    # This product pins LVGL 9.5. Leave the upstream v8 fallback untouched.
    boundary = source.index(" *                         LVGL v8 Implementation")
    legacy = source[boundary:]
    source = source[:boundary]
    source = replace_once(source, """            ret = touch_ctx->cbs.custom_touch_read(touch_ctx->handle, touch_data, &count,
                                                   MAX_TOUCH_POINTS_V9, touch_ctx->cbs.user_ctx);""", """            ret = touch_ctx->cbs.custom_touch_read(touch_ctx->handle, touch_data, &count,
                                                   MAX_TOUCH_POINTS_V9, touch_ctx->cbs.user_ctx);
            if (ret == ESP_OK) data->timestamp = chameleon_touch_sample_timestamp();""")
    source = replace_once(source, "    esp_lv_adapter_request_wake_from_isr();\n\n    if (!touch_ctx->touch_sem)",
                          "    if (!touch_ctx->touch_sem)")
    source = replace_once(source, """    xSemaphoreGiveFromISR(touch_ctx->touch_sem, &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken == pdTRUE) {""", """    xSemaphoreGiveFromISR(touch_ctx->touch_sem, &xHigherPriorityTaskWoken);
    /* Publish data before notifying the other core; never call LVGL in ISR. */
    esp_lv_adapter_request_wake_from_isr();
    const bool worker_woken = chameleon_lv_adapter_notify_from_isr();

    if (xHigherPriorityTaskWoken == pdTRUE || worker_woken) {""")
    source += legacy
    anchor = "static esp_err_t register_idf_interrupt_callback(esp_lv_adapter_touch_ctx_t *touch_ctx)\n{"
    source = replace_once(source, anchor, """void chameleon_lv_adapter_process_pending_touch(void)
{
    /* Called by the display worker while owning the LVGL lock. */
    esp_lv_adapter_context_t *ctx = esp_lv_adapter_get_context();
    if (!ctx) return;
    for (esp_lv_adapter_input_node_t *node = ctx->input_list; node; node = node->next) {
        if (node->type != ESP_LV_ADAPTER_INPUT_TYPE_TOUCH) continue;
        esp_lv_adapter_touch_ctx_t *tp = get_touch_ctx(node->indev);
        if (tp && tp->with_irq && !tp->multi.enabled && tp->touch_sem &&
                lv_indev_get_mode(node->indev) == LV_INDEV_MODE_EVENT &&
                uxSemaphoreGetCount(tp->touch_sem) > 0) {
            /* EVENT mode ignores continue_reading. A custom FIFO callback
             * republishes the semaphore while more captured reports remain.
             * Drain both edges of a tap before drawing, with bounded work. */
            for (unsigned reads = 0; reads < 64 && uxSemaphoreGetCount(tp->touch_sem) > 0; ++reads)
                lv_indev_read(node->indev);
        }
    }
}

""" + anchor)
    source = replace_once(source, "    return xSemaphoreGive(touch_ctx->touch_sem) == pdTRUE;", """    const bool signaled = xSemaphoreGive(touch_ctx->touch_sem) == pdTRUE;
    chameleon_lv_adapter_notify();
    return signaled;""")
    source = replace_once(source, """    xSemaphoreGiveFromISR(touch_ctx->touch_sem, &xHigherPriorityTaskWoken);
    return xHigherPriorityTaskWoken == pdTRUE;""", """    xSemaphoreGiveFromISR(touch_ctx->touch_sem, &xHigherPriorityTaskWoken);
    const bool worker_woken = chameleon_lv_adapter_notify_from_isr();
    return xHigherPriorityTaskWoken == pdTRUE || worker_woken;""")
    sources[TOUCH] = source

    for name, source in sources.items():
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        if not path.exists() or path.read_text() != source:
            path.write_text(source)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("component", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    prepare(args.component, args.output)
