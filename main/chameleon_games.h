#pragma once

#include "chameleon_state.h"

// Small, allocation-free round rules. The care Model owns the 20-second
// session clock and rewards; this class only scores individual rounds.
namespace ch::games {

enum class Kind : uint8_t { Hunt = 0, Leaves, Mimic, Ball };
enum class Phase : uint8_t { Reveal = 0, Shuffle, Input, Feedback, Finished };
enum class Result : uint8_t { None = 0, Hit, Miss };

class Session {
public:
    void start(Kind kind, GrowthStage stage, uint64_t now, uint32_t seed);
    Result tick(uint64_t now);
    Result choose(int option, uint64_t now);
    bool drag(float x, float y, uint64_t now);
    Result drop(uint64_t now);
    void finish();

    Kind kind() const { return kind_; }
    GrowthStage stage() const { return stage_; }
    Phase phase() const { return phase_; }
    int hits() const { return hits_; }
    int misses() const { return misses_; }
    int round() const { return round_; }
    Result last_result() const { return last_result_; }

    float bug_x(uint64_t now) const;
    float bug_y(uint64_t now) const;
    Item bug_item() const { return bug_item_; }
    float leaf_position(int card, uint64_t now) const;
    bool leaf_has_bug(int card) const { return card >= 0 && card < 3 && card == bug_card_; }
    Pose demo_pose(uint64_t now) const;
    uint64_t demo_started(uint64_t now) const;
    uint32_t demo_duration() const { return 720; }
    int sequence_length() const { return sequence_length_; }
    int input_progress() const { return input_progress_; }
    float ball_x() const { return ball_x_; }
    float ball_y() const { return ball_y_; }
    float goal_x() const { return 144.0f + 89.0f * goal_; }
    float goal_y() const { return 388.0f; }
    int goal_index() const { return goal_; }

private:
    uint32_t random();
    uint64_t display_time(uint64_t now) const;
    void begin_round(uint64_t now);
    void begin_shuffle(uint64_t now);
    Result resolve(Result result, uint64_t now, uint32_t feedback_ms);

    Kind kind_ = Kind::Hunt;
    GrowthStage stage_ = GrowthStage::Baby;
    Phase phase_ = Phase::Finished;
    Result last_result_ = Result::None;
    uint64_t clock_ = 0, phase_started_ = 0, round_started_ = 0;
    uint32_t rng_ = 0x4a55414eu, feedback_ms_ = 0;
    int hits_ = 0, misses_ = 0, round_ = 0;
    Item bug_item_ = Item::Cricket;
    int previous_bug_ = -1, bug_card_ = 0;
    float bug_from_x_ = 374, bug_from_y_ = 208;
    bool bug_forward_x_ = true, bug_forward_y_ = true;
    int leaf_slots_[3] = {0, 1, 2}, shuffle_from_[3] = {0, 1, 2};
    int shuffle_to_[3] = {0, 1, 2}, swap_a_[2] = {}, swap_b_[2] = {};
    int shuffle_count_ = 0, shuffle_step_ = 0;
    uint8_t sequence_[5] = {};
    int sequence_length_ = 1, input_progress_ = 0, mimic_successes_ = 0;
    float ball_x_ = 233, ball_y_ = 340, drag_distance_sq_ = 0;
    int goal_ = 1, previous_goal_ = -1;
    bool ball_dragged_ = false;
};

} // namespace ch::games
