#include "dsp/Engine.h"
#include "support/AperiodicMetrics.h"
#include "support/PitchedSwarmReference.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <vector>

namespace {
void require(bool ok,const char* message) { if(!ok) { std::cerr<<"FAIL: "<<message<<'\n'; std::exit(1); } }
struct Phrase { const char* name; std::array<int,8> notes; int count; };
constexpr std::array<Phrase,4> phrases{{{"low",{40},1},{"mid",{64},1},{"high",{88},1},{"dense",{40,47,52,55,59,64,67,71},8}}};
void measure(const char* component,const std::vector<float>& audio,int rate,int channel,const Phrase& phrase,double root)
{
    // Keep analysis duration/resolution comparable across rates. A 4096-point
    // window at 192 kHz covers only 21 ms and conflates a broad filtered-noise
    // region with a concentrated line. Preserve the calibrated score limits.
    const auto scale=rate>96000 ? 4u : rate>48000 ? 2u : 1u;
    for(const auto base:{4096u,16384u})
    {
        const auto size=base*scale;
        const auto m=dsp_core::test::measureAperiodic(audio,rate,size,root);
        std::cout<<rate<<','<<channel<<','<<phrase.name<<','<<component<<','<<size<<','<<m.rms<<','<<m.correlation<<','<<m.persistentLine<<'\n';
        require(m.correlation<0.40 && m.persistentLine<0.16,"diffusion survives the actual processed layer");
        require(m.rms>0.002 && m.rms<0.3,"each non-bass contribution has substantial bounded body");
    }
}
void balanceReference()
{
    for(const auto& phrase:phrases)
    {
        ironstack::Engine current; current.setLayersEnabled(true,true); current.prepare(48000);
        std::array<ironstack::test::PitchedSwarmReference,8> previous;
        std::array<ironstack::GuitarBus,2> bus;
        for(auto& side:bus) side.prepare(48000);
        for(int v=0;v<phrase.count;++v)
        { current.noteOn(1,phrase.notes[v],127); previous[v].reset(1,phrase.notes[v]); }
        std::vector<float> reference(96000);
        double now=0,old=0;
        for(int i=0;i<96000;++i)
        {
            float left,right; current.process(&left,&right,1,ironstack::LayerMask::guitar);
            double a=0,b=0;
            for(int v=0;v<phrase.count;++v)
            {
                const auto sample=previous[v].next(440.0*std::exp2((phrase.notes[v]-69.0)/12.0),48000);
                a+=sample.left; b+=sample.right;
            }
            const auto l=bus[0].next(a),r=bus[1].next(b);
            const auto mid=0.5*(l+r),side=0.5*(l-r)*ironstack::GuitarOscillators::spread;
            reference[static_cast<std::size_t>(i)]=static_cast<float>(mid+side);
            if(i<24000) continue;
            now+=0.5*(left*static_cast<double>(left)+right*static_cast<double>(right));
            old+=mid*mid+side*side;
        }
        std::cout<<"balance,"<<phrase.name<<",new_rms="<<std::sqrt(now/72000)<<",restored_rms="<<std::sqrt(old/72000)
                 <<",raw_difference_db="<<10*std::log10(now/old)<<",comparison_gain="<<std::sqrt(old/now)<<'\n';
        if(phrase.count==1)
            for(const auto size:{4096u,16384u})
            {
                const auto m=dsp_core::test::measureAperiodic(reference,48000,size,440.0*std::exp2((phrase.notes[0]-69.0)/12.0));
                std::cout<<"processed_control,"<<phrase.name<<','<<size<<','<<m.correlation<<','<<m.persistentLine<<'\n';
                // Passing diffusion requires BOTH scores to be low. A rich
                // pitched spectrum may distribute energy over many harmonics
                // while its autocorrelation still rejects it unambiguously.
                require(m.correlation>=0.40 || m.persistentLine>=0.16,"processed pitched reference cannot pass the diffuse gates");
            }
    }
}
}
int main()
{
    std::cout<<std::setprecision(9);
    balanceReference();
    std::cout<<std::setprecision(9)<<"rate,channel,phrase,component,window,rms,correlation,persistent_line\n";
    double peak=0;
    for(const auto rate:{44100,48000,88200,96000,176400,192000})
        for(const auto channel:{1,3})
            for(const auto& phrase:phrases)
            {
                ironstack::Engine engine; engine.setLayersEnabled(true,true); engine.prepare(rate);
                for(int n=0;n<phrase.count;++n) engine.noteOn(channel,phrase.notes[static_cast<std::size_t>(n)],127);
                const auto frames=static_cast<std::size_t>(rate)*3;
                std::vector<float> bass(frames),left(frames),right(frames);
                std::array<float,512> fullLeft{},fullRight{};
                for(std::size_t at=0;at<frames;)
                {
                    const auto count=std::min<std::size_t>(512,frames-at);
                    engine.process(fullLeft.data(),fullRight.data(),static_cast<int>(count),ironstack::LayerMask::both,
                        {bass.data()+at,nullptr,left.data()+at,right.data()+at},ironstack::BassComponent::grit);
                    for(std::size_t i=0;i<count;++i)
                    {
                        require(fullLeft[i]==bass[at+i]+left[at+i] && fullRight[i]==bass[at+i]+right[at+i],"diffuse contributions reconstruct Both exactly");
                        require(std::isfinite(fullLeft[i]) && std::isfinite(fullRight[i]),"combined output remains finite");
                        peak=std::max({peak,std::abs(static_cast<double>(fullLeft[i])),std::abs(static_cast<double>(fullRight[i]))});
                    }
                    at+=count;
                }
                const auto root=440.0*std::exp2((phrase.notes[0]-69.0)/12.0);
                measure("foundation_body",bass,rate,channel,phrase,root*0.5);
                measure("swarm_left",left,rate,channel,phrase,root);
                measure("swarm_right",right,rate,channel,phrase,root);
                for(int n=0;n<phrase.count;++n) engine.noteOff(channel,phrase.notes[static_cast<std::size_t>(n)]);
                const auto release=static_cast<int>(std::ceil(rate*ironstack::Engine::releaseSeconds))+ironstack::Engine::latencySamples+1;
                for(int i=0;i<release+32;++i)
                {
                    float l,r; engine.process(&l,&r,1);
                    if(i>=release) require(l==0 && r==0,"all layers cut to exact silence after the existing release/latency");
                }
            }
    // An injected zero source must remove all Foundation upper-body energy;
    // the protected clean sine must not have a second hidden pitched source.
    ironstack::BassVoice body; body.prepare(48000); body.start(55,127);
    const ironstack::BassExciter::Sources silence{};
    for(int i=0;i<48000;++i) require(body.next(1,ironstack::BassComponent::grit,&silence).output==0,"source ablation removes the entire upper body");
    std::cout<<"combined_nonbass_peak="<<peak<<" ablation=exact release=exact\n";
    require(peak<1.0,"combined diffuse body has full-velocity headroom");
}
