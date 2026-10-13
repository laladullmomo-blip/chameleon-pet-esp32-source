#include "chameleon_forest.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace ch_forest;
int total(const State &s){int n=0;for(auto v:s.materials)n+=v;return n;}
Model wish(WishKind kind,uint8_t target=0){Model m;m.initialize(0,0,0);m.sync_date(20261007);m.state.wishes[0].kind=static_cast<uint8_t>(kind);m.state.wishes[0].target=target;assert(m.issue());return m;}
int main(){
 assert(valid_date(20240229)&&!valid_date(20260229)&&!valid_date(20261301)&&!valid_date(19700101));
 assert(season_for_date(20260312)==Season::Spring&&season_for_date(20260601)==Season::Summer&&season_for_date(20261007)==Season::Autumn&&season_for_date(20260101)==Season::Winter);
 assert(holiday_for_date(20260312)==Holiday::TreeDay&&holiday_for_date(20260601)==Holiday::ChildrenDay&&holiday_for_date(20261007)==Holiday::NationalWeek&&holiday_for_date(20261008)==Holiday::None);
 Model m;m.initialize(8,15,20);assert(!m.issue()&&m.forage()==-1&&!m.holiday_gift());assert(m.sync_date(20261007));assert(m.state.encounters==8&&total(m.state)==0);
 assert(!m.tick(120000,false));assert(!m.tick(119999,true));assert(m.tick(1,true));assert(m.active_wish()==0);assert(!m.issue());assert(m.skip(0));assert(!m.skip(0));assert(!m.tick(599999,true));assert(m.tick(1,true));assert(m.skip(1));m.clear_dirty();assert(!m.tick(600000,true)&&!m.dirty());
 auto before=m.state;assert(!m.sync_date(20261006)&&!m.date_ready());assert(m.forage()==-1&&!m.issue()&&!m.holiday_gift());assert(!m.sync_date(20261007)&&m.date_ready());assert(std::memcmp(&before,&m.state,sizeof(before))==0);
 assert(m.sync_date(20261008)&&m.state.wishes[0].status==0&&m.state.wishes[1].status==0&&m.state.forage_used==0);
 auto f=wish(WishKind::Food,2);assert(!f.food(1,0));assert(!f.food(1,2));assert(f.food(2,2));assert(!f.food(3,2)&&total(f.state)==2&&f.state.wishes_complete==1);
 auto p=wish(WishKind::Pet);assert(!p.pet(1)&&!p.pet(1)&&!p.pet(2));assert(p.pet(3));assert(!p.pet(4)&&total(p.state)==2);assert(valid_state(p.state));
 auto g=wish(WishKind::Game,3);assert(!g.game(3,1,false)&&!g.game(3,0,true)&&!g.game(2,5,true));assert(g.game(3,1,true));assert(!g.game(3,1,true)&&total(g.state)==2);
 auto r=m.state;Model reboot;assert(reboot.restore(r)&&!reboot.date_ready());assert(!reboot.issue());reboot.sync_date(r.day);assert(reboot.date_ready());
 assert(reboot.sync_encounters(9)&&total(reboot.state)==1);assert(!reboot.sync_encounters(9));assert(reboot.sync_encounters(20)&&total(reboot.state)==4);assert(!reboot.sync_encounters(1000001));
 Model collect;collect.initialize(0,0,0);collect.sync_date(20261007);assert(collect.forage()==2&&collect.state.materials[2]==2&&collect.state.season_seen==4);assert(collect.forage()==-1);collect.tick(59999,false);assert(collect.forage()==-1);collect.tick(1,false);assert(collect.forage()>=0);collect.tick(60000,false);assert(collect.forage()>=0);collect.tick(60000,false);assert(collect.forage()==-1);assert(collect.sync_date(20261008)&&collect.forage()>=0);
 Model craft;craft.initialize(0,0,0);assert(!craft.craft(0)&&!craft.equip(0));craft.state.materials[0]=3;craft.state.materials[1]=2;assert(craft.craft(0)&&total(craft.state)==0&&craft.state.equipped==1);assert(!craft.craft(0));assert(craft.equip(-1)&&craft.equip(0)&&!craft.equip(1));assert(valid_state(craft.state));
 Model holiday;holiday.initialize(0,0,0);holiday.sync_date(20261001);assert(holiday.holiday_available()&&holiday.holiday_gift()&&total(holiday.state)==6);assert(!holiday.holiday_gift());holiday.sync_date(20261007);assert(!holiday.holiday_gift());holiday.sync_date(20260101);assert(!holiday.holiday_gift());holiday.sync_date(20270101);assert(holiday.holiday_gift()&&total(holiday.state)==12);holiday.sync_date(20270312);assert(holiday.holiday_gift());
 holiday.state.materials[0]=99;holiday.sync_date(20270601);assert(holiday.holiday_gift()&&holiday.state.materials[0]==99);
 auto bad=craft.state;bad.materials[0]=100;assert(!valid_state(bad));bad=craft.state;bad.equipped=4;assert(!valid_state(bad));bad=craft.state;bad.reserved=1;assert(!valid_state(bad));bad=craft.state;bad.wishes[1].status=1;assert(!valid_state(bad));bad=craft.state;bad.meals=1000001;assert(!valid_state(bad));assert(!reboot.restore(bad));
 std::puts("forest checks: 16 groups passed");
}
