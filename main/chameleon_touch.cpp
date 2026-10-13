#include "chameleon_touch_driver.h"
#include "chameleon_touch_buffer.h"

#include "esp_lv_adapter.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include <atomic>

namespace {
constexpr const char *TAG = "ch_touch";
constexpr uint32_t SAMPLE_PERIOD_MS = 5;
constexpr uint32_t STALLED_GESTURE_MS = 1000;

struct Runtime {
    portMUX_TYPE buffer_lock = portMUX_INITIALIZER_UNLOCKED;
    portMUX_TYPE task_lock = portMUX_INITIALIZER_UNLOCKED;
    ch_touch::Buffer buffer;
    bool discard_until_release = false;
    esp_lcd_touch_handle_t touch = nullptr;
    lv_indev_t *input = nullptr;
    TaskHandle_t task = nullptr;
    std::atomic<bool> initialized{false};
    std::atomic<bool> ready{false};
    std::atomic<bool> screen_off{false};
    std::atomic<uint32_t> screen_generation{0};
    std::atomic<uint32_t> consumed_timestamp{0};
    ch_touch::Sample consumed;
};

Runtime runtime;

uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

TickType_t wait_ticks(uint32_t milliseconds)
{
    const TickType_t ticks = pdMS_TO_TICKS(milliseconds);
    return ticks ? ticks : 1;
}

void publish_sampler(TaskHandle_t task)
{
    portENTER_CRITICAL(&runtime.task_lock);
    runtime.task = task;
    portEXIT_CRITICAL(&runtime.task_lock);
}

void notify_sampler()
{
    // Keep the handle protected through the notification: failed startup may
    // revoke it before deleting the task, including on the other CPU core.
    portENTER_CRITICAL(&runtime.task_lock);
    if (runtime.task) xTaskNotifyGive(runtime.task);
    portEXIT_CRITICAL(&runtime.task_lock);
}

void notify_consumer()
{
    // This publishes only the adapter's semaphore and dedicated worker
    // notification. It never processes an input event or touches LVGL state.
    esp_lv_adapter_touch_notify_interrupt(runtime.input);
}

bool current_report(uint32_t generation)
{
    return !runtime.screen_off.load(std::memory_order_acquire)
        && runtime.screen_generation.load(std::memory_order_acquire) == generation;
}

bool record_report(int16_t x, int16_t y, bool pressed, uint32_t timestamp, uint32_t generation)
{
    portENTER_CRITICAL(&runtime.buffer_lock);
    const bool current = current_report(generation);
    bool changed = false;
    if (current) {
        if (runtime.discard_until_release) {
            if (!pressed) {
                runtime.discard_until_release = false;
                // The dark transition cleared the FIFO and consumed state.
                // Wake the reader once so LVGL observes that release before
                // the next tap, even when the finger lifted while dark.
                changed = true;
            }
        } else changed = runtime.buffer.record(x, y, pressed, timestamp);
    }
    portEXIT_CRITICAL(&runtime.buffer_lock);
    if (changed) notify_consumer();
    return current;
}

bool cancel_report(uint32_t timestamp, uint32_t generation)
{
    portENTER_CRITICAL(&runtime.buffer_lock);
    const bool current = current_report(generation);
    if (current) runtime.buffer.cancel(timestamp);
    portEXIT_CRITICAL(&runtime.buffer_lock);
    if (current) notify_consumer();
    return current;
}

void IRAM_ATTR touch_interrupt(esp_lcd_touch_handle_t)
{
    if (runtime.screen_off.load(std::memory_order_acquire)) return;
    BaseType_t woken = pdFALSE;
    // Xtensa does not promise a lock-free C++ atomic<TaskHandle_t>. Use the
    // FreeRTOS ISR critical section and finish notifying before releasing the
    // handle lock so cleanup cannot delete a task already read by this ISR.
    portENTER_CRITICAL_ISR(&runtime.task_lock);
    if (runtime.task) vTaskNotifyGiveFromISR(runtime.task, &woken);
    portEXIT_CRITICAL_ISR(&runtime.task_lock);
    if (woken == pdTRUE) portYIELD_FROM_ISR();
}

void sampler(void *)
{
    // IRQ registration and startup must finish before any controller access.
    do {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    } while (!runtime.ready.load(std::memory_order_acquire));

    bool initial_read = true;
    bool active = false;
    bool gesture_cancelled = false;
    uint32_t last_valid_at = now_ms();
    uint32_t active_generation = runtime.screen_generation.load(std::memory_order_acquire);
    int16_t last_x = 0;
    int16_t last_y = 0;

    for (;;) {
        const bool dark = runtime.screen_off.load(std::memory_order_acquire);
        // A dark display accepts physical keys only. Touch IRQ is disabled by
        // the UI transition; a queued IRQ cannot turn into a dark-screen read.
        const TickType_t wait = dark ? portMAX_DELAY
            : initial_read ? 0 : wait_ticks(SAMPLE_PERIOD_MS);
        ulTaskNotifyTake(pdTRUE, wait);
        if (runtime.screen_off.load(std::memory_order_acquire)) {
            active = false;
            gesture_cancelled = false;
            initial_read = true;
            continue;
        }
        initial_read = false;

        // The sampler is the sole reader of the driver and its cached points.
        // Never consume cached points after a failed or invalid hardware read.
        // The generation and dark flag form one snapshot under the queue lock.
        // A read spanning an off/on cycle must not release the wake gesture gate.
        portENTER_CRITICAL(&runtime.buffer_lock);
        const uint32_t generation = runtime.screen_generation.load(std::memory_order_acquire);
        const bool readable = current_report(generation);
        portEXIT_CRITICAL(&runtime.buffer_lock);
        if (!readable) {
            active = false;
            gesture_cancelled = false;
            initial_read = true;
            continue;
        }
        if (generation != active_generation) {
            active = false;
            gesture_cancelled = false;
            last_valid_at = now_ms();
            active_generation = generation;
        }
        esp_err_t error = esp_lcd_touch_read_data(runtime.touch);
        esp_lcd_touch_point_data_t point{};
        uint8_t count = 0;
        if (error == ESP_OK) {
            error = esp_lcd_touch_get_data(runtime.touch, &point, &count, 1);
        }
        const uint32_t sampled_at = now_ms();
        if (!current_report(generation)) {
            active = false;
            gesture_cancelled = false;
            initial_read = true;
            continue;
        }
        if (error != ESP_OK) {
            if (active && !gesture_cancelled &&
                    sampled_at - last_valid_at >= STALLED_GESTURE_MS) {
                if (cancel_report(sampled_at, generation)) {
                    gesture_cancelled = true;
                    ESP_LOGW(TAG, "Cancelled touch after missing valid reports");
                } else {
                    active = false;
                    initial_read = true;
                }
            }
            continue;
        }

        const bool pressed = count != 0;
        if (pressed) {
            // esp_lcd_touch_get_data already applies the BSP mirror mapping.
            // Its inclusive x_max/y_max can produce 466 at the panel edge;
            // keep such reports on the last addressable pixel, not outside it.
            const uint16_t x_max = runtime.touch->config.x_max;
            const uint16_t y_max = runtime.touch->config.y_max;
            last_x = static_cast<int16_t>(x_max && point.x >= x_max ? x_max - 1 : point.x);
            last_y = static_cast<int16_t>(y_max && point.y >= y_max ? y_max - 1 : point.y);
        }
        // Recheck while publishing: the screen may change after the I2C guard.
        if (!record_report(last_x, last_y, pressed, sampled_at, generation)) {
            active = false;
            gesture_cancelled = false;
            initial_read = true;
            continue;
        }
        active = pressed;
        last_valid_at = sampled_at;
        // Cancellation discards the entire physical gesture. Neither the
        // producer nor the consumer may revive it before a valid release.
        if (!pressed) gesture_cancelled = false;
    }
}
} // namespace

