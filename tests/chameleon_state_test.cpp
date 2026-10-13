#include "chameleon_state.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void same_stock(const ch::State &before, const ch::State &after) {
    require(before.coins == after.coins, "autonomy changed money");
    for (int i = 0; i < ch::kItemCount; ++i) require(before.inventory[i] == after.inventory[i], "autonomy changed inventory");
}
ch::Life life(ch::Model &model, uint64_t now, float tilt = 0, ch::Weather weather = ch::Weather::Clear,
              int hour = 12, bool busy = false, ch::Scene scene = ch::Scene::Forest) {
    return model.life_step(now, hour, weather, tilt, scene, true, 0, busy);
}
}

int main() {
    using namespace ch;
    const std::vector<std::pair<const char *, std::function<void()>>> cases = {
        {"fresh state agrees with desktop care and supplies", [] {
            Model m;
            require(m.state.health == 82 && m.state.fullness == 70 && m.state.energy == 65 && m.state.mood == 90 && m.state.cleanliness == 80, "wrong default care");
            require(m.state.coins == 30 && m.state.inventory[0] == 3 && m.state.inventory[3] == 4 && m.state.inventory[6] == 1, "wrong initial supplies");
            require(m.state.auto_life && m.state.auto_world && std::strcmp(m.state.name, "卷卷") == 0, "wrong initial settings");
        }},
        {"a new life starts as a baby and growth speed locks after real progress", [] {
            Model m;
            require(m.stage() == GrowthStage::Baby && !m.state.growth_ms
                && m.state.growth_speed == GrowthSpeed::Normal && m.growth_remaining_ms() == kGrowthTeenMs
                && !m.growth_paused(), "newborn growth defaults wrong");
            require(!m.set_growth_speed(static_cast<GrowthSpeed>(255)), "unknown growth speed accepted");
            require(m.set_growth_speed(GrowthSpeed::Fast), "new life cannot choose isolated fast growth");
            m.tick(1);
            require(m.state.growth_ms == 1 && !m.set_growth_speed(GrowthSpeed::Normal), "changing growth speed could erase or jump existing progress");
        }},
        {"precise growth requests flash persistence only at five-minute checkpoints", [] {
            State healthy;
            healthy.health = healthy.fullness = healthy.energy = healthy.mood = healthy.cleanliness = 100;
            healthy.sleeping = true;
            Model m; m.restore(healthy); m.clear_dirty();
            m.tick(40);
            require(m.state.growth_ms == 40 && !m.dirty(), "one animation tick requested a flash save or lost exact progress");
            m.tick(4960);
            require(m.state.growth_ms == 5000 && !m.dirty(), "five seconds of growth requested a flash save");
            healthy.growth_ms = 5u * 60000u - 40;
            m.restore(healthy); m.tick(40);
            require(m.state.growth_ms == 5u * 60000u && m.dirty(), "five-minute growth checkpoint did not request persistence");
            m.clear_dirty(); m.tick(40);
            require(m.state.growth_ms == 5u * 60000u + 40 && !m.dirty(), "growth kept requesting flash saves after its checkpoint");
        }},
        {"care actions and stat changes still request persistence independently of growth", [] {
            State healthy;
            healthy.health = healthy.fullness = healthy.energy = healthy.mood = healthy.cleanliness = 100;
            Model m; m.restore(healthy); m.tick(40);
            require(!m.dirty(), "short growth interval requested persistence");
            require(m.pet(40) && m.state.strokes == 1 && m.dirty(), "growth checkpoint throttled ordinary care action persistence");
            healthy.growth_ms = kGrowthAdultMs;
            m.restore(healthy); m.tick(10u * 60000u);
            require(m.state.growth_ms == kGrowthAdultMs && m.state.fullness < healthy.fullness && m.dirty(), "capped growth suppressed care decay persistence");
        }},
        {"seven and fourteen well-cared-for days evolve once and adulthood is capped", [] {
            Model m; m.state.sleeping = true;
            for (uint32_t hour = 0; hour < 14u * 24u; ++hour) {
                // Fixture supplies hourly meals and hygiene while resting.
                m.state.fullness = m.state.cleanliness = 100;
                m.tick(3600000);
                const auto expected = hour + 1 < 7u * 24u ? GrowthStage::Baby
                    : hour + 1 < 14u * 24u ? GrowthStage::Teen : GrowthStage::Adult;
                require(m.stage() == expected && m.state.growth_ms == (hour + 1) * 3600000u,
                    "normal age crossed the wrong seven/fourteen day boundary");
            }
            m.tick(3600000);
            require(m.state.growth_ms == kGrowthAdultMs && !m.growth_remaining_ms() && !m.growth_paused(), "adult growth continued or reported paused");
        }},
        {"isolated fast growth switches at exactly thirty and one hundred twenty minutes", [] {
            Model m; require(m.set_growth_speed(GrowthSpeed::Fast), "fast setup failed");
            m.state.health = m.state.fullness = m.state.energy = m.state.cleanliness = 100;
            m.tick(kGrowthFastTeenMs - 1);
            require(m.stage() == GrowthStage::Baby && m.growth_remaining_ms() == 1, "teen appeared before its boundary");
            m.tick(1);
            require(m.stage() == GrowthStage::Teen && m.state.growth_ms == kGrowthFastTeenMs, "teen did not appear at thirty minutes");
            m.tick(kGrowthFastAdultMs - kGrowthFastTeenMs - 1);
            require(m.stage() == GrowthStage::Teen && m.growth_remaining_ms() == 1, "adult appeared before its boundary");
            m.tick(1);
            require(m.stage() == GrowthStage::Adult && m.state.growth_ms == kGrowthFastAdultMs, "adult did not appear at two hours");
        }},
        {"illness and unmet care pause growth without erasing it and good sleep resumes it", [] {
            for (int condition = 0; condition < 6; ++condition) {
                State saved; saved.growth_ms = 1234;
                if (condition == 0) saved.sick = true;
                if (condition == 1) saved.health = 49;
                if (condition == 2) saved.fullness = 25;
                if (condition == 3) saved.cleanliness = 30;
                if (condition == 4) saved.poop_count = 3;
                if (condition == 5) saved.energy = 15;
                Model m; m.restore(saved);
                require(m.growth_paused(), "unmet care was not shown as paused");
                m.tick(1000);
                require(m.state.growth_ms == 1234 && m.stage() == GrowthStage::Baby, "paused growth changed the age");
                auto healed = m.state; healed.sick = false; healed.health = 82; healed.fullness = 70;
                healed.cleanliness = 80; healed.poop_count = 0; healed.energy = 65;
                healed.sleeping = true;
                m.restore(healed); m.tick(1000);
                require(m.state.growth_ms == 2234 && !m.growth_paused(), "healthy sleeping did not resume previous growth");
            }
        }},
        {"growth integrates health recovery energy and digestion threshold crossings precisely", [] {
            State healthy; healthy.sleeping = true; healthy.health = 49;
            healthy.fullness = healthy.cleanliness = 100; healthy.energy = 100;
            Model m; m.restore(healthy); m.tick(2u * 3600000u);
            require(m.state.growth_ms == 0, "sub-fifty health counted before recovery");
            m.tick(60000); require(m.state.growth_ms == 60000, "fifty-health boundary did not resume growth");
            healthy.health = 82; healthy.energy = 14; m.restore(healthy);
            m.tick(360000); require(m.state.growth_ms == 180000, "resting energy crossing fifteen lost its partial growth interval");
            healthy.energy = 100; healthy.poop_count = 2; healthy.digestion_ms[0] = 12345;
            m.restore(healthy); m.tick(60000);
            require(m.state.growth_ms == 12345 && m.state.poop_count == 3, "digestion boundary did not pause growth immediately");
        }},
        {"restoring age never reads wall time and corrupt or legacy growth is safe", [] {
            State legacy; Model m; m.restore(legacy);
            require(m.stage() == GrowthStage::Baby && !m.state.growth_ms, "old save became an older pet");
            legacy.growth_ms = kGrowthTeenMs + 54321; legacy.growth_speed = GrowthSpeed::Normal;
            m.restore(legacy); require(m.stage() == GrowthStage::Teen && m.state.growth_ms == legacy.growth_ms, "restoration erased accumulated care time");
            m.restore(m.state); require(m.state.growth_ms == legacy.growth_ms, "reboot added offline or epoch time");
            legacy.growth_ms = UINT32_MAX; legacy.growth_speed = static_cast<GrowthSpeed>(255); m.restore(legacy);
            require(m.state.growth_speed == GrowthSpeed::Normal && m.state.growth_ms == kGrowthAdultMs
                && m.stage() == GrowthStage::Adult, "corrupt growth fields were not safely bounded");
            m.reset(); require(m.stage() == GrowthStage::Baby && m.state.growth_ms == 0, "new life retained old age");
        }},
        {"growth agrees at forty milliseconds and uneven ticks across an evolution boundary", [] {
            State saved; saved.growth_speed = GrowthSpeed::Fast; saved.growth_ms = kGrowthFastTeenMs - 12345;
            saved.sleeping = true; saved.health = saved.fullness = saved.cleanliness = 100; saved.energy = 14;
            Model coarse, fine, irregular; coarse.restore(saved); fine.restore(saved); irregular.restore(saved);
            coarse.tick(600000);
            for (uint32_t passed = 0; passed < 600000; passed += 40) fine.tick(40);
            for (uint32_t passed = 0; passed < 600000;) {
                const auto step = std::min<uint32_t>(600000 - passed, 17 + passed % 977);
                irregular.tick(step); passed += step;
            }
            require(coarse.state.growth_ms == kGrowthFastTeenMs - 12345 + 420000
                && coarse.state.growth_ms == fine.state.growth_ms && coarse.state.growth_ms == irregular.state.growth_ms
                && coarse.stage() == GrowthStage::Teen && fine.stage() == GrowthStage::Teen, "timer cadence changed accumulated growth or form");
        }},
        {"meal commits only after complete tongue animation", [] {
            Model m; const auto before = m.state;
            require(m.feed_start(Item::Cricket, 100), "meal did not start");
            require(!m.feed_finish(1499), "meal committed too early");
            same_stock(before, m.state);
            require(m.feed_finish(1500), "finished meal did not commit");
            require(m.state.inventory[0] == 2 && m.state.fullness == 95 && m.state.meals == 1, "wrong food reward");
            require(!m.feed_finish(1600) && m.state.inventory[0] == 2, "meal committed twice");
        }},
        {"cancelled meal preserves stock and restoration never resumes meal", [] {
            Model m; const auto before = m.state;
            require(m.feed_start(Item::Fly, 0) && m.cancel_feed(), "cancel failed");
            same_stock(before, m.state);
            require(!m.feed_finish(5000), "cancelled meal resumed");
            require(m.feed_start(Item::Fly, 0), "restart failed");
            m.restore(m.state);
            require(!m.feeding() && !m.feed_finish(5000), "restore resumed unfinished meal");
            same_stock(before, m.state);
        }},
        {"food rejects invalid stock full belly and concurrent interactions", [] {
            Model m;
            require(!m.feed_start(Item::Soap, 0) && !m.feed_start(static_cast<Item>(255), 0), "invalid food accepted");
            m.state.fullness = 100;
            require(!m.feed_start(Item::Cricket, 0), "full pet accepted meal");
            m.state.fullness = 10; m.state.inventory[1] = 0;
            require(!m.feed_start(Item::Cockroach, 0), "empty supply accepted");
            require(m.feed_start(Item::Cricket, 0), "valid food failed");
            require(!m.pet(0) && !m.sleep() && !m.start_game(0), "meal allowed concurrent care");
        }},
        {"all four insects preserve desktop nutrition values", [] {
            const int fullness[] = {25, 30, 18, 12}, moods[] = {3, 2, 6, 4};
            for (int i = 0; i < 4; ++i) {
                Model m; m.state.fullness = m.state.mood = 0;
                require(m.feed_start(static_cast<Item>(i), 0) && m.feed_finish(1400), "food could not finish");
                require(m.state.fullness == fullness[i] && m.state.mood == moods[i], "nutrition changed");
            }
        }},
        {"game target validation debounce and once-only settlement", [] {
            Model m; require(m.start_game(0), "game failed");
            require(m.state.energy == 63 && m.game_seconds_left(1) == 20, "wrong game cost or timer");
            require(!m.hit_game(0, 100), "wrong target accepted");
            require(m.hit_game(1, 100), "first hit failed");
            require(!m.hit_game(1, 400), "duplicate target accepted");
            require(!m.hit_game(m.game_target(), 349), "debounce failed");
            require(m.hit_game(m.game_target(), 350), "next valid hit failed");
            require(m.end_game() && !m.end_game(), "game settled twice");
            require(m.state.coins == 34 && m.state.mood == 94 && m.state.inventory[6] == 1, "game reward or toy reuse wrong");
        }},
        {"game timer ends at twenty seconds and reward is bounded", [] {
            Model m; require(m.start_game(0), "game failed");
            for (int i = 0; i < 40; ++i) require(m.hit_game(m.game_target(), 250 * i), "valid hit rejected");
            require(!m.hit_game(m.game_target(), 15000), "forty hit cap failed");
            m.tick(20000);
            require(!m.game_running() && m.state.coins == 110 && m.game_seconds_left(20000) == 0, "timed settlement failed");
            require(!m.hit_game(0, 20001) && !m.end_game(), "finished game accepted new action");
        }},
        {"all three game insects become targets without changing cost or rewards", [] {
            Model m; const auto before = m.state;
            require(m.start_game(0), "game failed");
            unsigned seen = 0;
            int previous = -1;
            for (int i = 0; i < 40; ++i) {
                const int target = m.game_target();
                require(target >= 0 && target < 3 && target != previous, "game target out of range or repeated");
                seen |= 1u << target;
                require(m.hit_game(target, 250u * i), "valid insect hit rejected");
                previous = target;
            }
            require(seen == 7, "one of the three insect buttons can never be a target");
            require(!m.hit_game(m.game_target(), 10000), "hit cap changed");
            m.tick(kGameDurationMs);
            require(!m.game_running() && m.state.coins == before.coins + 80
                && m.state.energy == before.energy - kGameEnergyCost, "game settlement or energy cost changed");
            for (int i = 0; i < kItemCount; ++i)
                require(m.state.inventory[i] == before.inventory[i], "game consumed an insect or toy");
            require(!m.end_game(), "game rewards paid twice");
        }},
        {"three non-ball games play without a toy and preserve care nutrition and supplies", [] {
            const char *games[] = {"capture", "leaf", "mimic"};
            for (const auto game : games) {
                (void)game;
                Model m; m.state.inventory[static_cast<unsigned>(Item::Ball)] = 0;
                m.sync_day(20261005); const State before = m.state;
                require(m.start_game(0, false) && m.award_game_hit(250), "non-ball game required a toy or refused a hit");
                m.tick(kGameDurationMs);
                require(!m.game_running() && m.state.coins == before.coins + 2 && m.state.energy == before.energy - kGameEnergyCost
                    && m.state.mood == before.mood + 2 && m.state.affinity == 2
                    && m.state.care_total == before.care_total + 1 && m.state.daily_mask == DailyPlay,
                    "non-ball game did not use the existing shared care settlement");
                require(m.state.fullness == before.fullness && m.state.meals == before.meals
                    && !m.state.digestion_ms[0] && !m.state.digestion_ms[1] && !m.state.digestion_ms[2]
                    && m.state.growth_ms == kGameDurationMs, "play awarded food digestion or accelerated growth");
                for (int i = 0; i < kItemCount; ++i)
                    require(m.state.inventory[i] == before.inventory[i], "play consumed a food or toy");
                const State settled = m.state;
                require(!m.end_game() && !m.award_game_hit(kGameDurationMs + 1), "finished non-ball game accepted another settlement");
                require(m.state.coins == settled.coins && m.state.affinity == settled.affinity
                    && m.state.care_total == settled.care_total, "finished non-ball game duplicated rewards");
            }
            Model ball; ball.state.inventory[static_cast<unsigned>(Item::Ball)] = 0;
            const State before = ball.state;
            require(!ball.start_game(0, true) && !ball.start_game(0) && !ball.game_running()
                && ball.state.energy == before.energy && ball.state.coins == before.coins,
                "ball game started without a ball or charged for refusal");
        }},
        {"all shared games retain sleep sickness hunger energy and feeding gates", [] {
            for (const bool needs_ball : {false, true}) {
                for (int condition = 0; condition < 4; ++condition) {
                    Model m;
                    if (condition == 0) m.state.sleeping = true;
                    if (condition == 1) m.state.sick = true;
                    if (condition == 2) m.state.fullness = 10;
                    if (condition == 3) m.state.energy = 9;
                    const State before = m.state;
                    require(!m.start_game(0, needs_ball) && !m.game_running() && m.state.energy == before.energy
                        && m.state.coins == before.coins, "new game bypassed a care gate or charged a refused start");
                }
                Model feeding; require(feeding.feed_start(Item::Fly, 0), "feeding fixture failed");
                require(!feeding.start_game(1, needs_ball) && !feeding.game_running(), "new game interrupted a meal");
            }
        }},
        {"shared verified hits enforce monotonic debounce a bounded score and expiry", [] {
            Model m; require(m.start_game(1000, false), "shared hit fixture failed");
            require(!m.award_game_hit(999) && m.award_game_hit(1000), "shared hit accepted time before game start");
            require(!m.award_game_hit(1249) && m.award_game_hit(1250) && !m.award_game_hit(1249)
                && m.game_hits() == 2, "shared hit debounce or backwards time changed score");
            for (int i = 2; i < 40; ++i)
                require(m.award_game_hit(1000 + 250u * i), "valid shared hit was rejected");
            require(!m.award_game_hit(11000) && m.game_hits() == 40, "shared score exceeded forty hits");
            m.tick(kGameDurationMs);
            require(m.state.coins == 110 && m.state.mood == 100 && m.state.affinity == 2
                && !m.award_game_hit(21000) && !m.end_game(), "shared bounded score settled incorrectly");
            Model deadline; require(deadline.start_game(1000, false), "expiry fixture failed");
            require(deadline.award_game_hit(20999) && !deadline.award_game_hit(21000)
                && deadline.game_hits() == 1, "shared hit accepted the exact deadline");
        }},
        {"shared early exits award only earned care and cannot be settled repeatedly", [] {
            Model empty; empty.sync_day(20261005); const State before = empty.state;
            require(empty.start_game(1000, false) && empty.end_game(), "empty shared game could not exit");
            require(empty.state.energy == before.energy - kGameEnergyCost && empty.state.coins == before.coins
                && empty.state.mood == before.mood && !empty.state.affinity && !empty.state.daily_mask
                && empty.state.care_total == before.care_total && !empty.end_game(), "empty exit manufactured care or refunded energy");
            Model hit; hit.sync_day(20261005);
            require(hit.start_game(1000, false) && hit.award_game_hit(1250) && hit.end_game(), "scored shared game could not exit");
            require(hit.state.coins == 32 && hit.state.mood == 92 && !hit.state.affinity
                && hit.state.daily_mask == DailyPlay && hit.state.care_total == 1, "early shared exit changed existing rewards or farmed bond");
            const State settled = hit.state;
            require(!hit.end_game() && !hit.award_game_hit(1500), "exited shared game remained active");
            hit.tick(kGameDurationMs);
            require(hit.state.coins == settled.coins && hit.state.affinity == settled.affinity
                && hit.state.care_total == settled.care_total, "timer rewarded an already exited shared game");
        }},
        {"shop preserves currency and inventory caps", [] {
            Model m; require(m.buy(Item::Cricket), "purchase failed");
            require(m.state.coins == 24 && m.state.inventory[0] == 4, "wrong shop price");
            m.state.inventory[0] = 99; const auto before = m.state;
            require(!m.buy(Item::Cricket) && !m.buy(static_cast<Item>(200)), "invalid shop purchase accepted");
            same_stock(before, m.state);
            m.state.coins = 0; require(!m.buy(Item::Fly), "unfunded purchase accepted");
        }},
        {"medicine bath and walking consume correct resources", [] {
            Model m;
            require(!m.medicine(0) && m.state.inventory[4] == 1, "healthy pet consumed medicine");
            m.state.health = 39;
            require(m.medicine(0) && m.state.health == 59 && m.state.inventory[4] == 0 && m.state.sick, "medicine failed");
            require(!m.medicine(0), "duplicate treatment accepted");
            require(m.clean(0) && m.state.cleanliness == 100 && m.state.inventory[5] == 1, "bath failed");
            require(!m.clean(0), "already clean bath accepted");
            m.state.energy = 1; require(!m.walk(0), "exhausted walk accepted");
            m.state.energy = 10; require(m.walk(0) && m.state.energy == 8, "walking cost failed");
            require(m.shake(0) && m.state.energy == 6 && m.reaction_pose() == Pose::Dizzy, "shake did not become dizzy");
        }},
        {"restoration clamps corrupt values and valid UTF-8 label", [] {
            Model m; State bad;
            bad.health = -100; bad.energy = 999; bad.coins = std::numeric_limits<int>::max();
            bad.inventory[0] = -1; bad.inventory[6] = 300; bad.brightness = -8; bad.idle_minutes = 500;
            bad.scene = static_cast<Scene>(255); bad.awake_scene = static_cast<Scene>(255);
            bad.sleeping = true; bad.sleep_reason = static_cast<SleepReason>(255);
            std::memset(bad.name, 0xff, sizeof(bad.name)); bad.meals = UINT32_MAX;
            m.restore(bad);
            require(m.state.health == 0 && m.state.energy == 100 && m.state.coins == 999999 && m.state.inventory[0] == 0 && m.state.inventory[6] == 99, "restore bounds failed");
            require(m.state.brightness == 10 && m.state.idle_minutes == 60 && m.state.scene == Scene::Forest && m.state.awake_scene == Scene::Forest, "restore settings failed");
            require(m.state.sleep_reason == SleepReason::Manual && std::strcmp(m.state.name, "卷卷") == 0 && m.state.meals == 1000000 && !m.dirty(), "restore label/counter failed");
        }},
        {"one unattended waking hour remains healthy with hourly needs", [] {
            Model m; m.tick(3600000);
            require(m.state.health == 82 && m.state.fullness == 64 && m.state.energy == 63
                && m.state.mood == 87 && m.state.cleanliness == 77, "hourly care rates changed");
            require(!m.state.sick && !m.needs(), "one ordinary unattended hour caused illness or a care warning");
        }},
        {"two waking hours cost five energy and fifteen minutes of sleep restore them", [] {
            Model m; m.tick(2u * 3600000u);
            require(m.state.energy == 60 && !m.state.sick && !m.growth_paused(),
                "ordinary care depleted energy too quickly or paused growth");
            require(m.sleep(), "energy recovery fixture could not rest");
            m.tick(15u * 60000u);
            require(m.state.energy == 65 && m.state.sleeping && !m.state.sick,
                "slower waking drain also slowed the established sleep recovery");
        }},
        {"slower waking energy retains fractions at lit dark and irregular timer cadence", [] {
            State saved; saved.energy = 100; saved.fullness = saved.cleanliness = 100;
            Model coarse, lit, dark, irregular;
            coarse.restore(saved); lit.restore(saved); dark.restore(saved); irregular.restore(saved);
            constexpr uint32_t duration = 4u * 3600000u;
            coarse.tick(duration);
            for (uint32_t passed = 0; passed < duration; passed += 40) lit.tick(40);
            for (uint32_t passed = 0; passed < duration; passed += 1000) dark.tick(1000);
            for (uint32_t passed = 0; passed < duration;) {
                const uint32_t step = std::min<uint32_t>(duration - passed, 17 + passed % 977);
                irregular.tick(step); passed += step;
            }
            for (const Model *model : {&coarse, &lit, &dark, &irregular}) {
                require(model->state.energy == 90 && model->state.growth_ms == duration
                    && model->state.companion_ms == duration && !model->growth_paused(),
                    "screen timing changed slower energy consumption or powered-on growth");
            }
        }},
        {"slower waking drain pauses growth exactly at the existing fifteen energy threshold", [] {
            State saved; saved.energy = 16; saved.fullness = saved.cleanliness = 100;
            Model coarse, fine; coarse.restore(saved); fine.restore(saved);
            coarse.tick(2u * 3600000u);
            for (int i = 0; i < 7200; ++i) fine.tick(1000);
            for (const Model *model : {&coarse, &fine})
                require(model->state.energy == 11 && model->state.growth_ms == 24u * 60000u
                    && model->growth_paused() && !model->state.sick,
                    "energy rate changed the growth gate or threshold-crossing integration");
        }},
        {"repeated twenty-second games charge two energy each without a second settlement charge", [] {
            Model m; const State original = m.state;
            for (uint64_t round = 0; round < 5; ++round) {
                const uint64_t now = round * kGameDurationMs;
                require(m.start_game(now, false) && m.award_game_hit(now), "repeated game fixture failed");
                m.tick(kGameDurationMs);
                require(!m.game_running() && !m.end_game(), "completed game charged or settled twice");
            }
            require(m.state.energy == 55 && m.state.coins == original.coins + 10
                && m.state.affinity == 10 && m.state.care_total == original.care_total + 5,
                "five short games exhausted energy or changed shared rewards");
            require(m.sleep(), "post-game recovery could not start");
            m.tick(30u * 60000u);
            require(m.state.energy == original.energy, "normal rest did not recover repeated-game energy");
        }},
        {"eight hours of sleep conserve needs and fully restore energy", [] {
            Model m; require(m.sleep(), "sleep failed");
            m.tick(6u * 3600000u); m.tick(2u * 3600000u);
            require(m.state.health == 86 && m.state.fullness == 54 && m.state.energy == 100
                && m.state.mood == 98 && m.state.cleanliness == 72, "nighttime care rates changed");
            require(!m.state.sick && m.state.sleeping, "overnight rest caused illness or hunger");
            for (const auto exposure : m.state.neglect_ms) require(!exposure, "overnight rest retained neglect");
        }},
        {"health penalties cannot bypass the sustained hunger grace", [] {
            Model m; m.state.fullness = 0; m.state.cleanliness = 0; m.state.poop_count = 3;
            m.tick(60u * 60000u - 1);
            require(!m.state.sick && m.state.health >= 79, "health decay bypassed the one-hour hunger warning period");
            State saved; saved.sick = true; saved.sickness_cause = SicknessCause::Dirt;
            saved.neglect_ms[0] = 180000; saved.neglect_ms[1] = 120000; saved.neglect_ms[2] = 240000;
            m.restore(saved);
            require(m.state.sick && m.state.sickness_cause == SicknessCause::Dirt
                && m.state.neglect_ms[0] == 180000 && m.state.neglect_ms[1] == 120000
                && m.state.neglect_ms[2] == 240000, "new care rules erased previous illness or saved exposure");
        }},
        {"stat decay keeps fractions at fine timer cadence", [] {
            Model m;
            for (int i = 0; i < 22500; ++i) m.tick(40); // Fifteen real minutes.
            require(m.state.fullness == 69 && m.state.energy == 64 && m.state.cleanliness == 79, "small timer ticks lost fractional care");
        }},
        {"large tick matches fine care across healing and hungry threshold", [] {
            Model coarse, fine; State original;
            original.health = 95; original.fullness = 16; original.cleanliness = 21;
            original.energy = 10; original.sleeping = true; original.sleep_reason = SleepReason::Manual;
            coarse.restore(original); fine.restore(original);
            coarse.tick(80u * 60000u);
            for (int i = 0; i < 4800; ++i) fine.tick(1000);
            require(coarse.state.health == fine.state.health && coarse.state.health == 94, "healing/hunger integration diverged");
            require(coarse.state.fullness == fine.state.fullness && coarse.state.energy == fine.state.energy && coarse.state.cleanliness == fine.state.cleanliness, "large care interval diverged");
        }},
        {"manual sleep is preserved by dawn and autonomy disabling", [] {
            Model m; m.state.scene = Scene::Meadow;
            require(m.sleep(), "manual sleep failed");
            require(m.state.scene == Scene::Meadow && m.state.sleep_reason == SleepReason::Manual, "manual sleep changed selected scene");
            life(m, 12000, 30, Weather::Rain, 8);
            require(m.state.sleeping && !m.feeding(), "morning or tilt woke manual sleep");
            m.state.auto_life = false; life(m, 13000);
            require(m.state.sleeping, "autonomy off woke manual sleep");
            require(m.wake() && m.state.scene == Scene::Meadow && m.state.sleep_reason == SleepReason::None, "manual waking failed");
        }},
        {"calendar scene pairing leaves unknown clock and manual choice honest", [] {
            State s;
            require(effective_scene(s, -1) == Scene::Forest && effective_scene(s, 22) == Scene::Night && effective_scene(s, 7) == Scene::Forest, "forest clock pairing failed");
            s.scene = Scene::Meadow; require(effective_scene(s, 22) == Scene::MeadowNight, "meadow pairing failed");
            s.scene = Scene::Clearing; require(effective_scene(s, 22) == Scene::Clearing, "clearing moved to branch");
            s.auto_world = false; require(effective_scene(s, 22) == Scene::Clearing, "manual scene ignored");
            require(phase_for_hour(6) == Phase::Dawn && phase_for_hour(8) == Phase::Day && phase_for_hour(17) == Phase::Dusk && phase_for_hour(20) == Phase::Night, "phase edges failed");
        }},
        {"routine sleep needs synchronised clock then morning wakes", [] {
            Model m;
            life(m, 12000, 0, Weather::Clear, -1);
            require(!m.state.sleeping, "unknown clock scheduled night sleep");
            life(m, 30000, 0, Weather::Clear, 22);
            require(m.state.sleeping && m.state.sleep_reason == SleepReason::Routine, "night routine failed");
            life(m, 30001, 0, Weather::Clear, 22);
            life(m, 40000, 0, Weather::Clear, 7);
            require(!m.state.sleeping, "morning did not wake routine");
        }},
        {"hidden scenes keep physiological naps and match visible care over four hours", [] {
            State initial; initial.energy=17; initial.fullness=90; initial.cleanliness=90;
            Model visible, hidden; visible.restore(initial); hidden.restore(initial);
            for(uint64_t now=1000;now<=4ull*3600000;now+=1000){
                visible.tick(1000);hidden.tick(1000);
                visible.life_step(now,12,Weather::Clear,0,Scene::Forest,true);
                const auto output=hidden.life_step(now,12,Weather::Clear,0,Scene::Forest,false);
                require(!output.has_action,"a hidden page animated autonomous wandering");
            }
            require(!hidden.state.sick&&hidden.state.energy==visible.state.energy&&hidden.state.sleeping==visible.state.sleeping,
                "a menu/standby page disabled automatic rest and caused exhaustion");
            require(hidden.state.growth_ms==visible.state.growth_ms&&hidden.state.companion_ms==visible.state.companion_ms,
                "hidden rest changed growth or companionship");
        }},
        {"hidden scenes wake at dawn and still defer rest for games and manual activity", [] {
            Model routine;
            routine.life_step(12000,22,Weather::Clear,0,Scene::Forest,false);
            require(routine.state.sleeping&&routine.state.sleep_reason==SleepReason::Routine,"hidden nighttime routine did not start");
            routine.life_step(13000,7,Weather::Clear,0,Scene::Forest,false);
            require(!routine.state.sleeping,"a hidden menu blocked the morning wake");
            require(routine.sleep(),"manual sleep fixture failed");
            routine.life_step(14000,8,Weather::Clear,0,Scene::Forest,false);
            require(routine.state.sleeping&&routine.state.sleep_reason==SleepReason::Manual,"hidden morning interrupted manual sleep");
            Model busy;busy.state.energy=17;
            busy.life_step(12000,22,Weather::Clear,0,Scene::Forest,false,0,true);
            require(!busy.state.sleeping,"manual interaction did not defer hidden rest");
            require(busy.start_game(13000),"game fixture failed");
            busy.life_step(14000,22,Weather::Clear,0,Scene::Forest,false);
            require(!busy.state.sleeping&&busy.game_running(),"hidden rest interrupted a game");
            Model disabled;disabled.state.auto_life=false;disabled.state.energy=17;
            disabled.life_step(12000,22,Weather::Clear,0,Scene::Forest,false);
            require(!disabled.state.sleeping,"disabled autonomous life started hidden rest");
        }},
        {"night manual waking gets thirty minute grace", [] {
            Model m;
            life(m, 12000, 0, Weather::Clear, 22);
            require(m.state.sleeping && m.wake(), "night sleep/wake failed");
            life(m, 16000, 0, Weather::Clear, 22);
            require(!m.state.sleeping, "just-woken pet immediately slept");
            life(m, 1811999, 0, Weather::Clear, 22);
            require(!m.state.sleeping, "wake grace ended early");
            life(m, 1816001, 0, Weather::Clear, 22);
            require(m.state.sleeping && m.state.sleep_reason == SleepReason::Routine, "routine failed after grace");
        }},
        {"nap stays in daytime scene recovers and avoids repeated naps", [] {
            Model m; m.state.scene = Scene::Clearing; m.state.energy = 17;
            life(m, 12000);
            require(m.state.sleeping && m.state.sleep_reason == SleepReason::Nap && m.state.scene == Scene::Clearing, "nap forged night scene");
            life(m, 12001); m.tick(53u * 60000u);
            life(m, 3192001);
            require(m.state.sleeping, "rounded energy woke nap before thirty-five actual energy");
            m.tick(60000);
            life(m, 3252001);
            require(!m.state.sleeping && m.state.energy == 35 && m.state.scene == Scene::Clearing, "rested nap did not wake");
            m.state.energy = 17; life(m, 3252002);
            life(m, 5052001);
            require(!m.state.sleeping, "nap grace failed");
            life(m, 5052002);
            require(m.state.sleeping && m.state.sleep_reason == SleepReason::Nap, "nap failed after thirty-minute grace");
        }},
        {"nap duration limit and disabling own sleep wake once", [] {
            Model m; m.state.energy = 10; life(m, 12000); life(m, 12001);
            life(m, 3612000); require(m.state.sleeping, "maximum nap duration ended before one hour");
            life(m, 3612001);
            require(!m.state.sleeping, "maximum nap duration did not wake");
            Model other; life(other, 12000, 0, Weather::Clear, 22);
            other.state.auto_life = false; life(other, 13000, 0, Weather::Clear, 22);
            require(!other.state.sleeping, "autonomy off did not release own sleep");
        }},
        {"left and right tilt do not trigger or alter autonomous actions", [] {
            Model tilted, level;
            for (uint64_t now = 0; now < 180000; now += 200) {
                const auto a = life(tilted, now, now % 400 ? -55 : 55);
                const auto b = life(level, now, 0);
                require(!a.gripping && a.pose != Pose::Grip, "removed tilt feature still gripped");
                require(a.pose == b.pose && a.start == b.start && a.x == b.x, "tilt altered life progression");
            }
        }},
        {"scene selection is locked across sleep wake and restore", [] {
            Model m;
            require(m.select_scene(Scene::Meadow), "scene selection failed");
            require(m.state.scene_locked && !m.state.auto_world && effective_scene(m.state, 22) == Scene::Meadow, "manual scene not locked");
            require(m.sleep() && m.state.scene == Scene::Meadow, "sleep moved selected scene");
            require(m.select_scene(Scene::Clearing) && m.state.sleeping && m.state.scene == Scene::Clearing, "sleeping scene selection failed");
            m.restore(m.state); require(m.state.scene == Scene::Clearing && m.state.sleeping, "restore changed sleeping scene");
            require(m.wake() && m.state.scene == Scene::Clearing, "wake restored stale scene");
            m.set_world_auto(true); require(!m.state.scene_locked && m.state.auto_world, "automatic day/night did not unlock");
            require(m.select_scene(Scene::Forest), "forest select failed"); m.set_world_auto(true);
            require(effective_scene(m.state, 22) == Scene::Night && m.state.scene == Scene::Forest, "auto mapping mutated stored scene");
            require(!m.select_scene(static_cast<Scene>(255)), "invalid scene accepted");
        }},
        {"ground busy sickness and manual sleep win over tilt", [] {
            Model m;
            require(!life(m, 0, 40, Weather::Clear, 12, false, Scene::Meadow).gripping, "ground scene gripped branch");
            require(!life(m, 1, 40, Weather::Rain, 12, true).has_action, "manual activity was interrupted");
            m.state.health = 39; require(!life(m, 2, 40).has_action, "sick pet autonomously gripped");
            m.state.health = 82; require(m.feed_start(Item::Fly, 3), "meal failed");
            require(!life(m, 4, 40, Weather::Rain).has_action, "tilt interrupted tongue meal");
            m.cancel_feed(); m.sleep(); require(!life(m, 5, 40).has_action, "tilt interrupted manual sleep");
        }},
        {"rain shelter remains stable when tilted", [] {
            Model m;
            const auto rain = life(m, 1, 0, Weather::Rain);
            require(rain.has_action && rain.pose == Pose::Shelter, "rain did not shelter");
            require(life(m, 4000, 0, Weather::Storm).start == rain.start, "continuous rain restarted pose");
            require(life(m, 4001, 20, Weather::Storm).pose == Pose::Shelter, "tilt interrupted shelter");
            life(m, 4100, 0, Weather::Rain);
            require(life(m, 4800, 0, Weather::Rain).pose == Pose::Shelter, "released grip failed to resume shelter");
            require(!life(m, 4801, 0, Weather::Unknown).has_action, "unknown weather kept rain pose");
        }},
        {"autonomy preserves inventory and walk offsets stay bounded", [] {
            Model m; const auto before = m.state; int walks = 0;
            for (uint64_t now = 0; now <= 600000; now += 200) {
                const auto output = life(m, now);
                require(output.x >= -24 && output.x <= 24 && std::isfinite(output.x), "walk position escaped scene");
                if (output.has_action && output.pose == Pose::Walk) ++walks;
            }
            require(walks > 0, "autonomy never walked"); same_stock(before, m.state);
        }},
        {"familiar pets wave more often while bond below fifty preserves the old action sequence", [] {
            int low_waves = 0, familiar_waves = 0;
            for (uint32_t seed = 1; seed <= 24; ++seed) {
                Model low, threshold, familiar;
                threshold.state.affinity = 49; familiar.state.affinity = 50;
                for (auto *m : {&low, &threshold, &familiar}) { m->set_seed(seed); m->reset_life(); }
                uint64_t low_start = UINT64_MAX, familiar_start = UINT64_MAX;
                for (uint64_t now = 0; now <= 1200000; now += 200) {
                    const auto a = life(low, now), b = life(threshold, now), c = life(familiar, now);
                    require(a.pose == b.pose && a.start == b.start && a.has_action == b.has_action
                        && a.x == b.x && std::strcmp(a.note, b.note) == 0,
                        "bond below fifty changed the original seeded autonomous sequence");
                    if (a.has_action && a.pose == Pose::Wave && a.start != low_start) {
                        ++low_waves; low_start = a.start;
                        require(std::strcmp(a.note, "向你挥挥小爪子") == 0, "new pet used the familiar greeting");
                    }
                    if (c.has_action && c.pose == Pose::Wave && c.start != familiar_start) {
                        ++familiar_waves; familiar_start = c.start;
                        require(std::strcmp(c.note, "熟悉你后，挥挥小爪子") == 0, "familiar greeting is not observable");
                    }
                }
                same_stock(State{}, low.state); same_stock(State{}, familiar.state);
            }
            require(low_waves > 100 && familiar_waves * 4 > low_waves * 5,
                "familiar bond did not meaningfully increase greeting frequency across fixed seeds");
        }},
        {"very close pets visibly approach the centre without leaving the branch or grass", [] {
            for (const auto scene : {Scene::Forest, Scene::Meadow}) {
                Model close; close.state.affinity = 80; close.set_seed(314159); close.reset_life();
                bool approached = false;
                for (uint64_t now = 0; now <= 1200000; now += 100) {
                    const auto output = life(close, now, 0, Weather::Cloudy, 12, false, scene);
                    require(std::isfinite(output.x) && output.x >= -24 && output.x <= 24,
                        "familiar approach escaped the existing walk limits");
                    if (output.has_action && output.pose == Pose::Walk && std::strstr(output.note, "想靠近你")) {
                        const float start = output.x;
                        require(std::abs(start) >= 4, "approach started at the centre already");
                        const auto middle = life(close, output.start + output.duration / 2, 0, Weather::Cloudy, 12, false, scene);
                        const auto end = life(close, output.start + output.duration, 0, Weather::Cloudy, 12, false, scene);
                        require(std::abs(middle.x) <= std::abs(start) / 2 + 1e-5 && std::abs(end.x) < 1e-5,
                            "approach label did not describe a real centre-directed trajectory");
                        approached = true; break;
                    }
                }
                require(approached, "very close pet never approached its companion across the seeded session");
                Model familiar; familiar.state.affinity = 79; familiar.set_seed(314159); familiar.reset_life();
                for (uint64_t now = 0; now <= 240000; now += 200)
                    require(!std::strstr(life(familiar, now, 0, Weather::Cloudy, 12, false, scene).note, "想靠近你"),
                        "centre approach unlocked below eighty bond");
            }
        }},
        {"high bond greetings preserve rain sleep sickness feeding and manual activity priority", [] {
            Model rain; rain.state.affinity = 100;
            require(life(rain, 12000, 0, Weather::Rain).pose == Pose::Shelter
                && life(rain, 24000, 0, Weather::Storm).pose == Pose::Shelter, "greeting interrupted leaf shelter");
            Model sleeping; sleeping.state.affinity = 100; sleeping.sleep();
            require(!life(sleeping, 12000).has_action && sleeping.state.sleeping, "greeting woke manually sleeping pet");
            Model night; night.state.affinity = 100; life(night, 12000, 0, Weather::Clear, 22);
            require(night.state.sleeping, "bond greeting overrode scheduled sleep");
            Model sick; sick.state.affinity = 100; sick.state.sick = true;
            require(!life(sick, 12000).has_action, "ill pet greeted instead of resting");
            Model hungry; hungry.state.affinity = 100; hungry.state.fullness = 10;
            require(!life(hungry, 12000).has_action, "starving pet approached instead of requesting care");
            Model feeding; feeding.state.affinity = 100; require(feeding.feed_start(Item::Fly, 0), "feeding fixture failed");
            require(!life(feeding, 12000).has_action, "greeting interrupted tongue animation");
            Model manual; manual.state.affinity = 100;
            require(!life(manual, 12000, 0, Weather::Clear, 12, true).has_action, "greeting interrupted manual activity");
            Model low, high; high.state.affinity = 100;
            low.set_seed(777); low.reset_life(); high.set_seed(777); high.reset_life();
            for (uint64_t now = 0; now <= 240000; now += 200) {
                const auto a = life(low, now, 0, Weather::Fog), b = life(high, now, 0, Weather::Fog);
                require(a.pose == b.pose && a.start == b.start && a.x == b.x && std::strcmp(a.note, b.note) == 0,
                    "special friendly behaviour leaked outside clear/cloudy weather");
            }
        }},
        {"manual activity freezes an autonomous walk without snapping to centre", [] {
            Model m; bool found = false;
            for (uint64_t now = 12000; now < 240000; now += 200) {
                const auto output = life(m, now);
                if (output.has_action && output.pose == Pose::Walk) {
                    const auto stopped = life(m, now + 1200, 0, Weather::Clear, 12, true);
                    const auto held = life(m, now + 20000, 0, Weather::Clear, 12, true);
                    require(!held.has_action && held.x == stopped.x, "busy pet kept walking");
                    require(std::abs(stopped.x) > 0, "walk cancelled by snapping to centre");
                    found = true; break;
                }
            }
            require(found, "test never reached walking");
        }},
        {"nonfinite tilt and extreme times remain safe", [] {
            Model m;
            require(!life(m, 1, std::numeric_limits<float>::quiet_NaN()).gripping, "NaN tilt became grip");
            require(!life(m, 2, std::numeric_limits<float>::infinity()).gripping, "infinite tilt became grip");
            life(m, UINT64_MAX - 10, 20);
            require(!life(m, UINT64_MAX, 20).gripping, "large monotonic time enabled removed grip");
            m.tick(UINT32_MAX);
            require(m.state.health >= 0 && m.state.health <= 100, "large tick exceeded care bounds");
        }},
        {"hunger and severe hunger expose concrete care reminders", [] {
            Model m; m.state.fullness = 26;
            require(!m.hungry() && !(m.needs() & NeedFood), "full pet requested food");
            m.state.fullness = 25; require(m.hungry() && !m.starving() && (m.needs() & NeedFood), "hunger threshold missing");
            m.state.fullness = 10; require(m.starving(), "severe hunger threshold missing");
            m.state.energy = 20; m.state.cleanliness = 30; m.state.mood = 35;
            require((m.needs() & (NeedRest | NeedClean | NeedCompany)) == (NeedRest | NeedClean | NeedCompany), "parallel needs were lost");
        }},
        {"severe hunger blocks games without charging energy until a meal is eaten", [] {
            Model m; m.state.fullness = 10;
            const auto before = m.state;
            require(!m.start_game(0) && !m.game_running(), "severely hungry pet started a game");
            require(m.state.energy == before.energy && m.state.mood == before.mood, "rejected game consumed care stats");
            same_stock(before, m.state);
            require(m.feed_start(Item::Fly, 0) && m.feed_finish(kFeedDurationMs), "hungry pet could not eat");
            require(m.start_game(2000) && m.state.energy == before.energy - kGameEnergyCost, "fed pet could not play or was charged twice");
            Model boundary; boundary.state.fullness = 11;
            require(boundary.start_game(0), "non-severe hunger incorrectly blocked game");
        }},
        {"neglected hunger illness persists independently of health", [] {
            Model m; m.state.fullness = 10;
            m.tick(60u * 60000u - 1); require(!m.state.sick, "hunger illness arrived early");
            m.tick(1);
            require(m.state.sick && m.state.sickness_cause == SicknessCause::Hunger && m.state.health > 40, "hunger failed to cause independent illness");
            m.state.fullness = 90; m.tick(1);
            require(m.state.sick && (m.needs() & NeedMedicine), "feeding silently cured illness");
            const State saved = m.state; m.restore(saved);
            require(m.state.sick && m.state.sickness_cause == SicknessCause::Hunger, "illness not persisted");
            require(!m.start_game(0), "sick pet started strenuous game");
        }},
        {"three illness causes use distinct sustained neglect delays", [] {
            Model dirt; dirt.state.cleanliness = 20;
            dirt.tick(90u * 60000u - 1); require(!dirt.state.sick, "dirt illness arrived early");
            dirt.tick(1); require(dirt.state.sick && dirt.state.sickness_cause == SicknessCause::Dirt, "dirty illness did not start");
            Model tired; tired.state.energy = 10;
            tired.tick(120u * 60000u - 1); require(!tired.state.sick, "fatigue illness arrived early");
            tired.tick(1); require(tired.state.sick && tired.state.sickness_cause == SicknessCause::Exhaustion, "fatigue illness did not start");
            Model weak; weak.state.health = 39; weak.tick(0);
            require(weak.state.sick && weak.state.sickness_cause == SicknessCause::Weakness, "weak health did not become illness");
        }},
        {"addressing a need before its deadline resets continuous neglect", [] {
            Model m; m.state.fullness = 10; m.tick(120000);
            m.state.fullness = 50; m.tick(1000); require(m.state.neglect_ms[0] == 0 && !m.state.sick, "hunger exposure did not reset");
            m.state.fullness = 10; m.tick(60u * 60000u - 1); require(!m.state.sick, "non-continuous hunger was summed");
            m.tick(1); require(m.state.sick, "new sustained hunger never caused illness");
            Model nap; nap.state.energy = 9; nap.tick(115u * 60000u);
            require(nap.sleep(), "rest failed"); nap.tick(1);
            require(!nap.state.sick && nap.state.neglect_ms[2] == 0, "rest did not avert fatigue illness");
        }},
        {"real feeding bathing and cleanup interrupt their warning timers", [] {
            Model hungry; hungry.state.fullness = 10; hungry.tick(55u * 60000u);
            require(hungry.feed_start(Item::Cricket, 55u * 60000u)
                && hungry.feed_finish(55u * 60000u + kFeedDurationMs), "warning meal failed");
            hungry.tick(1); require(!hungry.state.sick && !hungry.state.neglect_ms[0], "meal did not reset hunger exposure");
            Model dirty; dirty.state.cleanliness = 20; dirty.tick(85u * 60000u);
            require(dirty.clean(85u * 60000u), "warning bath failed");
            dirty.tick(1); require(!dirty.state.sick && !dirty.state.neglect_ms[1], "bath did not reset dirt exposure");
            Model waste; waste.state.poop_count = 3; waste.tick(85u * 60000u);
            require(waste.clear_poop(85u * 60000u), "warning cleanup failed");
            require(!waste.state.sick && !waste.state.neglect_ms[1], "cleanup did not reset waste exposure");
        }},
        {"medicine recovery requires food cleanliness and rest", [] {
            Model m; m.state.health = 5; m.state.fullness = 10;
            require(m.medicine(0), "weak pet rejected medicine");
            require(m.state.sick && m.state.health == 25 && m.state.treatment_ms == kRecoveryMs, "treatment skipped recovery");
            const int stock = m.state.inventory[4];
            require(!m.medicine(0) && m.state.inventory[4] == stock, "medicine repeated during treatment");
            m.tick(60000); require(m.state.sick && m.state.treatment_ms == kRecoveryMs, "hungry recovery progressed");
            m.state.fullness = 80; m.state.cleanliness = 20; m.tick(60000);
            require(m.state.treatment_ms == kRecoveryMs, "dirty recovery progressed");
            m.state.cleanliness = 80; m.state.energy = 10; m.tick(60000);
            require(m.state.treatment_ms == kRecoveryMs, "exhausted recovery progressed");
            m.state.energy = 50; m.tick(29999); require(m.state.sick && m.state.treatment_ms == 1, "recovery arrived early");
            m.tick(1); require(!m.state.sick && m.state.sickness_cause == SicknessCause::None && m.state.health >= 45, "valid care did not cure very low health");
            m.tick(1000); require(!m.state.sick, "cured low-health pet immediately relapsed");
        }},
        {"successful meals produce delayed bounded waste and cancelling produces none", [] {
            Model m; require(m.feed_start(Item::Fly, 0) && m.cancel_feed(), "cancel failed");
            m.tick(kDigestionMs); require(!m.state.poop_count, "cancelled meal produced waste");
            require(m.feed_start(Item::Fly, kDigestionMs) && m.feed_finish(kDigestionMs + kFeedDurationMs), "meal failed");
            require(!m.state.poop_count && m.state.digestion_ms[0] == kDigestionMs, "meal produced immediate waste");
            m.tick(kDigestionMs - 1); require(!m.state.poop_count, "digestion arrived early");
            m.tick(1); require(m.state.poop_count == 1 && m.state.digestion_ms[0] == 0, "digestion did not produce one waste");
            m.state.fullness = 0;
            for (int i = 0; i < 3; ++i) require(m.feed_start(Item::Cricket, 300000 + i * 2000) && m.feed_finish(301400 + i * 2000), "additional meal failed");
            int pending = 0; for (const auto timer : m.state.digestion_ms) if (timer) ++pending;
            require(pending == 2, "digestion queue exceeded remaining waste capacity");
            m.tick(kDigestionMs); require(m.state.poop_count == 3, "waste cap failed");
        }},
        {"cleaning waste is free separate from soap bath and works during sleep", [] {
            Model m; const int soap = m.state.inventory[5], coins = m.state.coins;
            require(!m.clear_poop(0), "empty habitat cleanup rewarded care");
            m.state.poop_count = 2; require(m.sleep(), "sleep failed");
            require(m.clear_poop(0) && !m.state.poop_count && m.state.sleeping, "sleeping cleanup failed");
            require(m.state.inventory[5] == soap && m.state.coins == coins && m.state.care_total == 1, "cleanup consumed bath supplies");
            require(!m.clear_poop(1) && m.state.care_total == 1, "empty cleanup farmed care");
            m.wake(); m.state.poop_count = 1; m.state.cleanliness = 30;
            require(m.clean(0) && m.state.poop_count == 1 && m.state.inventory[5] == soap - 1, "bath silently cleaned waste");
        }},
        {"three waste piles remind first and only cause illness after ninety minutes", [] {
            Model clean, messy; messy.state.poop_count = 3;
            clean.tick(120000); messy.tick(120000);
            require(!messy.state.sick && (messy.needs() & NeedClean), "two minutes of waste caused illness instead of a reminder");
            clean.tick(90u * 60000u - 120000 - 1); messy.tick(90u * 60000u - 120000 - 1);
            require(messy.state.cleanliness < clean.state.cleanliness && messy.state.health < clean.state.health, "waste did not affect care");
            require(!messy.state.sick, "waste illness arrived before ninety continuous minutes");
            messy.tick(1);
            require(messy.state.sick && messy.state.sickness_cause == SicknessCause::Dirt, "full waste habitat did not cause illness");
        }},
        {"illness digestion and recovery are independent of tick cadence", [] {
            State saved; saved.fullness = 11; saved.energy = 12; saved.cleanliness = 22;
            saved.digestion_ms[0] = 12345; saved.digestion_ms[1] = 98765;
            Model coarse, fine; coarse.restore(saved); fine.restore(saved);
            coarse.tick(3u * 3600000u); for (int i = 0; i < 10800; ++i) fine.tick(1000);
            require(coarse.state.sick == fine.state.sick && coarse.state.sickness_cause == fine.state.sickness_cause, "illness cause changed with cadence");
            require(coarse.state.health == fine.state.health && coarse.state.cleanliness == fine.state.cleanliness && coarse.state.poop_count == fine.state.poop_count, "waste care changed with cadence");
            for (int i = 0; i < 3; ++i) require(coarse.state.neglect_ms[i] == fine.state.neglect_ms[i] && coarse.state.digestion_ms[i] == fine.state.digestion_ms[i], "timers changed with cadence");
            coarse.state.fullness = fine.state.fullness = 80;
            coarse.state.cleanliness = fine.state.cleanliness = 80; coarse.state.energy = fine.state.energy = 14;
            require(coarse.medicine(3u * 3600000u) && fine.medicine(3u * 3600000u), "treatment failed");
            require(coarse.sleep() && fine.sleep(), "treatment rest failed");
            coarse.tick(240000); for (int i = 0; i < 240; ++i) fine.tick(1000);
            require(!coarse.state.sick && !fine.state.sick && coarse.state.health == fine.state.health, "recovery crossing rest threshold changed with cadence");
        }},
        {"daily care requires a verified real date and only successful actions count", [] {
            Model m; m.sync_day(0);
            require(m.feed_start(Item::Fly, 0) && m.feed_finish(1400), "meal failed");
            require(!m.state.day_key && !m.state.daily_mask && !m.daily_ready(), "unknown clock invented daily progress");
            m.sync_day(20261001); require(m.day_verified() && m.state.day_key == 20261001, "real date failed");
            require(m.feed_start(Item::Fly, 2000) && m.cancel_feed() && !m.state.daily_mask, "cancelled meal counted as daily feed");
            require(m.feed_start(Item::Fly, 3000) && m.feed_finish(4400), "meal failed");
            require(m.state.daily_mask == DailyFeed, "successful meal did not count");
            m.state.cleanliness = 0; require(m.clean(4500), "bath failed");
            require(m.state.daily_mask == DailyFeed && !m.clear_poop(5000), "bath or empty cleanup counted");
            m.state.poop_count = 1; require(m.clear_poop(5000), "waste cleanup failed");
            require(m.state.daily_mask == (DailyFeed | DailyClean), "waste cleanup did not count");
            require(m.start_game(6000) && m.end_game(), "empty game failed");
            require(!m.daily_ready(), "zero-hit game counted daily play");
            require(m.start_game(7000) && m.hit_game(m.game_target(), 7100) && m.end_game(), "successful game failed");
            require(m.daily_ready(), "three care actions did not complete daily goal");
            const int coins = m.state.coins;
            require(m.claim_daily(8000) && m.state.coins == coins + 10 && m.state.daily_claims == 1, "daily reward failed");
            require(!m.claim_daily(8100) && !m.daily_ready(), "daily reward could be claimed twice");
        }},
        {"daily date validation restore and rollover prevent duplicated rewards", [] {
            Model m; m.sync_day(20260229); require(!m.day_verified(), "invalid non-leap date accepted");
            m.sync_day(20240229); require(m.day_verified(), "valid leap date rejected");
            m.state.daily_mask = 7; require(m.claim_daily(0), "reward failed");
            State saved = m.state; m.restore(saved);
            require(!m.day_verified() && !m.daily_ready(), "restore trusted a stale date");
            m.sync_day(20240229); require(m.state.daily_claimed && !m.claim_daily(0), "reload duplicated same-day reward");
            m.sync_day(20240301); require(!m.state.daily_mask && !m.state.daily_claimed, "next day did not reset goals");
            m.state.daily_mask = 7; require(m.daily_ready(), "new date goal failed");
            m.sync_day(20240228); require(!m.day_verified() && !m.claim_daily(0), "clock rollback allowed duplicate daily reward");
            m.sync_day(0); require(!m.daily_ready(), "lost clock allowed reward");
        }},
        {"permanent care badges survive dates and saves without evolution", [] {
            Model m; require(!m.badge_mask(), "new pet has unearned badges");
            m.state.care_total = 9; m.state.poop_count = 1; require(m.clear_poop(0), "care action failed");
            require(m.state.care_total == 10 && m.badge_mask() == 1, "first care milestone missing");
            m.state.care_total = 50; require(m.badge_mask() == 3, "second care milestone missing");
            m.state.care_total = 100; m.sync_day(20261001); m.sync_day(20261002); m.restore(m.state);
            require(m.badge_mask() == 7 && m.state.care_total == 100, "date or restore erased permanent badges");
        }},
        {"restore sanitises new care fields and retains pending digestion", [] {
            State bad; bad.poop_count = 255; bad.digestion_ms[0] = UINT32_MAX; bad.digestion_ms[1] = 20;
            bad.neglect_ms[0] = UINT32_MAX; bad.sick = true; bad.sickness_cause = static_cast<SicknessCause>(255);
            bad.treatment_ms = UINT32_MAX; bad.day_key = 20261301; bad.daily_mask = 255; bad.daily_claimed = true;
            Model m; m.restore(bad);
            require(m.state.poop_count == 3 && !m.state.digestion_ms[0] && !m.state.digestion_ms[1], "corrupt waste queue exceeded cap");
            require(m.state.neglect_ms[0] == 60u * 60000u && m.state.sickness_cause == SicknessCause::Weakness && m.state.treatment_ms == kRecoveryMs, "corrupt sickness fields not bounded");
            require(!m.state.day_key && !m.state.daily_mask && !m.state.daily_claimed, "invalid saved date retained rewards");
            State pending; pending.digestion_ms[0] = 1234; m.restore(pending); m.tick(1234);
            require(m.state.poop_count == 1, "saved digestion did not resume");
        }},
        {"V4 preserves the complete legacy prefix and new companions begin without rewards", [] {
            require(offsetof(State, affinity) == 176, "V4 bond fields overlap the old save padding");
            require(sizeof(State) == 216 && offsetof(State, retired_photo_receipt) == 200
                && offsetof(State, retired_photo_ids) == 204, "retired V4 placeholders changed legacy byte positions");
            Model m;
            require(!m.state.affinity && !m.state.affection_cooldown_ms && !m.state.companion_ms
                && !m.state.anniversary_claimed && !m.state.encounter_claimed && !m.state.encounter_total
                && !m.state.retired_photo_receipt && !m.state.retired_photo_ids[0] && !m.state.retired_photo_ids[1],
                "new companion inherited progress or paid rewards");
            require(std::strcmp(affinity_name(0), "初识") == 0 && std::strcmp(affinity_name(19), "初识") == 0
                && std::strcmp(affinity_name(20), "熟悉") == 0 && std::strcmp(affinity_name(50), "亲近") == 0
                && std::strcmp(affinity_name(80), "形影不离") == 0, "bond level boundaries are wrong");
        }},
        {"only completed meals earn bond and cancelled invalid or unfinished meals do not", [] {
            Model m;
            require(!m.feed_start(Item::Soap, 0) && !m.state.affinity, "failed feeding earned bond");
            require(m.feed_start(Item::Fly, 0) && !m.feed_finish(1399) && !m.state.affinity,
                "unfinished tongue animation earned bond");
            require(m.cancel_feed() && !m.state.affinity, "cancelled feeding earned bond");
            require(m.feed_start(Item::Cricket, 0) && m.feed_finish(kFeedDurationMs) && m.state.affinity == 2,
                "successful meal did not earn two bond points");
            require(!m.feed_finish(5000) && m.state.affinity == 2, "meal bond paid twice");
        }},
        {"petting bond cooldown survives reboot and suppresses rapid or backwards timestamp taps", [] {
            Model m;
            require(m.pet(100) && m.state.affinity == 1 && m.state.affection_cooldown_ms == kAffectionCooldownMs,
                "first stroke did not start persisted bond cooldown");
            require(m.pet(101) && m.pet(0) && m.state.affinity == 1, "repeated or backwards taps farmed bond");
            m.tick(59999);
            require(m.state.affection_cooldown_ms == 1 && m.state.affinity == 1, "cooldown elapsed too early");
            Model restarted; restarted.restore(m.state);
            require(restarted.pet(0) && restarted.state.affinity == 1 && restarted.state.affection_cooldown_ms == 1,
                "restart cleared the remaining cooldown");
            restarted.tick(1); require(!restarted.state.affection_cooldown_ms, "cooldown did not expire precisely");
            require(restarted.pet(1) && restarted.state.affinity == 2, "stroke after cooldown did not earn bond");
            require(restarted.feed_start(Item::Fly, 100) && !restarted.pet(200) && restarted.state.affinity == 2,
                "failed stroke during feeding earned bond");
        }},
        {"real hygiene and medicine earn bond without rewards for unavailable care", [] {
            Model m;
            require(!m.clear_poop(0) && !m.medicine(0) && !m.state.affinity, "unnecessary care earned bond");
            m.state.cleanliness = 100;
            require(!m.clean(0) && !m.state.affinity && m.state.inventory[5] == 2,
                "full cleanliness consumed soap or earned bond");
            m.state.cleanliness = 60;
            require(m.clean(0) && m.state.affinity == 1, "real bath did not earn one bond point");
            m.state.poop_count = 2;
            require(m.clear_poop(2000) && m.state.affinity == 2 && !m.clear_poop(2001), "poop cleanup bond paid incorrectly");
            m.state.sick = true;
            require(m.medicine(4000) && m.state.affinity == 4 && !m.medicine(4001), "medicine bond missing or duplicated");
        }},
        {"full successful games earn bond but early exits empty games and wrong hits do not", [] {
            Model early; require(early.start_game(0) && early.hit_game(early.game_target(), 100), "early fixture failed");
            require(early.end_game() && !early.state.affinity, "early game exit farmed bond");
            Model empty; require(empty.start_game(0), "empty fixture failed");
            require(!empty.hit_game(99, 100), "wrong insect accepted");
            empty.tick(kGameDurationMs); require(!empty.state.affinity, "zero-hit game earned bond");
            Model full; require(full.start_game(0) && full.hit_game(full.game_target(), 100), "full fixture failed");
            full.tick(kGameDurationMs - 1); require(!full.state.affinity && full.game_running(), "game bond paid before twenty seconds");
            full.tick(1); require(full.state.affinity == 2 && !full.game_running() && !full.end_game(),
                "completed game did not earn exactly two bond points");
            Model overflow; require(overflow.start_game(UINT64_MAX - 100), "overflow fixture failed");
            require(overflow.hit_game(overflow.game_target(), UINT64_MAX - 99), "overflow hit failed");
            overflow.tick(100); require(!overflow.state.affinity, "saturated clock paid a game before its duration");
        }},
        {"bond caps at one hundred never decays and persists across restoration", [] {
            State s; s.affinity = 99;
            Model m; m.restore(s); require(m.feed_start(Item::Fly, 0) && m.feed_finish(1400), "cap fixture meal failed");
            require(m.state.affinity == 100, "bond exceeded one hundred");
            m.state.sleeping = true; m.tick(6u * 3600000u);
            require(m.state.affinity == 100, "sleep illness or absence reduced permanent bond");
            m.restore(m.state); require(m.state.affinity == 100, "restore erased bond");
        }},
        {"powered-on companionship includes illness rest and screen-off time but reboot adds none", [] {
            State s; s.sick = true; s.sleeping = true;
            Model m; m.restore(s); m.tick(123456);
            require(m.state.companion_ms == 123456 && !m.state.growth_ms, "care pause also paused companionship");
            m.restore(m.state); require(m.state.companion_ms == 123456, "restore added offline time");
            m.sync_day(20990101); require(m.state.companion_ms == 123456, "calendar change aged companion");
            m.tick(UINT32_MAX);
            require(m.state.companion_ms == 123456ull + 6ull * 3600000ull, "tick guard lost its six-hour bound");
            m.reset(); require(!m.state.companion_ms && !m.pending_anniversary(), "reset retained old companionship");
        }},
        {"companionship publishes precise milliseconds with five-minute flash checkpoints", [] {
            State s; s.sleeping = true; s.health = s.fullness = s.energy = s.mood = s.cleanliness = 100;
            s.growth_ms = kGrowthAdultMs;
            Model m; m.restore(s); m.tick(40);
            require(m.state.companion_ms == 40 && !m.dirty(), "one display tick requested companionship flash save");
            m.tick(4960); require(m.state.companion_ms == 5000 && !m.dirty(), "five seconds requested companionship flash save");
            s.companion_ms = 5ull * 60000ull - 40; m.restore(s); m.tick(40);
            require(m.state.companion_ms == 5ull * 60000ull && m.dirty(), "companionship checkpoint did not request persistence");
            m.clear_dirty(); m.tick(40); require(!m.dirty(), "companionship checkpoint remained continuously dirty");
        }},
        {"adult anniversaries appear at twenty-one and thirty accumulated days once each", [] {
            State s; s.growth_ms = kGrowthAdultMs; s.companion_ms = 21ull * kCompanionDayMs - 1;
            Model m; m.restore(s);
            require(!m.pending_anniversary() && !m.complete_anniversary(21), "anniversary appeared before day twenty-one");
            m.tick(1); require(m.pending_anniversary() == 21 && !m.complete_anniversary(30), "wrong anniversary order");
            const auto coins = m.state.coins;
            require(m.complete_anniversary(21) && !m.complete_anniversary(21) && !m.pending_anniversary()
                && m.state.coins == coins + 10 && m.state.affinity == 3 && m.state.anniversary_claimed == 1,
                "twenty-one-day reward missing or paid twice");
            m.restore(m.state); require(!m.pending_anniversary(), "reboot replayed the celebrated anniversary");
            s = m.state; s.companion_ms = 30ull * kCompanionDayMs; m.restore(s);
            require(m.pending_anniversary() == 30 && m.complete_anniversary(30) && !m.complete_anniversary(30)
                && m.state.anniversary_claimed == 3 && m.state.coins == coins + 25 && m.state.affinity == 6,
                "thirty-day reward missing or paid twice");
        }},
        {"missed anniversaries wait for adulthood and retain deterministic one-time order", [] {
            State s; s.companion_ms = 31ull * kCompanionDayMs; s.growth_ms = kGrowthTeenMs;
            Model m; m.restore(s); require(!m.pending_anniversary() && !m.complete_anniversary(21), "young pet received adult anniversary");
            m.state.growth_ms = kGrowthAdultMs;
            require(m.pending_anniversary() == 21 && m.complete_anniversary(21) && m.pending_anniversary() == 30
                && m.complete_anniversary(30) && !m.pending_anniversary(), "missed celebrations skipped or repeated a milestone");
            require(!m.complete_anniversary(0) && !m.complete_anniversary(255), "unknown anniversary accepted");
        }},
        {"forest encounters respect verified clock exact hours weather and daily rewards", [] {
            Model m;
            require(!m.encounter_available(Encounter::Dew, 8, Weather::Clear, 0), "unsynchronised clock spawned rewarding dew");
            m.sync_day(20261004);
            for (int hour = -1; hour <= 24; ++hour) {
                require(m.encounter_available(Encounter::Dew, hour, Weather::Clear, 0) == (hour >= 6 && hour < 10),
                    "dew time window wrong");
                require(m.encounter_available(Encounter::Firefly, hour, Weather::Clear, 0) == (hour >= 0 && hour < 24 && (hour >= 20 || hour < 6)),
                    "firefly time window wrong");
            }
            require(!m.encounter_available(Encounter::Dew, 8, Weather::Unknown, 0)
                && !m.encounter_available(Encounter::Firefly, 22, Weather::Storm, 0)
                && !m.encounter_available(static_cast<Encounter>(255), 8, Weather::Clear, 0), "invalid weather or encounter accepted");
            const auto coins = m.state.coins;
            require(m.complete_encounter(Encounter::Dew, 8, Weather::Clear, 0) && m.state.affinity == 1
                && m.state.coins == coins + 1 && m.state.encounter_total == 1 && m.state.encounter_claimed == 1,
                "dew did not give its small once-only reward");
            require(!m.complete_encounter(Encounter::Dew, 8, Weather::Clear, 2000), "same-day dew paid twice");
            require(m.complete_encounter(Encounter::Firefly, 22, Weather::Clear, 2000)
                && m.state.encounter_total == 2 && m.state.affinity == 2 && m.state.coins == coins + 3,
                "different same-day encounter was blocked or paid wrongly");
        }},
        {"rainbows need an observed rain-to-clear transition and expire without offline history", [] {
            Model m; m.sync_day(20261004);
            m.observe_weather(Weather::Clear, 100);
            require(!m.encounter_available(Encounter::Rainbow, 12, Weather::Clear, 100), "clear weather alone produced rainbow");
            m.observe_weather(Weather::Rain, 1000); m.observe_weather(Weather::Clear, 2000);
            require(m.encounter_available(Encounter::Rainbow, 12, Weather::Clear, 2000)
                && m.encounter_available(Encounter::Rainbow, 12, Weather::Cloudy, 2000 + kRainbowWindowMs - 1),
                "observed rain transition did not open the rainbow window");
            require(!m.encounter_available(Encounter::Rainbow, 22, Weather::Clear, 2000)
                && !m.encounter_available(Encounter::Rainbow, 12, Weather::Clear, 2000 + kRainbowWindowMs)
                && !m.encounter_available(Encounter::Rainbow, 12, Weather::Clear, 1999), "rainbow accepted night expired or stale time");
            m.restore(m.state); m.sync_day(20261004);
            require(!m.encounter_available(Encounter::Rainbow, 12, Weather::Clear, 0), "reboot restored unobserved rainfall");
            m.observe_weather(Weather::Storm, 0); m.observe_weather(Weather::Cloudy, 1000);
            require(m.complete_encounter(Encounter::Rainbow, 12, Weather::Cloudy, 1000) && m.state.coins == 33
                && m.state.encounter_claimed == 2 && m.state.encounter_total == 1, "rainbow reward or once-only mask wrong");
        }},
        {"busy sleeping sick and manual interactions cannot collect forest encounters", [] {
            for (int condition = 0; condition < 6; ++condition) {
                Model m; m.sync_day(20261004);
                if (condition == 0) m.state.sleeping = true;
                if (condition == 1) m.state.sick = true;
                if (condition == 2) require(m.feed_start(Item::Fly, 0), "busy feed fixture failed");
                if (condition == 3) require(m.start_game(0), "busy game fixture failed");
                if (condition == 4) require(m.pet(0), "busy pet fixture failed");
                const auto before = m.state;
                require(!m.encounter_available(Encounter::Dew, 8, Weather::Clear, 0, condition == 5)
                    && !m.complete_encounter(Encounter::Dew, 8, Weather::Clear, 0, condition == 5), "busy or ill pet collected encounter");
                require(m.state.affinity == before.affinity && m.state.coins == before.coins && !m.state.encounter_total,
                    "failed encounter changed money or bond");
            }
        }},
        {"saved encounter masks survive restart new days reset and rollback cannot farm rewards", [] {
            Model m; m.sync_day(20261004);
            require(m.complete_encounter(Encounter::Dew, 8, Weather::Clear, 0), "daily fixture failed");
            m.restore(m.state);
            require(!m.complete_encounter(Encounter::Dew, 8, Weather::Clear, 0), "restart trusted stale day");
            m.sync_day(20261004); require(!m.complete_encounter(Encounter::Dew, 8, Weather::Clear, 0), "restart reset same-day mask");
            m.sync_day(20261003); require(!m.day_verified() && !m.complete_encounter(Encounter::Dew, 8, Weather::Clear, 2000),
                "calendar rollback duplicated encounter");
            m.sync_day(20261005);
            require(!m.state.encounter_claimed && m.complete_encounter(Encounter::Dew, 8, Weather::Clear, 2000)
                && m.state.encounter_total == 2, "next verified day did not reset encounters");
            m.observe_weather(Weather::Rain, 4000); m.observe_weather(Weather::Clear, 5000);
            m.sync_day(0); m.sync_day(20261005);
            require(!m.encounter_available(Encounter::Rainbow, 12, Weather::Clear, 6000), "lost clock retained a rewarding weather transition");
        }},
        {"new field restoration and saturated counters are bounded without arithmetic wrap", [] {
            State s; s.affinity = UINT16_MAX; s.affection_cooldown_ms = UINT32_MAX; s.companion_ms = UINT64_MAX;
            s.anniversary_claimed = 255; s.encounter_claimed = 255; s.encounter_total = UINT32_MAX;
            s.day_key = 20261004; s.coins = 999999;
            for (auto &byte : s.v3_reserved) byte = 255;
            Model m; m.restore(s);
            require(m.state.affinity == 100 && m.state.affection_cooldown_ms == kAffectionCooldownMs
                && m.state.companion_ms == kMaxCompanionMs && m.state.anniversary_claimed == 3
                && m.state.encounter_claimed == 7 && m.state.encounter_total == 1000000u,
                "corrupt V4 fields did not clamp");
            for (const auto byte : m.state.v3_reserved) require(!byte, "legacy padding retained untrusted bytes");
            m.tick(1); require(m.state.companion_ms == kMaxCompanionMs && m.state.affection_cooldown_ms == kAffectionCooldownMs - 1,
                "saturated companionship or cooldown wrapped");
            m.sync_day(20261005);
            require(m.complete_encounter(Encounter::Dew, 8, Weather::Clear, 2) && m.state.coins == 999999
                && m.state.encounter_total == 1000000u && m.state.affinity == 100, "reward cap arithmetic wrapped");
            s.day_key = 20261301; m.restore(s); require(!m.state.encounter_claimed && !m.day_verified(), "bad date retained active reward mask");
        }},
        {"long absences agree with irregular timer ticks across care boundaries", [] {
            for (int profile = 0; profile < 48; ++profile) {
                State s;
                s.health = profile % 3 == 0 ? 40 : profile % 3 == 1 ? 45 : 100;
                s.fullness = profile % 4 == 0 ? 10 : profile % 4 == 1 ? 11 : profile % 4 == 2 ? 25 : 90;
                s.energy = profile % 5 == 0 ? 9 : profile % 5 == 1 ? 10 : 45;
                s.cleanliness = profile % 6 == 0 ? 20 : profile % 6 == 1 ? 21 : 80;
                s.poop_count = profile % 4; s.sleeping = profile % 2;
                s.sick = profile % 7 == 0; s.treatment_ms = s.sick ? kRecoveryMs : 0;
                s.digestion_ms[0] = s.poop_count < 3 ? 23456 : 0;
                Model one, irregular; one.restore(s); irregular.restore(s);
                one.tick(600000);
                for (uint32_t passed = 0; passed < 600000;) {
                    const uint32_t dt = std::min<uint32_t>(600000 - passed, 17 + passed % 977);
                    irregular.tick(dt); passed += dt;
                }
                if (one.state.health != irregular.state.health || one.state.fullness != irregular.state.fullness
                    || one.state.energy != irregular.state.energy || one.state.cleanliness != irregular.state.cleanliness) {
                    std::cerr << "profile " << profile << " coarse " << one.state.health << '/' << one.state.fullness << '/' << one.state.energy << '/' << one.state.cleanliness
                        << " irregular " << irregular.state.health << '/' << irregular.state.fullness << '/' << irregular.state.energy << '/' << irregular.state.cleanliness << '\n';
                }
                require(one.state.health == irregular.state.health && one.state.fullness == irregular.state.fullness
                    && one.state.energy == irregular.state.energy && one.state.cleanliness == irregular.state.cleanliness,
                    "irregular tick cadence changed care outcome");
                require(one.state.sick == irregular.state.sick && one.state.sickness_cause == irregular.state.sickness_cause
                    && one.state.treatment_ms == irregular.state.treatment_ms && one.state.poop_count == irregular.state.poop_count,
                    "irregular tick cadence changed sickness or digestion");
                for (int i = 0; i < 3; ++i) require(one.state.neglect_ms[i] == irregular.state.neglect_ms[i], "irregular cadence changed neglect exposure");
            }
        }}
    };
    int passed = 0;
    for (const auto &test : cases) {
        try { test.second(); ++passed; std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception &error) { std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; return 1; }
    }
    std::cout << passed << " chameleon care/life tests passed\n";
    return 0;
}
