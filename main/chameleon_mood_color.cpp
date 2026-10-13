// SPDX-License-Identifier: MIT
#include "chameleon_mood_color.h"
#include "chameleon_assets.h"
#include "esp_heap_caps.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace ch_mood {
namespace {
constexpr size_t kFrameBytes = 128u * 128u * 3u;
constexpr unsigned kPaletteSize = 1024;
struct Slot {
    lv_image_dsc_t image{};
    uint8_t *pixels = nullptr;
    const lv_image_dsc_t *original = nullptr;
    Tone tone = Tone::Original;
    int stage = -1;
};
struct Memo { uint16_t source = 0, target = 0; bool valid = false; };
Slot slots[2];
Memo palette[kPaletteSize];
int last_slot = -1, palette_stage = -2;
Tone palette_tone = Tone::Original;
bool palette_adult_sick = false;

struct RGB { int r, g, b; };
struct HSV { int h, s, v; };
RGB unpack(uint16_t color) {
    const int r = (color >> 11) & 31, g = (color >> 5) & 63, b = color & 31;
    return {(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)};
}
uint16_t pack(RGB color) {
    return static_cast<uint16_t>((color.r >> 3) << 11 | (color.g >> 2) << 5 | (color.b >> 3));
}
HSV hsv(RGB color) {
    const int v = std::max({color.r, color.g, color.b});
    const int delta = v - std::min({color.r, color.g, color.b});
    if (!delta || !v) return {0, 0, v};
    int h = v == color.r ? (color.g - color.b) * 60 / delta :
            v == color.g ? 120 + (color.b - color.r) * 60 / delta :
                          240 + (color.r - color.g) * 60 / delta;
    if (h < 0) h += 360;
    return {h, (delta * 255 + v / 2) / v, v};
}
RGB rgb(HSV color) {
    const int f = color.h % 60, v = color.v, s = color.s;
    const int p = (v * (255 - s) + 127) / 255;
    const int q = (v * (255 * 60 - s * f) + 7650) / (255 * 60);
    const int t = (v * (255 * 60 - s * (60 - f)) + 7650) / (255 * 60);
    switch (color.h / 60) {
        case 0: return {v, t, p}; case 1: return {q, v, p};
        case 2: return {p, v, t}; case 3: return {p, q, v};
        case 4: return {t, p, v}; default: return {v, p, q};
    }
}
bool body_green(RGB color, HSV value, int stage, bool adult_sick) {
    if (value.h < 70 || value.h > 155 || value.s < 64 || value.v <= 64 ||
        color.g < 90 || color.g - color.r < 20 || color.g - color.b < 20) return false;
    // Adult cyan stripes include hues 150..159, below a simple cyan cutoff.
    // Their blue exceeds red; baby/teen green shadows must still be recolored.
    // New sick drawings use green crest/body shades that the older cyan rule
    // also classified as stripes. Only these four sources use the HSV gate
    // alone; their actual turquoise stripes remain above hue 155.
    return stage != CH_STAGE_ADULT || adult_sick || color.r >= color.b + 8;
}
bool adult_sick_source(const lv_image_dsc_t *source, int stage) {
    if (stage != CH_STAGE_ADULT) return false;
    const auto &animation = ch_stages[CH_STAGE_ADULT].animations[CH_SICK];
    for (int i = 0; i < animation.count; ++i)
        if (animation.frames[i] == source) return true;
    return false;
}
int identify(const lv_image_dsc_t *source) {
    for (int stage = 0; stage < CH_STAGE_COUNT; ++stage)
        for (int pose = 0; pose < CH_ANIM_COUNT; ++pose) {
            const auto &animation = ch_stages[stage].animations[pose];
            for (int i = 0; i < animation.count; ++i)
                if (animation.frames[i] == source) return stage;
        }
    return -1;
}
uint16_t recolor(uint16_t source, Tone tone, int stage, bool adult_sick) {
    auto &memo = palette[(static_cast<unsigned>(source) * 2654435761u) >> 22];
    if (memo.valid && memo.source == source) return memo.target;
    const RGB color = unpack(source); const HSV original = hsv(color);
    uint16_t result = source;
    if (body_green(color, original, stage, adult_sick)) {
        // Targets: #B2D94C, #C88845, #B6B978, #8A9E93. Integer HSV preserves
        // source shading while the dominant body color maps to the target.
        constexpr HSV targets[] = {{0,0,0}, {77,166,217}, {31,167,200},
                                   {63,90,185}, {147,32,158}};
        // RGB565-expanded approved dominant greens: AEC8, A6AA, 96C7.
        constexpr int base_v[] = {219,215,219}, base_s[] = {178,158,189};
        const int reference = stage >= 0 && stage < CH_STAGE_COUNT ? stage : CH_STAGE_BABY;
        const HSV target = targets[static_cast<unsigned>(tone)];
        const int v = std::min(255, (original.v * target.v + base_v[reference] / 2) / base_v[reference]);
        const int s = std::min(255, (original.s * target.s + base_s[reference] / 2) / base_s[reference]);
        result = pack(rgb({target.h, s, v}));
    }
    memo = {source, result, true};
    return result;
}
} // namespace

