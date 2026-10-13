#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// Shared by the board audio task and host checks; independent of pet save ABI.
namespace ch_audio {
constexpr int kDefaultEffects = 45;
constexpr int kDefaultAlarm = 85;
constexpr int kRate = 22050;
constexpr int kChunk = 441;
inline int volume(int value) { return std::clamp(value, 0, 100); }
inline int16_t sample(int index, int count, float frequency, bool alarm) {
    if(index < 0 || index >= count) return 0;
    const float envelope = std::min(1.0f, std::min(index / 180.0f, (count - 1 - index) / 300.0f));
    // Alarm has its own stronger signal, below half full scale. Ordinary taps
    // keep their shipped waveform and are not made louder by the alarm level.
    const float amplitude = alarm ? 11000.0f : 2800.0f;
    return static_cast<int16_t>(std::sin(6.28318530718f * frequency * index / kRate) * amplitude * envelope);
}
} // namespace ch_audio
