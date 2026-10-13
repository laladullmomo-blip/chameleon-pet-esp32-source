// SPDX-License-Identifier: MIT
// Independent runtime/asset checks; LVGL cache and ESP allocation are host seams.
#include "chameleon_mood_color.h"
#include "chameleon_assets.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <vector>

static std::map<void *, size_t> allocations;
static size_t allocation_calls = 0, live_bytes = 0, peak_bytes = 0;
static bool fail_allocation = false;
static std::vector<const void *> cache_drops;
void *ch_mood_test_allocate(size_t size, int caps) {
    assert(caps == 3 && "tinted pixels must use SPIRAM plus byte access");
    if (fail_allocation) return nullptr;
    void *memory = std::malloc(size); assert(memory);
    allocations[memory] = size; ++allocation_calls; live_bytes += size;
    peak_bytes = std::max(peak_bytes, live_bytes); return memory;
}
void ch_mood_test_free(void *memory) {
    const auto found = allocations.find(memory); assert(found != allocations.end());
    live_bytes -= found->second; allocations.erase(found); std::free(memory);
}
extern "C" void lv_image_cache_drop(const void *source) { assert(source); cache_drops.push_back(source); }

using ch_mood::Tone;
static uint16_t pixel(const lv_image_dsc_t *source, unsigned x, unsigned y) {
    uint16_t color; std::memcpy(&color, source->data + y * source->header.stride + x * 2, 2); return color;
}
static uint16_t rgb565(unsigned hex) {
    return static_cast<uint16_t>(((hex >> 19) & 31) << 11 | ((hex >> 10) & 63) << 5 | ((hex >> 3) & 31));
}
static int brightness(uint16_t color) {
    const int r = (color >> 11) & 31, g = (color >> 5) & 63, b = color & 31;
    return std::max({(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)});
}
static void priority() {
    ch::State state; state.mood = 85; assert(ch_mood::choose(state) == Tone::Happy);
    state.mood = 84; assert(ch_mood::choose(state) == Tone::Original);
    state.mood = 40; assert(ch_mood::choose(state) == Tone::Original);
    state.mood = 39; assert(ch_mood::choose(state) == Tone::Unhappy);
    state.fullness = 25; assert(ch_mood::choose(state) == Tone::Hungry);
    state.fullness = 26; assert(ch_mood::choose(state) == Tone::Unhappy);
    state.mood = 99; state.fullness = 25; assert(ch_mood::choose(state) == Tone::Hungry);
    state.sick = true; assert(ch_mood::choose(state) == Tone::Sick);
    state.sleeping = true; assert(ch_mood::choose(state) == Tone::Original);
    state.sick = false; assert(ch_mood::choose(state) == Tone::Original);
}
static void target_colors_and_shading() {
    const uint16_t bases[] = {0xB6C8, 0xA6AA, 0x96C7};
    const uint16_t shadows[] = {0x2C4B, 0x33CA, 0x75AC};
    const unsigned targets[] = {0xB2D94C, 0xC88845, 0xB6B978, 0x8A9E93};
    const Tone tones[] = {Tone::Happy, Tone::Unhappy, Tone::Hungry, Tone::Sick};
    for (int stage = 0; stage < CH_STAGE_COUNT; ++stage) {
        uint8_t data[6]{}; std::memcpy(data, &bases[stage], 2); std::memcpy(data + 2, &shadows[stage], 2); data[4] = data[5] = 255;
        lv_image_dsc_t source{}; source.header.magic = LV_IMAGE_HEADER_MAGIC; source.header.cf = LV_COLOR_FORMAT_RGB565A8;
        source.header.w = 2; source.header.h = 1; source.header.stride = 4; source.data_size = sizeof(data); source.data = data;
        for (unsigned i = 0; i < 4; ++i) {
            const auto *output = ch_mood::frame(&source, tones[i], stage); assert(output != &source);
            if (pixel(output, 0, 0) != rgb565(targets[i])) std::fprintf(stderr,"Target mismatch stage=%d tone=%u actual=%04x expected=%04x\n",stage,i,pixel(output,0,0),rgb565(targets[i]));
            assert(pixel(output, 0, 0) == rgb565(targets[i]) && "dominant green must match the user-approved RGB565 target");
            assert(brightness(pixel(output, 1, 0)) < brightness(pixel(output, 0, 0)) && "body shadows must stay darker");
            assert(output->data[4] == 255 && output->data[5] == 255);
        }
        ch_mood::clear(); assert(live_bytes == 0);
    }
}
static void buffers_and_fallbacks() {
    const auto *first = ch_stages[CH_STAGE_BABY].animations[CH_IDLE].frames[0];
    const auto *second = ch_stages[CH_STAGE_BABY].animations[CH_IDLE].frames[1];
    const auto *third = ch_stages[CH_STAGE_TEEN].animations[CH_IDLE].frames[0];
    const auto before = allocation_calls;
    assert(ch_mood::frame(first, Tone::Original) == first && allocation_calls == before);
    fail_allocation = true; assert(ch_mood::frame(first, Tone::Happy) == first); fail_allocation = false;
    const auto *a = ch_mood::frame(first, Tone::Happy); assert(a != first && ch_mood::source_for(a) == first);
    const auto drops = cache_drops.size(); assert(ch_mood::frame(first, Tone::Happy) == a && cache_drops.size() == drops);
    const auto *b = ch_mood::frame(second, Tone::Happy); assert(a != b && ch_mood::source_for(a) == first);
    const auto *c = ch_mood::frame(third, Tone::Sick); assert(c == a && ch_mood::source_for(c) == third);
    assert(cache_drops.size() == drops + 1 && cache_drops.back() == c);
    assert(allocation_calls == before + 2 && live_bytes == 2u * 128u * 128u * 3u);
    assert(ch_mood::frame(c, Tone::Original) == third);
    lv_image_dsc_t invalid = *first; invalid.header.cf = LV_COLOR_FORMAT_RGB565;
    assert(ch_mood::frame(&invalid, Tone::Happy) == &invalid);
    invalid = *first; invalid.header.stride = 1; assert(ch_mood::frame(&invalid, Tone::Happy) == &invalid);
    invalid = *first; invalid.data_size = 1; assert(ch_mood::frame(&invalid, Tone::Happy) == &invalid);
    const auto reject = [&](lv_image_dsc_t &descriptor) {
        const auto calls = allocation_calls, drops_before = cache_drops.size();
        assert(ch_mood::frame(&descriptor, Tone::Happy, CH_STAGE_BABY) == &descriptor);
        assert(allocation_calls == calls && cache_drops.size() == drops_before);
    };
    invalid = *first; invalid.header.stride = first->header.w * 2 - 2; reject(invalid);
    invalid = *first; invalid.header.w = 0; reject(invalid);
    invalid = *first; invalid.header.h = 0; reject(invalid);
    invalid = *first; invalid.data_size = 128u * 128u * 3u + 1; reject(invalid);
    invalid = *first; invalid.header.w = 1; invalid.header.h = 1; invalid.header.stride = 49154; reject(invalid);
    // This RGB plane fits uint32_t, while adding its alpha plane wraps on
    // ESP32. Host size_t is 64-bit, so prove the 32-bit case independently.
    constexpr uint32_t hostile_rgb_bytes = 43778u * 65406u;
    constexpr uint32_t hostile_required = hostile_rgb_bytes + hostile_rgb_bytes / 2;
    static_assert(hostile_rgb_bytes == 2863343868u && hostile_required == 48506u,
                  "malformed header must reproduce the ESP32 size calculation overflow");
    invalid = *first; invalid.header.w = 1; invalid.header.h = 65406;
    invalid.header.stride = 43778; invalid.data_size = hostile_required; reject(invalid);
    assert(ch_mood::frame(nullptr, Tone::Happy) == nullptr);
    ch_mood::clear(); assert(live_bytes == 0 && allocations.empty());
}
static std::pair<unsigned, unsigned> locate(const lv_image_dsc_t *source, uint16_t color) {
    const size_t rgb_bytes = source->header.stride * source->header.h;
    for (unsigned y = 0; y < source->header.h; ++y) for (unsigned x = 0; x < source->header.w; ++x)
        if (source->data[rgb_bytes + y * (source->header.stride / 2) + x] && pixel(source, x, y) == color)
            return {x, y};
    assert(false && "Independent test sample must exist in the approved source");
    return {0, 0};
}
static void adult_sick_cache_classification() {
    // #189A5A is actual crest/body green in the new sick drawings, but an
    // immutable cyan/shadow role in the older adult poses. Same tone/stage
    // must never make the two source classifications share a memo result.
    const uint16_t shared = 0x1CCB, turquoise = 0x4EB4;
    const auto &sick = ch_stages[CH_STAGE_ADULT].animations[CH_SICK];
    const auto &idle = ch_stages[CH_STAGE_ADULT].animations[CH_IDLE];
    const auto allocations_before = allocation_calls;
    for (const Tone tone : {Tone::Happy, Tone::Unhappy, Tone::Hungry, Tone::Sick}) {
        for (int i = 0; i < sick.count; ++i) {
            const auto *sick_source = sick.frames[i];
            const auto *idle_source = idle.frames[i % idle.count];
            const auto sick_point = locate(sick_source, shared);
            const auto cyan_point = locate(sick_source, turquoise);
            const auto idle_point = locate(idle_source, shared);
            const auto *first = ch_mood::frame(sick_source, tone);
            const uint16_t tinted = pixel(first, sick_point.first, sick_point.second);
            assert(tinted != shared && pixel(first, cyan_point.first, cyan_point.second) == turquoise);
            const auto *second = ch_mood::frame(idle_source, tone);
            assert(pixel(second, idle_point.first, idle_point.second) == shared);
            // A different sick source forces a rewrite of the two-slot cache,
            // rather than returning the still-cached first sick descriptor.
            const auto *next_sick = sick.frames[(i + 1) % sick.count];
            const auto next_point = locate(next_sick, shared);
            const auto *third = ch_mood::frame(next_sick, tone);
            assert(pixel(third, next_point.first, next_point.second) == tinted);
        }
        ch_mood::clear(); assert(live_bytes == 0);
    }
    assert(allocation_calls == allocations_before + 8 && peak_bytes == 98304);
    std::puts("Adult sick -> idle -> next sick cache classification passed in all four tones; old idle cyan/shadows unchanged.");
}
static void approved_frames() {
    // Independent approved color sets from all 246 shipped RGB565 frames.
    // Every other shade (eyes, abdomen, outlines, cyan, effects) is immutable.
    const std::set<uint16_t> bodies[CH_STAGE_COUNT] = {
        {0xAEC8,0xAEE8,0xA6A8,0xBF09,0xB6C8,0x2C8B,0x2C4B,0x34CB},
        {0x552C,0x550C,0x9E6A,0x4CEB,0xA68A,0xA6AA,0xC6E9,0x33CA,0x964A,0x654B,0x544A},
        {0x9EE7,0x9EC7,0x96C7,0xB727,0x75AC}
    };
    const std::set<uint16_t> cyan = {0x1BEA,0x1C8B,0x1CAB,0x1CCB,0x24AB,0x24CB,0x2D0D,0x4EB4};
    // Independently inspected only in the four new adult sick drawings:
    // these dark greens fill the crest, torso and coiled tail. #4AD7A5
    // (0x4EB4, hue 158) remains the actual wide turquoise stripe.
    const std::set<uint16_t> adult_sick_greens = {0x1BEA,0x1C8B,0x1CAB,0x1CCB,0x24AB,0x24CB,0x2D0D};
    const std::set<uint16_t> adult_sick_cyan = {0x4EB4};
    const Tone tones[] = {Tone::Happy,Tone::Unhappy,Tone::Hungry,Tone::Sick};
    size_t total_frames = 0, body_pixels[CH_STAGE_COUNT]{}, cyan_pixels = 0;
    size_t adult_sick_body = 0, adult_sick_added = 0, adult_sick_stripes = 0;
    const auto allocation_start = allocation_calls;
    for (int stage = 0; stage < CH_STAGE_COUNT; ++stage) for (int pose = 0; pose < CH_ANIM_COUNT; ++pose) {
        const auto &animation = ch_stages[stage].animations[pose];
        for (int index = 0; index < animation.count; ++index) {
            ++total_frames; const auto *source = animation.frames[index];
            const std::vector<uint8_t> original(source->data, source->data + source->data_size);
            assert(ch_mood::frame(source, Tone::Original) == source);
            for (const auto tone : tones) {
                const auto *output = ch_mood::frame(source, tone); // Also exercise automatic stage lookup.
                assert(output != source && ch_mood::source_for(output) == source);
                assert(std::memcmp(&output->header, &source->header, sizeof(source->header)) == 0);
                assert(output->data_size == source->data_size);
                const size_t rgb_bytes = source->header.stride * source->header.h;
                assert(std::memcmp(source->data + rgb_bytes, output->data + rgb_bytes, source->data_size - rgb_bytes) == 0);
                for (unsigned y = 0; y < source->header.h; ++y) for (unsigned x = 0; x < source->header.w; ++x) {
                    const auto color = pixel(source,x,y), changed = pixel(output,x,y);
                    const auto alpha = source->data[rgb_bytes + y * (source->header.stride / 2) + x];
                    const bool sick_drawing = stage == CH_STAGE_ADULT && pose == CH_SICK;
                    const bool extra_sick_green = sick_drawing && adult_sick_greens.count(color);
                    const bool body = alpha && (bodies[stage].count(color) || extra_sick_green);
                    if (body) { assert(changed != color); if (tone == Tone::Happy) ++body_pixels[stage]; }
                    else assert(changed == color && "approved non-body pixel was recolored");
                    const auto &protected_cyan = sick_drawing ? adult_sick_cyan : cyan;
                    if (stage == CH_STAGE_ADULT && alpha && protected_cyan.count(color)) { assert(changed == color); if (tone == Tone::Happy) ++cyan_pixels; }
                    if (sick_drawing && alpha && tone == Tone::Happy) {
                        adult_sick_body += body; adult_sick_added += extra_sick_green;
                        adult_sick_stripes += adult_sick_cyan.count(color);
                    }
                }
                assert(std::memcmp(source->data, original.data(), original.size()) == 0 && "flash asset data was modified");
            }
        }
    }
    assert(total_frames == CH_FRAME_COUNT && total_frames == 246);
    assert(adult_sick_body == 9940 && adult_sick_added == 5019 && adult_sick_stripes == 1107);
    std::printf("New adult sick four-frame regions: body %zu (newly covered %zu), protected turquoise %zu; other 78 adult frames retain old palette roles.\n",
                adult_sick_body, adult_sick_added, adult_sick_stripes);
    std::printf("Asset regions: body %zu/%zu/%zu; adult cyan %zu.\n",body_pixels[0],body_pixels[1],body_pixels[2],cyan_pixels);
    std::fflush(stdout);
    assert(body_pixels[0] > 130000 && body_pixels[1] > 190000 && body_pixels[2] > 90000 && cyan_pixels > 90000);
    assert(allocation_calls == allocation_start + 2 && peak_bytes == 98304 && "PSRAM usage must stay at two frames across all tones/stages");
    ch_mood::clear(); assert(live_bytes == 0);
    std::printf("All %zu approved frames checked in four tones; body pixels %zu/%zu/%zu; adult cyan %zu; peak pixels buffer %zu bytes.\n",
                total_frames,body_pixels[0],body_pixels[1],body_pixels[2],cyan_pixels,peak_bytes);
}
int main() {
    priority(); target_colors_and_shading(); buffers_and_fallbacks(); adult_sick_cache_classification(); approved_frames();
    std::puts("Runtime mood-color checks passed: sleep priority, exact targets, shading, preserved details/alpha/assets, cache invalidation, PSRAM reuse, failure fallback.");
}