Tone choose(const ch::State &state) {
    if (state.sleeping) return Tone::Original;
    if (state.sick) return Tone::Sick;
    if (state.fullness <= 25) return Tone::Hungry;
    if (state.mood < 40) return Tone::Unhappy;
    return state.mood >= 85 ? Tone::Happy : Tone::Original;
}

const void *source_for(const void *source) {
    for (const auto &slot : slots) if (source == &slot.image && slot.original) return slot.original;
    return source;
}

const lv_image_dsc_t *frame(const lv_image_dsc_t *input, Tone tone, int stage) {
    const auto *source = static_cast<const lv_image_dsc_t *>(source_for(input));
    if (!source || tone == Tone::Original || static_cast<unsigned>(tone) > static_cast<unsigned>(Tone::Sick)) return source;
    const auto &header = source->header;
    const size_t rgb_bytes = static_cast<size_t>(header.stride) * header.h;
    // Bound the RGB plane before adding alpha. The header's 16-bit fields
    // multiply safely on ESP32, but RGB plus half that size can wrap size_t.
    if (rgb_bytes > kFrameBytes) return source;
    const size_t required = rgb_bytes + rgb_bytes / 2;
    if (!source->data || header.cf != LV_COLOR_FORMAT_RGB565A8 || !header.w || !header.h ||
        header.stride % 2 || header.stride < header.w * 2 || required > source->data_size ||
        source->data_size > kFrameBytes ||
        (header.flags & (LV_IMAGE_FLAGS_COMPRESSED | LV_IMAGE_FLAGS_ALLOCATED | LV_IMAGE_FLAGS_PREMULTIPLIED | LV_IMAGE_FLAGS_CUSTOM_DRAW))) return source;
    if (stage < 0 || stage >= CH_STAGE_COUNT) stage = identify(source);
    const bool adult_sick = adult_sick_source(source, stage);
    for (int i = 0; i < 2; ++i) if (slots[i].original == source && slots[i].tone == tone && slots[i].stage == stage) {
        last_slot = i; return &slots[i].image;
    }
    const int next = last_slot < 0 ? 0 : 1 - last_slot;
    auto &slot = slots[next];
    if (!slot.pixels) slot.pixels = static_cast<uint8_t *>(heap_caps_malloc(kFrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!slot.pixels) return source;
    // Drop decoded/transformed cache entries before changing a reused source.
    if (slot.original) lv_image_cache_drop(&slot.image);
    slot.image = *source; slot.image.data = slot.pixels;
    std::memcpy(slot.pixels, source->data, source->data_size);
    if (palette_tone != tone || palette_stage != stage || palette_adult_sick != adult_sick) {
        for (auto &memo : palette) memo.valid = false;
        palette_tone = tone; palette_stage = stage;
        palette_adult_sick = adult_sick;
    }
    const auto *alpha = source->data + rgb_bytes;
    for (unsigned y = 0; y < header.h; ++y) for (unsigned x = 0; x < header.w; ++x) {
        if (!alpha[y * (header.stride / 2) + x]) continue;
        const size_t offset = y * header.stride + x * 2;
        uint16_t color; std::memcpy(&color, source->data + offset, sizeof(color));
        color = recolor(color, tone, stage, adult_sick);
        std::memcpy(slot.pixels + offset, &color, sizeof(color));
    }
    slot.original = source; slot.tone = tone; slot.stage = stage; last_slot = next;
    return &slot.image;
}

void clear() {
    for (auto &slot : slots) {
        if (slot.pixels) { lv_image_cache_drop(&slot.image); heap_caps_free(slot.pixels); }
        slot = Slot{};
    }
    for (auto &memo : palette) memo.valid = false;
    last_slot = -1; palette_stage = -2; palette_tone = Tone::Original;
    palette_adult_sick = false;
}
} // namespace ch_mood
