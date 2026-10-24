#include <dsp_core/DiffuseNoise.h>
#include "support/PitchedSwarmReference.h"
#include "support/AperiodicMetrics.h"
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>

namespace {
void require(bool ok,const char* message) { if(!ok) { std::cerr<<"FAIL: "<<message<<'\n'; std::exit(1); } }
}
int main()
{
    // Harness controls: silence/invalid input cannot earn a tonality pass,
    // and lowering amplitude must not improve the normalised diagnostics.
    std::vector<float> fixture(96000,0.0f);
    const auto rejects=[&](const std::vector<float>& input)
    {
        try { dsp_core::test::measureAperiodic(input,48000,4096,220); }
        catch(const std::invalid_argument&) { return true; }
        return false;
    };
    require(rejects(fixture),"silent measurement rejected");
    fixture[0]=std::numeric_limits<float>::quiet_NaN();
    require(rejects(fixture),"nonfinite measurement rejected");
    for(std::size_t i=0;i<fixture.size();++i) fixture[i]=static_cast<float>(0.4*std::sin(6.283185307179586*220*i/48000));
    const auto full=dsp_core::test::measureAperiodic(fixture,48000,4096,220);
    for(auto& x:fixture) x*=0.5f;
    const auto half=dsp_core::test::measureAperiodic(fixture,48000,4096,220);
    require(std::abs(full.correlation-half.correlation)<1e-8 && std::abs(full.persistentLine-half.persistentLine)<1e-8,
            "tonality cannot pass by reducing level");
    require(std::abs(full.rms-2*half.rms)<1e-8,"independent body measurement tracks level");
    std::cout<<std::setprecision(9)<<"source,rate,hz,seed,window,rms,correlation,persistent_line,body_fraction,detail_fraction,air_fraction,above18k\n";
    for(const auto rate : {44100,48000,88200,96000,176400,192000})
        for(const auto hz : {27.5,55.0,220.0,880.0,3520.0,14080.0})
            for(const auto seed : {0u,17u,2039u})
            {
                dsp_core::DiffuseNoise source; source.prepare(rate); source.setRegister(hz); source.reset(seed);
                ironstack::test::PitchedSwarmReference old; old.reset(1,static_cast<int>(seed%128));
                std::mt19937 rng(seed);
                std::array<std::vector<float>,4> audio;
                for(auto& a:audio) a.resize(static_cast<std::size_t>(rate)*3);
                for(std::size_t i=0;i<audio[0].size();++i)
                {
                    audio[0][i]=static_cast<float>(source.next());
                    audio[1][i]=static_cast<float>(0.35*std::sin(6.283185307179586*hz*i/rate));
                    audio[2][i]=static_cast<float>(old.next(hz,rate).left);
                    audio[3][i]=static_cast<float>(0.5*(2.0*static_cast<double>(rng())/4294967296.0-1.0));
                }
                for(const auto size : {4096u,16384u})
                    for(std::size_t kind=0;kind<audio.size();++kind)
                    {
                        const auto m=dsp_core::test::measureAperiodic(audio[kind],rate,size,hz);
                        constexpr const char* names[]{"diffuse","sine_control","restored_swarm_control","white_reference"};
                        std::cout<<names[kind]<<','<<rate<<','<<hz<<','<<seed<<','<<size<<','<<m.rms<<','<<m.correlation<<','<<m.persistentLine
                                 <<','<<m.bandFractions[0]<<','<<m.bandFractions[1]<<','<<m.bandFractions[2]<<','<<m.above18k<<'\n';
                        // Calibrated against the controls at both window sizes.
                        // These detect periodic collapse and weak body; they do
                        // not claim perceptual pitchlessness or alias rejection.
                        if(kind==0)
                        {
                            require(m.rms>0.20 && m.rms<0.40,"substantial diffuse body level");
                            require(m.correlation<0.40,"no strong periodic autocorrelation");
                            require(m.persistentLine<0.16,"no persistent concentrated line");
                            require(m.bandFractions[0]>0.09 && m.bandFractions[1]>0.20 && m.bandFractions[2]>0.035,
                                    "body/detail/air remain occupied");
                            require(m.above18k<0.02,"source bandwidth rolls off above 18 kHz");
                        }
                        if(kind==1 && hz>=55.0 && hz<=880.0)
                            require(m.correlation>0.98 && m.persistentLine>0.98,"sine is detected as periodic");
                        if(kind==2 && hz>=55.0 && hz<=880.0)
                            require(m.correlation>0.60 && m.persistentLine>0.24,"restored unison is rejected as tonal");
                        if(kind==3)
                            require(m.correlation<0.25 && m.persistentLine<0.02,"broadband reference lacks persistent lines");
                    }
            }
}