extern "C" esp_err_t chameleon_touch_start(esp_lcd_touch_handle_t touch, lv_indev_t *input)
{
    if (!touch || !input || touch->config.int_gpio_num == GPIO_NUM_NC) return ESP_ERR_INVALID_ARG;
    bool expected = false;
    if (!runtime.initialized.compare_exchange_strong(expected, true)) return ESP_ERR_INVALID_STATE;

    runtime.touch = touch;
    runtime.input = input;
    runtime.ready.store(false, std::memory_order_release);
    runtime.consumed_timestamp.store(0, std::memory_order_relaxed);
    runtime.consumed = {};
    portENTER_CRITICAL(&runtime.buffer_lock);
    runtime.buffer = {};
    portEXIT_CRITICAL(&runtime.buffer_lock);

    TaskHandle_t task = nullptr;
    if (xTaskCreatePinnedToCore(sampler, "pet_touch", 4096, nullptr, 7, &task, 1) != pdPASS) {
        runtime.touch = nullptr;
        runtime.input = nullptr;
        runtime.initialized.store(false, std::memory_order_release);
        return ESP_ERR_NO_MEM;
    }
    publish_sampler(task);
    void *const original_user_data = touch->config.user_data;
    const esp_err_t error = esp_lcd_touch_register_interrupt_callback_with_data(
        touch, touch_interrupt, &runtime);
    if (error != ESP_OK) {
        // Registration can enable the GPIO before failing to install its
        // handler. Quiesce it before deleting a task referenced by the ISR.
        gpio_intr_disable(touch->config.int_gpio_num);
        esp_lcd_touch_register_interrupt_callback(touch, nullptr);
        publish_sampler(nullptr);
        vTaskDelete(task);
        touch->config.user_data = original_user_data;
        runtime.touch = nullptr;
        runtime.input = nullptr;
        runtime.initialized.store(false, std::memory_order_release);
        return error;
    }

    runtime.ready.store(true, std::memory_order_release);
    notify_sampler();
    ESP_LOGI(TAG, "Independent touch sampler: lit 5 ms, dark touch disabled");
    return ESP_OK;
}

