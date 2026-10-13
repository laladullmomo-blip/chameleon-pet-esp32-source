#include "chameleon_games.h"

#include <cmath>
#include <limits>

namespace ch::games {
namespace {
constexpr uint32_t kRevealMs = 900, kShuffleMs = 600, kDemoStepMs = 900;
int stage_index(GrowthStage stage) { return static_cast<int>(stage); }
float clamp(float value, float low, float high) { return value < low ? low : value > high ? high : value; }
void increment(int &value) { if (value < std::numeric_limits<int>::max()) ++value; }
float travel(float from, float low, float high, bool forward, uint64_t elapsed, float pixels_per_second) {
    const double width = high - low;
    // Reduce before converting so even uint64_t clock extremes stay precise.
    const double period_ms = width * 2000.0 / pixels_per_second;
    const double offset = std::fmod(static_cast<double>(elapsed), period_ms) * pixels_per_second / 1000.0;
    double position = std::fmod(from - low + (forward ? offset : -offset), width * 2.0);
    if (position < 0) position += width * 2.0;
    if (position > width) position = width * 2.0 - position;
    return low + static_cast<float>(position);
}
Pose option_pose(int option) {
    return option == 0 ? Pose::Blink : option == 1 ? Pose::Wave : Pose::Stretch;
}
}

uint32_t Session::random() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}

uint64_t Session::display_time(uint64_t now) const { return now < clock_ ? clock_ : now; }

void Session::start(Kind kind, GrowthStage stage, uint64_t now, uint32_t seed) {
    *this = Session{};
    kind_ = kind;
    stage_ = stage_index(stage) >= 0 && stage_index(stage) <= 2 ? stage : GrowthStage::Baby;
    clock_ = now;
    rng_ = seed ? seed : 0x4a55414eu;
    if (static_cast<unsigned>(kind) > static_cast<unsigned>(Kind::Ball)) return;
    begin_round(now);
}

void Session::begin_round(uint64_t now) {
    increment(round_);
    round_started_ = phase_started_ = now;
    last_result_ = Result::None;
    input_progress_ = 0;
    if (kind_ == Kind::Hunt) {
        phase_ = Phase::Input;
        int species = static_cast<int>(random() % 4);
        if (species == previous_bug_) species = (species + 1 + static_cast<int>(random() % 3)) % 4;
        previous_bug_ = species;
        bug_item_ = static_cast<Item>(species);
        bug_from_x_ = 374.0f + static_cast<float>(random() % 35);
        bug_from_y_ = 208.0f + static_cast<float>(random() % 73);
        bug_forward_x_ = (random() & 1u) != 0;
        bug_forward_y_ = (random() & 1u) != 0;
    } else if (kind_ == Kind::Leaves) {
        phase_ = Phase::Reveal;
        bug_card_ = static_cast<int>(random() % 3);
        shuffle_count_ = stage_index(stage_);
        shuffle_step_ = 0;
        for (int card = 0; card < 3; ++card) leaf_slots_[card] = card;
        for (int step = 0; step < shuffle_count_; ++step) {
            // Adult swaps use distinct pairs so the second animation cannot
            // simply undo the first or leave the hidden card in its old slot.
            swap_a_[step] = step == 0 ? bug_card_ : swap_b_[0];
            swap_b_[step] = (swap_a_[step] + 1 + static_cast<int>(random() % 2)) % 3;
            if (step && swap_b_[step] == swap_a_[0]) swap_b_[step] = 3 - swap_a_[step] - swap_a_[0];
        }
    } else if (kind_ == Kind::Mimic) {
        phase_ = Phase::Reveal;
        const int base = 1 + stage_index(stage_), limit = base + 2;
        sequence_length_ = base + mimic_successes_;
        if (sequence_length_ > limit) sequence_length_ = limit;
        for (int index = 0; index < sequence_length_; ++index) {
            int option = static_cast<int>(random() % 3);
            if (index && option == sequence_[index - 1]) option = (option + 1 + static_cast<int>(random() % 2)) % 3;
            sequence_[index] = static_cast<uint8_t>(option);
        }
    } else {
        phase_ = Phase::Input;
        goal_ = static_cast<int>(random() % 3);
        if (goal_ == previous_goal_) goal_ = (goal_ + 1 + static_cast<int>(random() % 2)) % 3;
        previous_goal_ = goal_;
        ball_x_ = 233;
        ball_y_ = 340;
        ball_dragged_ = false;
        drag_distance_sq_ = 0;
    }
}

