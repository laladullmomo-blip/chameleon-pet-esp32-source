#include "../main/chameleon_hardware_schedule.h"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace {
unsigned checks=0;
void expect(uint32_t actual, uint32_t wanted, const char *what) {
    ++checks;
    if(actual!=wanted) {
        std::fprintf(stderr,"FAIL %s: got %u, wanted %u\n",what,actual,wanted);
        std::exit(1);
    }
}
}

int main() {
    using ch_hw::detail::NetworkSchedule;
    using ch_hw::detail::network_wait_ms;
    constexpr int64_t now=90000;
    NetworkSchedule schedule;
    expect(network_wait_ms(now,schedule),30000,"unconfigured offline service blocks");
    schedule.work_pending=true;
    expect(network_wait_ms(now,schedule),1,"configuration request gets next turn");
    schedule.work_pending=false;
    schedule.station_configured=true;
    schedule.reconnect_at=now+30000;
    expect(network_wait_ms(now,schedule),30000,"thirty-second retry fallback");
    expect(network_wait_ms(now+29250,schedule),750,"retry deadline beats fallback");
    expect(network_wait_ms(now+29750,schedule),250,"old quarter-second cadence preserved at deadline");
    expect(network_wait_ms(now+29999,schedule),1,"one millisecond to retry");
    expect(network_wait_ms(now+30000,schedule),1,"retry due now");
    expect(network_wait_ms(now+30001,schedule),1,"overdue retry handled promptly");
    schedule.connected=true;
    schedule.weather_at=now+15*60*1000;
    expect(network_wait_ms(now,schedule),30000,"connected weather interval stays fifteen minutes");
    schedule.reconnect_at=0;
    expect(network_wait_ms(now,schedule),30000,"connected state ignores old retry deadline");
    schedule.weather_at=now+650;
    expect(network_wait_ms(now,schedule),650,"weather deadline beats fallback");
    expect(network_wait_ms(now+649,schedule),1,"one millisecond to weather refresh");
    expect(network_wait_ms(now+650,schedule),1,"weather due now");
    schedule.weather_at=0;
    expect(network_wait_ms(now,schedule),1,"new connection immediately fetches weather");
    schedule.weather_at=now+15*60*1000;
    schedule.work_pending=true;
    expect(network_wait_ms(now,schedule),1,"manual refresh request gets next turn");
    schedule.work_pending=false;
    schedule.weather_expires_at=now+45*60*1000;
    expect(network_wait_ms(now,schedule),30000,"forty-five-minute expiry stays unchanged");
    schedule.weather_expires_at=now+300;
    expect(network_wait_ms(now,schedule),300,"expiry deadline beats weather and fallback");
    expect(network_wait_ms(now+299,schedule),1,"one millisecond to expiry");
    expect(network_wait_ms(now+300,schedule),1,"expiry boundary immediately reevaluated");
    expect(network_wait_ms(now+301,schedule),30000,"expired weather cannot cause busy polling");
    schedule.weather_at=now+200;
    schedule.weather_expires_at=now+300;
    expect(network_wait_ms(now,schedule),200,"earliest deadline wins");
    schedule.connected=false;
    schedule.reconnect_at=now+500;
    expect(network_wait_ms(now,schedule),300,"offline expiry precedes reconnect");
    schedule.station_configured=false;
    schedule.weather_expires_at=0;
    expect(network_wait_ms(now,schedule),30000,"offline state ignores weather and retry deadlines");
    schedule.weather_expires_at=now-1;
    expect(network_wait_ms(now,schedule),30000,"stale offline timestamp stays blocked");
    schedule.work_pending=true;
    expect(network_wait_ms(now,schedule),1,"pending city update while offline remains responsive");
    schedule.work_pending=false;
    schedule.provisioning=true;
    expect(network_wait_ms(now,schedule),1000,"setup mode retains one-second fallback");
    schedule.station_configured=true;schedule.reconnect_at=now+750;
    expect(network_wait_ms(now,schedule),750,"setup retry deadline remains exact");
    schedule.work_pending=true;
    expect(network_wait_ms(now,schedule),1,"setup notifications retain immediate processing");

    using ch_hw::detail::ReconnectBackoff;
    ReconnectBackoff retry;
    retry.reset(now);
    expect(static_cast<uint32_t>(retry.at-now),0,"manual retry is immediate");
    for(const uint32_t interval:{30000u,60000u,120000u,240000u,300000u,300000u}) {
        retry.attempted(now);
        expect(static_cast<uint32_t>(retry.at-now),interval,"repeated failed scans back off and cap at five minutes");
    }
    retry.reset(now);retry.attempted(now);
    expect(static_cast<uint32_t>(retry.at-now),30000,"successful reconnect resets first retry interval");

    using ch_hw::detail::WeatherBackoff;
    WeatherBackoff weather_retry;
    for(const uint32_t interval:{60000u,120000u,300000u,900000u,900000u})
        expect(weather_retry.failed_delay_ms(),interval,"weather failures use bounded exponential backoff");
    weather_retry.reset();
    expect(weather_retry.failed_delay_ms(),60000,"a fresh weather request resets its retry delay");

    using ch_hw::detail::DebouncedKey;
    DebouncedKey key;
    key.init(false,0);
    key.observe(true,100);
    expect(key.settle(139),false,"press is not stable before forty milliseconds");
    expect(static_cast<uint32_t>(key.deadline()),140,"press schedules its precise debounce deadline");
    expect(key.settle(140),false,"stable press counts only on release");
    key.observe(false,200);
    expect(key.settle(239),false,"release is not stable before forty milliseconds");
    expect(key.settle(240),true,"ordinary short press counts once on release");
    expect(key.settle(1000),false,"settled release does not repeat");
    expect(static_cast<uint32_t>(key.deadline()),0,"idle key needs no debounce polling");

    key.init(true,0);
    key.observe(false,100);
    expect(key.settle(140),false,"button held during startup never toggles screen");
    key.observe(true,200);key.settle(240);key.observe(false,300);
    expect(key.settle(340),true,"startup release arms the next press");

    key.init(false,0);key.observe(true,100);key.settle(140);
    key.observe(false,1100);
    expect(key.settle(1140),false,"one-second hold is excluded from short press");
    key.observe(true,1200);key.settle(1240);key.observe(false,2199);
    expect(key.settle(2239),true,"hold just below one second remains short");

    key.init(false,0);key.observe(true,100);key.observe(false,120);
    expect(key.settle(500),false,"sub-forty-millisecond glitch does not trigger");
    key.observe(true,600);key.observe(false,615);key.observe(true,620);
    expect(key.settle(659),false,"bounce restarts the stable-level window");
    key.settle(660);key.observe(false,800);key.observe(true,815);key.observe(false,820);
    expect(key.settle(860),true,"bouncing press and release emit one tap");

    // These queued edges are processed long after they occurred. Settling at
    // each edge timestamp preserves valid taps without periodic GPIO reads.
    key.init(false,0);key.observe(true,100);
    expect(key.settle(180),false,"queued release settles earlier press first");
    key.observe(false,180);
    expect(key.settle(260),true,"queued next press settles preceding valid tap");
    key.observe(true,260);key.settle(340);key.observe(false,340);
    expect(key.settle(800),true,"second queued complete tap survives blocked worker");
    key.init(true,900);key.observe(false,1000);
    expect(key.settle(1040),false,"overflow recovery does not fabricate a tap");
    expect(ch_hw::detail::kBatteryPeriodMs,30000,"battery percentage cadence is thirty seconds");
    expect(ch_hw::detail::kChargingPeriodMs,5000,"charging detection keeps five-second cadence");
    std::printf("PASS hardware scheduling and debounce: %u checks\n",checks);
}
