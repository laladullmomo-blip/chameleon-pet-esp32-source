#include "chameleon_forest.h"
#include <algorithm>

namespace ch_forest {
const Recipe recipes[3]={{"叶片挂饰",{3,2,0,0}},{"蘑菇灯",{0,0,3,2}},{"花瓣风铃",{2,3,0,1}}};
namespace {
uint32_t mix(uint32_t value){value^=value>>16;value*=0x7feb352d;value^=value>>15;return value;}
bool leap(unsigned year){return year%4==0&&(year%100!=0||year%400==0);}
unsigned year(uint32_t day){return day/10000;}
}
const char *material_name(Material material){const char *names[]={"叶片","花瓣","果壳","露珠"};const auto i=static_cast<unsigned>(material);return i<4?names[i]:"未知";}
const char *season_name(Season season){const char *names[]={"春日花开","夏夜微光","秋日落叶","冬日晶莹"};return names[static_cast<unsigned>(season)%4];}
const char *holiday_name(Holiday holiday){const char *names[]={"森林日常","元旦快乐","植树节","儿童节","国庆小庆典"};return names[static_cast<unsigned>(holiday)%5];}
bool valid_date(uint32_t day){
    const unsigned y=year(day),m=day/100%100,d=day%100;
    if(y<2020||y>2099||m<1||m>12||d<1)return false;
    const unsigned days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return d<=days[m-1]+(m==2&&leap(y)?1:0);
}
Season season_for_date(uint32_t day){const unsigned m=day/100%100;return m>=3&&m<=5?Season::Spring:m>=6&&m<=8?Season::Summer:m>=9&&m<=11?Season::Autumn:Season::Winter;}
Holiday holiday_for_date(uint32_t day){
    if(!valid_date(day))return Holiday::None;
    const unsigned md=day%10000;
    return md==101?Holiday::NewYear:md==312?Holiday::TreeDay:md==601?Holiday::ChildrenDay:md>=1001&&md<=1007?Holiday::NationalWeek:Holiday::None;
}
int required(const Wish &wish){return wish.kind==static_cast<uint8_t>(WishKind::Pet)?3:1;}
bool valid_state(const State &s){
    if((s.day&&!valid_date(s.day))||s.active_ms>600000||s.forage_wait_ms>60000||s.forage_used>3||s.crafted_mask>7||s.equipped>3||s.season_seen>15||s.holiday_mask>15||s.reserved||s.wishes_complete>1000000||s.encounters>1000000||s.meals>1000000||s.strokes>1000000)return false;
    if(s.equipped&&!(s.crafted_mask&(1u<<(s.equipped-1))))return false;
    if((s.holiday_year&& (s.holiday_year<2020||s.holiday_year>2099))||(!s.holiday_year&&s.holiday_mask))return false;
    for(auto count:s.materials)if(count>99)return false;
    bool unresolved=false;
    for(const auto &w:s.wishes){
        if(w.kind>2||w.status>3||w.target>3||(w.kind==1&&w.target)||w.progress>required(w))return false;
        if(w.status==2&&w.progress!=required(w))return false;
        if(w.status==0&&w.progress)return false;
        if(w.status==1&&w.progress>=required(w))return false;
        if(unresolved&&w.status!=0)return false;
        if(w.status==0||w.status==1)unresolved=true;
    }
    return true;
}
void Model::initialize(uint32_t encounters,uint32_t meals,uint32_t strokes){state=State{};state.encounters=encounters;state.meals=meals;state.strokes=strokes;date_ready_=false;dirty_=true;}
bool Model::restore(const State &saved){if(!valid_state(saved))return false;state=saved;date_ready_=false;dirty_=false;return true;}
bool Model::sync_date(uint32_t day){
    date_ready_=valid_date(day)&&day>=state.day;
    if(!date_ready_)return false;
    if(day==state.day)return false;
    state.day=day;state.active_ms=0;state.forage_used=0;
    const auto first=mix(day)%3;
    for(int i=0;i<2;++i){auto &w=state.wishes[i];w=Wish{};w.kind=(first+i)%3;w.target=w.kind==1?0:mix(day+i+71)%4;}
    if(year(day)>state.holiday_year){state.holiday_year=year(day);state.holiday_mask=0;}
    dirty_=true;return true;
}
bool Model::tick(uint32_t elapsed,bool eligible){
    const auto previous_wait=state.forage_wait_ms;
    state.forage_wait_ms=elapsed>=previous_wait?0:previous_wait-elapsed;
    if(previous_wait&&!state.forage_wait_ms)dirty_=true;
    if(!date_ready_||!eligible||active_wish()>=0||(state.wishes[0].status!=0&&state.wishes[1].status!=0))return false;
    const auto old=state.active_ms;state.active_ms=static_cast<uint32_t>(std::min<uint64_t>(600000,uint64_t(old)+elapsed));
    if(state.active_ms/60000!=old/60000)dirty_=true;
    const bool second=state.wishes[0].status!=0;
    if(state.active_ms>=(second?600000u:120000u))return issue();
    return false;
}
bool Model::issue(){
    if(!date_ready_||active_wish()>=0)return false;
    for(auto &w:state.wishes)if(w.status==0){w.status=1;state.active_ms=0;dirty_=true;return true;}
    return false;
}
int Model::active_wish() const {for(int i=0;i<2;++i)if(state.wishes[i].status==1)return i;return -1;}
bool Model::skip(int slot){if(!date_ready_||slot<0||slot>1||state.wishes[slot].status!=1)return false;state.wishes[slot].status=3;state.active_ms=0;dirty_=true;return true;}
void Model::add(Material material,unsigned count){auto &value=state.materials[static_cast<unsigned>(material)];value=std::min<unsigned>(99,value+count);dirty_=true;}
bool Model::advance(WishKind kind,uint8_t target){
    if(!date_ready_)return false;
    const int index=active_wish();if(index<0)return false;
    auto &w=state.wishes[index];if(w.kind!=static_cast<uint8_t>(kind)||w.target!=target)return false;
    ++w.progress;dirty_=true;
    if(w.progress<required(w))return false;
    w.status=2;state.active_ms=0;state.wishes_complete=std::min<uint32_t>(1000000u,state.wishes_complete+1);
    add(static_cast<Material>(mix(state.day+index+17)%4),2);return true;
}
bool Model::food(uint32_t meals,uint8_t item){if(item>3||meals<=state.meals||meals>1000000)return false;state.meals=meals;dirty_=true;return advance(WishKind::Food,item);}
bool Model::pet(uint32_t strokes){if(strokes<=state.strokes||strokes>1000000)return false;state.strokes=strokes;dirty_=true;return advance(WishKind::Pet,0);}
bool Model::game(uint8_t kind,int hits,bool full_round){return kind<4&&hits>0&&full_round&&advance(WishKind::Game,kind);}
bool Model::sync_encounters(uint32_t receipt){
    if(receipt<=state.encounters||receipt>1000000)return false;
    const auto previous=state.encounters;state.encounters=receipt;
    // Normally the delta is one; cap recovery work for a wildly newer imported
    // pet save. Old pre-install encounters are seeded, not retroactively paid.
    for(unsigned i=1;i<=std::min<uint32_t>(3u,receipt-previous);++i)add(static_cast<Material>((previous+i)%4));
    dirty_=true;return true;
}
int Model::forage(){
    if(!date_ready_||state.forage_used>=3||state.forage_wait_ms)return -1;
    const auto season=season_for_date(state.day);
    const unsigned seasonal[]={1,3,2,0};
    const unsigned material=state.forage_used==0?seasonal[static_cast<unsigned>(season)]:mix(state.day+state.forage_used)%4;
    add(static_cast<Material>(material),state.forage_used==0?2:1);
    state.season_seen|=1u<<static_cast<unsigned>(season);++state.forage_used;state.forage_wait_ms=60000;dirty_=true;
    return material;
}
bool Model::craft(int recipe){
    if(recipe<0||recipe>=3||(state.crafted_mask&(1u<<recipe)))return false;
    for(int i=0;i<4;++i)if(state.materials[i]<recipes[recipe].cost[i])return false;
    for(int i=0;i<4;++i)state.materials[i]-=recipes[recipe].cost[i];
    state.crafted_mask|=1u<<recipe;state.equipped=recipe+1;dirty_=true;return true;
}
bool Model::equip(int recipe){
    if(recipe<-1||recipe>=3||(recipe>=0&&!(state.crafted_mask&(1u<<recipe))))return false;
    const unsigned next=recipe+1;if(state.equipped==next)return false;state.equipped=next;dirty_=true;return true;
}
bool Model::holiday_available() const {const auto h=holiday_for_date(state.day);return date_ready_&&h!=Holiday::None&&state.holiday_year==year(state.day)&&!(state.holiday_mask&(1u<<(static_cast<unsigned>(h)-1)));}
bool Model::holiday_gift(){
    if(!holiday_available())return false;
    state.holiday_mask|=1u<<(static_cast<unsigned>(holiday_for_date(state.day))-1);
    for(int i=0;i<4;++i)add(static_cast<Material>(i),i<2?2:1);
    return true;
}
} // namespace ch_forest