void Session::begin_shuffle(uint64_t now) {
    phase_ = Phase::Shuffle;
    phase_started_ = now;
    for (int card = 0; card < 3; ++card) shuffle_from_[card] = shuffle_to_[card] = leaf_slots_[card];
    const int a = swap_a_[shuffle_step_], b = swap_b_[shuffle_step_];
    shuffle_to_[a] = shuffle_from_[b];
    shuffle_to_[b] = shuffle_from_[a];
}

Result Session::resolve(Result result, uint64_t now, uint32_t feedback_ms) {
    phase_ = Phase::Feedback;
    phase_started_ = now;
    feedback_ms_ = feedback_ms;
    last_result_ = result;
    if (result == Result::Hit) {
        increment(hits_);
        if (kind_ == Kind::Mimic && mimic_successes_ < 2) ++mimic_successes_;
    } else if (result == Result::Miss) increment(misses_);
    return result;
}

Result Session::tick(uint64_t now) {
    if (phase_ == Phase::Finished || now < clock_) return Result::None;
    clock_ = now;
    if (phase_ == Phase::Feedback) {
        if (now - phase_started_ >= feedback_ms_) begin_round(now);
        return Result::None;
    }
    if (kind_ == Kind::Leaves) {
        if (phase_ == Phase::Reveal && now - phase_started_ >= kRevealMs) {
            const uint64_t next = phase_started_ + kRevealMs;
            if (shuffle_count_) begin_shuffle(next);
            else { phase_ = Phase::Input; phase_started_ = next; }
        }
        while (phase_ == Phase::Shuffle && now - phase_started_ >= kShuffleMs) {
            for (int card = 0; card < 3; ++card) leaf_slots_[card] = shuffle_to_[card];
            ++shuffle_step_;
            const uint64_t next = phase_started_ + kShuffleMs;
            if (shuffle_step_ < shuffle_count_) begin_shuffle(next);
            else { phase_ = Phase::Input; phase_started_ = next; }
        }
    } else if (kind_ == Kind::Mimic && phase_ == Phase::Reveal) {
        const uint32_t total = static_cast<uint32_t>(sequence_length_) * kDemoStepMs;
        if (now - phase_started_ >= total) {
            phase_ = Phase::Input;
            phase_started_ += total;
        }
    } else if (phase_ == Phase::Input) {
        if (kind_ == Kind::Hunt && now - round_started_ >= 6000u - 1000u * stage_index(stage_)) {
            return resolve(Result::Miss, now, 800);
        }
        if (kind_ == Kind::Ball && stage_ == GrowthStage::Adult && now - round_started_ >= 6000) {
            return resolve(Result::Miss, now, 800);
        }
    }
    return Result::None;
}

Result Session::choose(int option, uint64_t now) {
    if (phase_ == Phase::Finished || now < clock_) return Result::None;
    const Result elapsed = tick(now);
    if (elapsed != Result::None) return elapsed;
    if (phase_ != Phase::Input) return Result::None;
    if (kind_ == Kind::Hunt) {
        return option == 0 ? resolve(Result::Hit, now, 1400) : Result::None;
    }
    if (option < 0 || option > 2) return Result::None;
    if (kind_ == Kind::Leaves) {
        const bool hit = option == bug_card_;
        return resolve(hit ? Result::Hit : Result::Miss, now, hit ? 1400 : 800);
    }
    if (kind_ == Kind::Mimic) {
        if (option != sequence_[input_progress_]) return resolve(Result::Miss, now, 1000);
        ++input_progress_;
        if (input_progress_ == sequence_length_) return resolve(Result::Hit, now, 1000);
    }
    return Result::None;
}

