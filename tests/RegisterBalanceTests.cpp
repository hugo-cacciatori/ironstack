#include "dsp/Engine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <vector>

namespace {
void require(bool ok,const char* message) { if(!ok) { std::cerr<<"FAIL: "<<message<<'\n';std::exit(1); } }
struct Measurement { double rms=0,peak=0; };
Measurement render(const std::vector<int>& notes,int rate,bool channels,bool full)
{
    ironstack::Engine engine;engine.setLayersEnabled(true,true);engine.prepare(rate);
    for(std::size_t i=0;i<notes.size();++i) engine.noteOn(channels ? static_cast<int>(i)+1:1,notes[i],127);
    std::array<float,512> left{},right{};
    double power=0,peak=0;int measured=0;
    for(int at=0;at<rate*2;)
    {
        const auto count=std::min(512,rate*2-at);
        engine.process(left.data(),right.data(),count,full ? ironstack::LayerMask::both:ironstack::LayerMask::guitar);
        for(int i=0;i<count;++i)
        {
            require(std::isfinite(left[i]) && std::isfinite(right[i]),"register extremes remain finite");
            peak=std::max({peak,std::abs(static_cast<double>(left[i])),std::abs(static_cast<double>(right[i]))});
            if(at+i>=rate/2){power+=0.5*(left[i]*static_cast<double>(left[i])+right[i]*static_cast<double>(right[i]));++measured;}
        }
        at+=count;
    }
    return {std::sqrt(power/measured),peak};
}
}
int main()
{
    // Absolute pre-redesign level references; the old pitched control passing
    // through today's bus is a separate identity test, not this level oracle.
    const std::array<std::vector<int>,4> notes{{{40},{64},{88},{40,47,52,55,59,64,67,71}}};
    constexpr std::array<double,4> targets{0.0299177758,0.0624994787,0.10963084,0.10832924};
    constexpr std::array<double,4> tolerances{0.1,0.1,1.5,0.5};
    std::cout<<std::setprecision(10)<<"fixture,target_rms,current_rms,difference_db,peak\n";
    for(std::size_t i=0;i<notes.size();++i)
    {
        const auto m=render(notes[i],48000,false,false);
        const auto difference=20*std::log10(m.rms/targets[i]);
        std::cout<<i<<','<<targets[i]<<','<<m.rms<<','<<difference<<','<<m.peak<<'\n';
        require(std::abs(difference)<tolerances[i],"processed low/mid/high/dense reference balance");
    }
    const std::array<std::vector<int>,7> dense{{{88,89,90,91,92,93,94,95},{112,113,114,115,116,117,118,119},
        {120,121,122,123,124,125,126,127},{127,127,127,127,127,127,127,127},{96,96,96,96,96,96,96,96},
        {69,69,69,69,69,69,69,69},{0,120,121,122,123,124,125,127}}};
    double maximumPeak=0;
    for(const int rate:{44100,48000,192000})
        for(std::size_t i=0;i<dense.size();++i)
        {
            const auto m=render(dense[i],rate,i>=3 && i<=5,true);
            maximumPeak=std::max(maximumPeak,m.peak);
            std::cout<<"headroom,"<<rate<<','<<i<<','<<m.rms<<','<<m.peak<<'\n';
            require(m.peak<0.95,"eight high-register or cross-channel notes retain default headroom including clean bass");
        }
    for(const int rate:{44100,48000,192000})
    {
        ironstack::GuitarOscillators moving,continuing;
        for(auto* source:{&moving,&continuing}){source->prepare(rate);source->reset(1,64);}
        for(int i=0;i<rate/10;++i){moving.next(329.6275569128699);continuing.next(329.6275569128699);}
        const auto a=moving.next(1318.5102276514797),b=continuing.next(329.6275569128699);
        require(a.left==b.left && a.right==b.right,"register-gain move begins at the exact continuing sample");
    }
    std::cout<<"register_balance_cases=4 high_density_cases=21 maximum_combined_peak="<<maximumPeak<<" smoothed_onset=exact\n";
}
