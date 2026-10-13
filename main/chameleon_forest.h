#pragma once
#include <cstdint>

namespace ch_forest {
enum class Material : uint8_t { Leaf, Petal, Nut, Dew };
enum class WishKind : uint8_t { Food, Pet, Game };
enum class WishStatus : uint8_t { Waiting, Active, Complete, Skipped };
enum class Season : uint8_t { Spring, Summer, Autumn, Winter };
enum class Holiday : uint8_t { None, NewYear, TreeDay, ChildrenDay, NationalWeek };
struct Wish { uint8_t kind=0,target=0,progress=0,status=0; };
struct State {
    uint32_t day=0,active_ms=0,encounters=0,meals=0,strokes=0;
    uint16_t materials[4]{};
    uint32_t forage_wait_ms=0,wishes_complete=0;
    Wish wishes[2]{};
    uint16_t holiday_year=0;
    uint8_t holiday_mask=0,forage_used=0,crafted_mask=0,equipped=0,season_seen=0,reserved=0;
};
static_assert(sizeof(Wish)==4&&sizeof(State)==52,"Forest save layout");
struct Recipe { const char *name; uint8_t cost[4]; };
extern const Recipe recipes[3];
const char *material_name(Material material);
const char *season_name(Season season);
const char *holiday_name(Holiday holiday);
bool valid_date(uint32_t day);
Season season_for_date(uint32_t day);
Holiday holiday_for_date(uint32_t day);
bool valid_state(const State &state);
int required(const Wish &wish);

class Model {
public:
    State state;
    void initialize(uint32_t encounters,uint32_t meals,uint32_t strokes);
    bool restore(const State &saved);
    bool sync_date(uint32_t day);
    bool date_ready() const { return date_ready_; }
    // Called by the existing UI tick. Eligibility excludes rest, illness,
    // screen-off, games, feeding and modal reminders; no new timer is needed.
    bool tick(uint32_t elapsed,bool eligible);
    bool issue();
    bool skip(int slot);
    int active_wish() const;
    bool food(uint32_t meals,uint8_t item);
    bool pet(uint32_t strokes);
    bool game(uint8_t kind,int hits,bool full_round);
    // Receipt comes from the persisted pet counter, so a missed save can be
    // reconciled after reboot without repeating already collected materials.
    bool sync_encounters(uint32_t receipt);
    int forage(); // Material index, or -1 when unavailable.
    bool craft(int recipe);
    bool equip(int recipe); // -1 removes decoration.
    bool holiday_gift();
    bool holiday_available() const;
    bool dirty() const { return dirty_; }
    void clear_dirty() { dirty_=false; }
private:
    bool date_ready_=false,dirty_=false;
    bool advance(WishKind kind,uint8_t target);
    void add(Material material,unsigned count=1);
};
} // namespace ch_forest
