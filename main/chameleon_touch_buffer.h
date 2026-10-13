#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ch_touch {

enum class Kind : uint8_t { Down, Move, Up, Cancel };

struct Sample {
    int16_t x = 0;
    int16_t y = 0;
    uint32_t timestamp = 0;
    Kind kind = Kind::Up;
};

// The producer captures hardware independently of rendering. The consumer
// keeps every Down/Up edge in order, even when an entire tap arrives during a
// display transfer. The owner serializes calls across cores.
class Buffer {
public:
    static constexpr std::size_t capacity = 64;

    bool record(int16_t x, int16_t y, bool pressed, uint32_t timestamp)
    {
        const bool was_pressed = pressed_;
        pressed_ = pressed;
        if (pressed) {
            x_ = x;
            y_ = y;
        }

        // Overflow/error cancellation discards the rest of that physical
        // gesture. A fresh Down is permitted only after a valid release.
        if (discard_gesture_) {
            if (!pressed) discard_gesture_ = false;
            return false;
        }

        if (pressed) {
            return push({x_, y_, timestamp, was_pressed ? Kind::Move : Kind::Down});
        }
        if (was_pressed) return push({x_, y_, timestamp, Kind::Up});
        return false;
    }

    bool cancel(uint32_t timestamp)
    {
        head_ = 0;
        size_ = 1;
        samples_[0] = {x_, y_, timestamp, Kind::Cancel};
        discard_gesture_ = pressed_;
        return true;
    }

    bool pop(Sample &sample)
    {
        if (!size_) return false;
        sample = samples_[head_];
        head_ = (head_ + 1) % capacity;
        --size_;
        return true;
    }

    bool pending() const { return size_ != 0; }
    std::size_t size() const { return size_; }
    bool active() const { return pressed_; }

private:
    bool push(const Sample &sample)
    {
        if (sample.kind == Kind::Move && size_) {
            Sample &tail = samples_[(head_ + size_ - 1) % capacity];
            if (tail.kind == Kind::Move) {
                // Do not replace Down: the initial hit coordinate must not
                // drift to wherever the finger moved during the blocked draw.
                tail = sample;
                return true;
            }
        }
        if (size_ == capacity) {
            // Dropping an arbitrary edge can turn a drag into a click or leave
            // LVGL pressed. Cancel the whole stream instead of inventing one.
            return cancel(sample.timestamp);
        }
        samples_[(head_ + size_) % capacity] = sample;
        ++size_;
        return true;
    }

    std::array<Sample, capacity> samples_{};
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    int16_t x_ = 0;
    int16_t y_ = 0;
    bool pressed_ = false;
    bool discard_gesture_ = false;
};

} // namespace ch_touch