bool Session::drag(float x, float y, uint64_t now) {
    if (kind_ != Kind::Ball || phase_ == Phase::Finished || now < clock_ || !std::isfinite(x) || !std::isfinite(y)) return false;
    tick(now);
    if (phase_ != Phase::Input) return false;
    ball_x_ = clamp(x, 130, 336);
    ball_y_ = clamp(y, 338, 396);
    const float dx = ball_x_ - 233, dy = ball_y_ - 340;
    const float distance = dx * dx + dy * dy;
    if (distance > drag_distance_sq_) drag_distance_sq_ = distance;
    ball_dragged_ = true;
    return true;
}

Result Session::drop(uint64_t now) {
    if (kind_ != Kind::Ball || phase_ == Phase::Finished || now < clock_) return Result::None;
    const Result elapsed = tick(now);
    if (elapsed != Result::None) return elapsed;
    if (phase_ != Phase::Input || !ball_dragged_ || drag_distance_sq_ < 36) return Result::None;
    const float dx = ball_x_ - goal_x(), dy = ball_y_ - goal_y();
    const float radius = 40.0f - 6.0f * stage_index(stage_);
    const bool hit = dx * dx + dy * dy <= radius * radius;
    return resolve(hit ? Result::Hit : Result::Miss, now, hit ? 1600 : 1000);
}

void Session::finish() { phase_ = Phase::Finished; }

float Session::bug_x(uint64_t now) const {
    const uint64_t sample = phase_ == Phase::Feedback || phase_ == Phase::Finished ? phase_started_ : display_time(now);
    return travel(bug_from_x_, 374, 408, bug_forward_x_, sample < round_started_ ? 0 : sample - round_started_, 8);
}

float Session::bug_y(uint64_t now) const {
    const uint64_t sample = phase_ == Phase::Feedback || phase_ == Phase::Finished ? phase_started_ : display_time(now);
    return travel(bug_from_y_, 208, 280, bug_forward_y_, sample < round_started_ ? 0 : sample - round_started_, 12);
}

float Session::leaf_position(int card, uint64_t now) const {
    if (card < 0 || card > 2) return -1;
    if (phase_ != Phase::Shuffle) return static_cast<float>(leaf_slots_[card]);
    const uint64_t sample = display_time(now);
    const uint64_t elapsed = sample < phase_started_ ? 0 : sample - phase_started_;
    float progress = elapsed >= kShuffleMs ? 1.0f : static_cast<float>(elapsed) / kShuffleMs;
    progress = progress * progress * (3.0f - 2.0f * progress);
    return shuffle_from_[card] + (shuffle_to_[card] - shuffle_from_[card]) * progress;
}

Pose Session::demo_pose(uint64_t now) const {
    if (kind_ != Kind::Mimic || phase_ != Phase::Reveal) return Pose::Idle;
    const uint64_t elapsed = display_time(now) - phase_started_;
    const uint64_t index = elapsed / kDemoStepMs;
    if (index >= static_cast<uint64_t>(sequence_length_) || elapsed % kDemoStepMs >= demo_duration()) return Pose::Idle;
    return option_pose(sequence_[index]);
}

uint64_t Session::demo_started(uint64_t now) const {
    if (kind_ != Kind::Mimic || phase_ != Phase::Reveal) return phase_started_;
    const uint64_t elapsed = display_time(now) - phase_started_;
    const uint64_t index = elapsed / kDemoStepMs;
    if (index >= static_cast<uint64_t>(sequence_length_)) return phase_started_;
    return phase_started_ + index * kDemoStepMs;
}

} // namespace ch::games