extern "C" esp_err_t chameleon_touch_read(esp_lcd_touch_handle_t touch,
                                          esp_lcd_touch_point_data_t *points,
                                          uint8_t *count, uint8_t max_count,
                                          void *)
{
    if (!touch || touch != runtime.touch || !points || !count || !max_count) return ESP_ERR_INVALID_ARG;

    ch_touch::Sample sample;
    portENTER_CRITICAL(&runtime.buffer_lock);
    const bool popped = runtime.buffer.pop(sample);
    const bool more = runtime.buffer.pending();
    portEXIT_CRITICAL(&runtime.buffer_lock);
    if (popped) runtime.consumed = sample;
    else sample = runtime.consumed;
    runtime.consumed_timestamp.store(popped ? sample.timestamp : 0, std::memory_order_relaxed);

    const bool pressed = sample.kind == ch_touch::Kind::Down || sample.kind == ch_touch::Kind::Move;
    *count = pressed ? 1 : 0;
    if (pressed) {
        points[0] = {};
        points[0].x = static_cast<uint16_t>(sample.x);
        points[0].y = static_cast<uint16_t>(sample.y);
        points[0].strength = 1;
    }
    if (popped && sample.kind == ch_touch::Kind::Cancel) {
        // This callback runs in LVGL's worker with its lock held. Missing
        // reports and queue overflow are a cancellation, never a user click.
        lv_indev_wait_release(runtime.input);
    }
    if (more) notify_consumer();
    return ESP_OK;
}

extern "C" void chameleon_touch_set_screen_off(bool screen_off)
{
    portENTER_CRITICAL(&runtime.buffer_lock);
    if (runtime.screen_off.load(std::memory_order_acquire) == screen_off) {
        portEXIT_CRITICAL(&runtime.buffer_lock);
        return;
    }
    runtime.screen_generation.fetch_add(1, std::memory_order_acq_rel);
    runtime.screen_off.store(screen_off, std::memory_order_release);
    if (screen_off) {
        runtime.buffer = {};
        runtime.discard_until_release = true;
    }
    portEXIT_CRITICAL(&runtime.buffer_lock);
    if (screen_off) {
        if (runtime.touch) gpio_intr_disable(runtime.touch->config.int_gpio_num);
        runtime.consumed = {};
        runtime.consumed_timestamp.store(0, std::memory_order_relaxed);
        // Called with the LVGL lock held. Cancel a pending tap/drag rather
        // than allowing its release to activate a covered control on wake.
        if (runtime.input && lv_indev_get_state(runtime.input) == LV_INDEV_STATE_PRESSED)
            lv_indev_wait_release(runtime.input);
    } else if (runtime.touch) {
        gpio_intr_enable(runtime.touch->config.int_gpio_num);
    }
    notify_sampler();
}

extern "C" uint32_t chameleon_touch_sample_timestamp(void)
{
    return runtime.consumed_timestamp.load(std::memory_order_relaxed);
}
