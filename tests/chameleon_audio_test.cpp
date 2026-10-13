#include "chameleon_audio.h"
#include <cassert>
#include <cstdio>

int main() {
    assert(ch_audio::volume(-1)==0&&ch_audio::volume(101)==100);
    for(int v=0;v<=100;++v)assert(ch_audio::volume(v)==v);
    assert(ch_audio::sample(-1,4410,880,true)==0);
    assert(ch_audio::sample(4410,4410,880,true)==0);
    assert(ch_audio::sample(0,4410,880,true)==0);
    assert(ch_audio::sample(4409,4410,880,true)==0);
    for(float frequency:{880.0f,1060.0f,1240.0f}) {
        double effect_energy=0,alarm_energy=0;
        int peak=0;
        for(int i=0;i<4410;++i) {
            const int effect=ch_audio::sample(i,4410,frequency,false);
            const int alarm=ch_audio::sample(i,4410,frequency,true);
            effect_energy+=double(effect)*effect;alarm_energy+=double(alarm)*alarm;
            peak=std::max(peak,std::abs(alarm));
            assert(std::abs(alarm)<=11000&&std::abs(effect)<=2800);
        }
        assert(peak>10900&&peak<16384); // headroom, with a clearly stronger signal
        assert(alarm_energy>15*effect_energy&&alarm_energy<16*effect_energy);
    }
    // Old normal tap output is retained at the shipped level, including ramp.
    for(int i=0;i<882;++i) {
        const float envelope=std::min(1.0f,std::min(i/180.0f,(881-i)/300.0f));
        const int16_t shipped=std::sin(6.28318530718f*740.0f*i/22050)*2800.0f*envelope;
        assert(ch_audio::sample(i,882,740,false)==shipped);
    }
    std::puts("Audio checks passed: range, silence, alarm headroom/energy, unchanged taps");
}
