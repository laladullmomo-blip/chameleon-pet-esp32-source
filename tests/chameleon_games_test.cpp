#include "chameleon_games.h"

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
void require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
bool near(float a, float b, float epsilon = 0.001f) { return std::fabs(a - b) < epsilon; }
int bug_card(const ch::games::Session &game) {
    for (int card = 0; card < 3; ++card) if (game.leaf_has_bug(card)) return card;
    throw std::runtime_error("hidden bug is missing");
}
int pose_option(ch::Pose pose) {
    if (pose == ch::Pose::Blink) return 0;
    if (pose == ch::Pose::Wave) return 1;
    if (pose == ch::Pose::Stretch) return 2;
    throw std::runtime_error("demo used a pose without an input button");
}
}

int main() {
    using ch::GrowthStage;
    using ch::Pose;
    using namespace ch::games;
    const std::vector<std::pair<const char *, std::function<void()>>> cases = {
        {"inactive and finished sessions cannot score or reopen", [] {
            Session game;
            require(game.phase() == Phase::Finished && game.choose(0, 100) == Result::None,
                "default session accepts a click");
            game.start(Kind::Hunt, GrowthStage::Baby, 100, 1);
            require(game.choose(0, 101) == Result::Hit && game.hits() == 1, "hunt cannot score");
            game.finish(); game.finish();
            require(game.choose(0, 10000) == Result::None && game.tick(20000) == Result::None
                && game.drop(20000) == Result::None && !game.drag(144, 388, 20000)
                && game.hits() == 1 && game.phase() == Phase::Finished, "finishing was not idempotent");
            game.start(Kind::Mimic, static_cast<GrowthStage>(255), 30000, 0);
            require(game.stage() == GrowthStage::Baby && game.hits() == 0 && game.misses() == 0,
                "restart failed to reset or normalise invalid growth stage");
            game.start(static_cast<Kind>(255), GrowthStage::Adult, 40000, 1);
            require(game.phase() == Phase::Finished, "unknown game became playable");
        }},
        {"hunt follows slow bounded paths and varies all four food species", [] {
            Session game, same;
            game.start(Kind::Hunt, GrowthStage::Baby, 100, 0xC0FFEE);
            same.start(Kind::Hunt, GrowthStage::Baby, 100, 0xC0FFEE);
            require(game.bug_item() == same.bug_item() && near(game.bug_x(200), same.bug_x(200))
                && near(game.bug_y(200), same.bug_y(200)), "fixed seeds are not reproducible");
            float previous_x = game.bug_x(100), previous_y = game.bug_y(100);
            for (uint64_t time = 140; time < 6000; time += 40) {
                const float x = game.bug_x(time), y = game.bug_y(time);
                require(x >= 374 && x <= 408 && y >= 208 && y <= 280, "bug escaped its touch area");
                require(std::fabs(x - previous_x) < 0.4f && std::fabs(y - previous_y) < 0.6f,
                    "bug moves too far during one UI frame");
                previous_x = x; previous_y = y;
            }
            unsigned species = 0;
            uint64_t time = 100;
            for (int round = 0; round < 32; ++round) {
                species |= 1u << static_cast<int>(game.bug_item());
                const auto previous = game.bug_item();
                require(game.choose(1, time) == Result::None, "invisible hunt option scored");
                require(game.choose(0, time) == Result::Hit, "visible bug ignored");
                require(game.choose(0, time + 1) == Result::None, "one bug scored twice");
                game.tick(time + 1399);
                require(game.phase() == Phase::Feedback, "eat feedback ended before all frames");
                time += 1400; game.tick(time);
                require(game.phase() == Phase::Input && game.bug_item() != previous,
                    "new round reused the last species or stayed in feedback");
            }
            require(species == 15 && game.hits() == 32, "hunt has missing food species or lost hits");
        }},
        {"hunt escapes use growth difficulty and emit each miss once", [] {
            for (int stage = 0; stage < 3; ++stage) {
                Session game;
                game.start(Kind::Hunt, static_cast<GrowthStage>(stage), 200, 42);
                const uint64_t deadline = 200 + 6000 - 1000 * stage;
                require(game.tick(deadline - 1) == Result::None && game.phase() == Phase::Input,
                    "bug disappeared before its stage window");
                require(game.tick(deadline) == Result::Miss && game.misses() == 1
                    && game.phase() == Phase::Feedback, "expired bug did not register exactly one miss");
                require(game.tick(deadline) == Result::None && game.choose(0, deadline + 1) == Result::None,
                    "expired bug can still be caught");
                game.tick(deadline + 800);
                require(game.phase() == Phase::Input && game.round() == 2, "miss did not resume another round");
            }
        }},
        {"leaves reveal before input and shuffle continuously by growth stage", [] {
            for (int stage = 0; stage < 3; ++stage) {
                Session game;
                game.start(Kind::Leaves, static_cast<GrowthStage>(stage), 100, 0xC0FFEE);
                const int hidden = bug_card(game);
                require(!game.leaf_has_bug(-1) && !game.leaf_has_bug(3)
                    && game.leaf_position(-1, 100) == -1 && game.leaf_position(3, 100) == -1,
                    "out-of-range card was exposed");
                require(game.choose(hidden, 999) == Result::None && game.phase() == Phase::Reveal,
                    "reveal accepted a premature answer");
                game.tick(1000);
                require(game.phase() == (stage ? Phase::Shuffle : Phase::Input), "wrong shuffle count for age");
                for (int step = 0; step < stage; ++step) {
                    const uint64_t started = 1000 + step * 600;
                    float from[3], midpoint[3], before[3];
                    for (int card = 0; card < 3; ++card) {
                        from[card] = game.leaf_position(card, started);
                        midpoint[card] = game.leaf_position(card, started + 300);
                        before[card] = game.leaf_position(card, started + 599);
                    }
                    int moving = 0;
                    for (int card = 0; card < 3; ++card) moving += !near(from[card], midpoint[card]);
                    require(moving == 2 && near(midpoint[0] + midpoint[1] + midpoint[2], 3),
                        "shuffle teleported a card or failed to exchange exactly two");
                    require(game.choose(hidden, started + 300) == Result::None,
                        "a moving card accepted an answer");
                    game.tick(started + 600);
                    for (int card = 0; card < 3; ++card)
                        require(near(before[card], game.leaf_position(card, started + 600)),
                            "shuffle jumped at a segment boundary");
                }
                const uint64_t ready = 1000 + stage * 600;
                require(game.phase() == Phase::Input && game.hits() == 0 && game.misses() == 0,
                    "shuffle produced a score or never enabled input");
                unsigned slots = 0;
                for (int card = 0; card < 3; ++card) slots |= 1u << static_cast<int>(game.leaf_position(card, ready));
                require(slots == 7, "final leaf positions overlap");
                require(game.choose(hidden, ready) == Result::Hit && game.hits() == 1,
                    "tracked hidden card was not accepted");
                game.tick(ready + 1399);
                require(game.phase() == Phase::Feedback, "correct leaf answer skipped eat feedback");
                game.tick(ready + 1400);
                require(game.phase() == Phase::Reveal && game.round() == 2, "next leaves round was not revealed");
            }
        }},
        {"leaves wrong answers score a miss and feedback blocks double input", [] {
            Session game;
            game.start(Kind::Leaves, GrowthStage::Adult, 0, 17);
            const int hidden = bug_card(game);
            game.tick(2100);
            require(game.choose(-1, 2100) == Result::None && game.choose(3, 2100) == Result::None,
                "invalid card IDs affected scoring");
            require(game.choose((hidden + 1) % 3, 2100) == Result::Miss && game.hits() == 0 && game.misses() == 1,
                "wrong card did not miss");
            require(game.choose(hidden, 2101) == Result::None, "feedback accepted another card");
            game.tick(2899);
            require(game.phase() == Phase::Feedback, "miss feedback ended early");
            game.tick(2900);
            require(game.phase() == Phase::Reveal, "miss did not start a new round");
        }},
        {"mimic shows real actions with quiet gaps and awards complete sequences", [] {
            for (int stage = 0; stage < 3; ++stage) {
                Session game;
                game.start(Kind::Mimic, static_cast<GrowthStage>(stage), 100, 0xC0FFEE);
                uint64_t started = 100;
                for (int round = 0; round < 4; ++round) {
                    const int expected_length = 1 + stage + (round < 2 ? round : 2);
                    require(game.sequence_length() == expected_length, "mimic difficulty did not follow age and progress");
                    int answers[5] = {};
                    for (int step = 0; step < expected_length; ++step) {
                        const uint64_t at = started + step * 900;
                        answers[step] = pose_option(game.demo_pose(at));
                        require(game.demo_started(at) == at && game.demo_duration() == 720,
                            "UI cannot align the actual action frame clock");
                        require(game.demo_pose(at + 719) != Pose::Idle && game.demo_pose(at + 720) == Pose::Idle,
                            "demonstration omitted its rest gap");
                        if (step) require(answers[step] != answers[step - 1], "adjacent actions were visually indistinguishable");
                    }
                    const uint64_t ready = started + expected_length * 900;
                    require(game.choose(answers[0], ready - 1) == Result::None, "demo accepts premature input");
                    game.tick(ready);
                    require(game.phase() == Phase::Input && game.demo_pose(ready) == Pose::Idle,
                        "mimic did not stop demonstration before input");
                    for (int step = 0; step < expected_length; ++step) {
                        const Result result = game.choose(answers[step], ready + step);
                        require(result == (step + 1 == expected_length ? Result::Hit : Result::None),
                            "partial input incorrectly completed a round");
                        require(game.input_progress() == step + 1 && game.hits() == round + (step + 1 == expected_length),
                            "mimic progress or hits drifted");
                    }
                    require(game.choose(answers[0], ready + expected_length) == Result::None, "full sequence scored twice");
                    started = ready + expected_length - 1 + 1000;
                    game.tick(started);
                }
            }
        }},
        {"mimic errors cannot earn partial rewards", [] {
            Session game;
            game.start(Kind::Mimic, GrowthStage::Adult, 0, 99);
            const int first = pose_option(game.demo_pose(0)), second = pose_option(game.demo_pose(900));
            game.tick(2700);
            require(game.choose(255, 2700) == Result::None && game.input_progress() == 0,
                "invalid action advanced the sequence");
            require(game.choose(first, 2700) == Result::None && game.hits() == 0 && game.input_progress() == 1,
                "partial sequence earned a reward");
            require(game.choose((second + 1) % 3, 2701) == Result::Miss && game.hits() == 0 && game.misses() == 1,
                "wrong remembered action did not miss");
            game.tick(3701);
            require(game.sequence_length() == 3 && game.input_progress() == 0,
                "failed sequence increased difficulty or leaked progress");
        }},
        {"ball supports broad age-based goals and rejects off-goal drops", [] {
            for (int stage = 0; stage < 3; ++stage) {
                Session game;
                game.start(Kind::Ball, static_cast<GrowthStage>(stage), 100, 42);
                const float radius = 40.0f - 6.0f * stage;
                const float direction = game.goal_index() == 2 ? -1.0f : 1.0f;
                require(game.drag(game.goal_x() + direction * radius, game.goal_y(), 101)
                    && game.drop(102) == Result::Hit && game.hits() == 1,
                    "goal boundary rejected a valid age tolerance");
                require(!game.drag(233, 340, 103) && game.drop(103) == Result::None,
                    "ball input during feedback scored twice");
                const int old_goal = game.goal_index();
                game.tick(1701);
                require(game.phase() == Phase::Feedback && game.goal_index() == old_goal
                    && !game.drag(233, 340, 1701) && game.drop(1701) == Result::None,
                    "ball accepted input before its full walk, wave and happy response");
                game.tick(1702);
                require(game.goal_index() != old_goal && near(game.ball_x(), 233) && near(game.ball_y(), 340),
                    "next ball round did not reset or vary its target");
                const float second_direction = game.goal_index() == 2 ? -1.0f : 1.0f;
                game.drag(game.goal_x() + second_direction * (radius + 0.5f), game.goal_y(), 1703);
                require(game.drop(1704) == Result::Miss && game.misses() == 1,
                    "off-goal ball drop was counted as a hit");
                game.tick(2703);
                require(game.phase() == Phase::Feedback, "ball miss feedback ended before one second");
                game.tick(2704);
                require(game.phase() == Phase::Input, "ball miss inherited the longer success feedback");
            }
        }},
        {"ball requires a real drag and keeps malformed coordinates out", [] {
            Session game;
            game.start(Kind::Ball, GrowthStage::Baby, 0, 1);
            require(game.drop(1) == Result::None && game.hits() == 0 && game.misses() == 0,
                "a tap submitted an undragged ball");
            game.drag(238, 340, 2);
            require(game.drop(3) == Result::None && game.phase() == Phase::Input,
                "sub-six-pixel jitter was submitted");
            require(!game.drag(std::numeric_limits<float>::quiet_NaN(), 340, 4)
                && !game.drag(233, std::numeric_limits<float>::infinity(), 4)
                && near(game.ball_x(), 238), "nonfinite input corrupted ball position");
            require(game.drag(-10000, 10000, 5) && near(game.ball_x(), 130) && near(game.ball_y(), 396),
                "drag escaped the circular-screen ball area");
            require(game.drag(10000, -10000, 6) && near(game.ball_x(), 336) && near(game.ball_y(), 338),
                "opposite boundary failed to clamp");
        }},
        {"adult ball deadline cannot be bypassed by drag or drop", [] {
            for (int entry = 0; entry < 3; ++entry) {
                Session game;
                game.start(Kind::Ball, GrowthStage::Adult, 100, 1);
                game.drag(game.goal_x(), game.goal_y(), 6099);
                require(game.phase() == Phase::Input, "adult ball expired before six seconds");
                if (entry == 0) require(game.tick(6100) == Result::Miss, "timer missed adult expiration");
                if (entry == 1) require(game.drop(6100) == Result::Miss, "drop bypassed adult expiration");
                if (entry == 2) require(!game.drag(233, 340, 6100), "drag bypassed adult expiration");
                require(game.misses() == 1 && game.hits() == 0 && game.phase() == Phase::Feedback,
                    "adult expiration awarded or duplicated a hit");
            }
            for (const GrowthStage stage : {GrowthStage::Baby, GrowthStage::Teen}) {
                Session game;
                game.start(Kind::Ball, stage, 0, 1);
                require(game.tick(60000) == Result::None && game.phase() == Phase::Input && game.misses() == 0,
                    "young ball inherited the adult deadline");
            }
        }},
        {"backward clocks and huge gaps do not duplicate score or undo phases", [] {
            Session game;
            game.start(Kind::Hunt, GrowthStage::Adult, 500, 1);
            require(game.choose(0, 499) == Result::None && game.hits() == 0, "backward input scored");
            game.choose(0, 600);
            require(game.tick(599) == Result::None && game.phase() == Phase::Feedback
                && game.choose(0, 599) == Result::None && game.hits() == 1,
                "backward clock bypassed feedback");
            game.tick(2000);
            require(game.round() == 2 && game.bug_x(100) == game.bug_x(2000), "backward draw rewound the current round");
            const uint64_t huge = std::numeric_limits<uint64_t>::max() - 100;
            require(game.tick(huge) == Result::Miss && game.misses() == 1,
                "large gap produced multiple imaginary rounds");
            require(game.tick(huge) == Result::None && game.misses() == 1, "unchanged huge clock duplicated miss");
            game.start(Kind::Mimic, GrowthStage::Adult, huge, 1);
            require(game.tick(std::numeric_limits<uint64_t>::max()) == Result::None
                && game.phase() == Phase::Reveal && game.demo_pose(huge) != Pose::Idle,
                "clock near uint64 max overflowed a phase deadline");
            game.start(Kind::Leaves, GrowthStage::Adult, 0, 1);
            game.tick(2100);
            game.choose(bug_card(game), 2100);
            game.tick(2100 - 1);
            require(game.phase() == Phase::Feedback && game.hits() == 1, "leaf clock regressed");
        }},
    };
    int passed = 0;
    for (const auto &test : cases) {
        try { test.second(); ++passed; std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception &error) { std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; return 1; }
    }
    std::cout << passed << " game checks passed\n";
    return 0;
}
