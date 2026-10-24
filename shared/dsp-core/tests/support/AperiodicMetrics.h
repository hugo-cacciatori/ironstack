#pragma once
#include "StereoMetrics.h"
#include <array>

namespace dsp_core::test
{
// Offline diagnostics only, not a perceptual pitch detector or alias metric.
// Multiple late windows catch a source that settles into a persistent line.
struct AperiodicMetrics
{
    double rms=0.0, mean=0.0, peak=0.0, correlation=0.0, persistentLine=0.0;
    std::array<double,3> bandFractions{}; // 100-1500 / 1500-6000 / 6000-14000 Hz
    double above18k=0.0;
};
inline AperiodicMetrics measureAperiodic(const std::vector<float>& audio, double rate,
                                         std::size_t size, double controlHz)
{
    if(size<1024 || (size&(size-1))!=0 || audio.size()<size*4
       || !std::isfinite(rate) || rate<=0 || !std::isfinite(controlHz) || controlHz<=0
       || !std::all_of(audio.begin(),audio.end(),[](float x){return std::isfinite(x);}))
        throw std::invalid_argument("four windows and a power-of-two analysis size required");
    AperiodicMetrics result;
    std::vector<double> minimumLine(size/2,1.0);
    for(int frame=0;frame<4;++frame)
    {
        const auto start=static_cast<std::size_t>(0.15*rate)
            +static_cast<std::size_t>(frame)*(audio.size()-size-static_cast<std::size_t>(0.15*rate))/3;
        std::vector<float> window(size);
        double mean=0, energy=0;
        for(std::size_t i=0;i<size;++i)
        {
            const auto x=audio[start+i]; mean+=x; energy+=x*static_cast<double>(x);
            result.peak=std::max(result.peak,std::abs(static_cast<double>(x)));
        }
        mean/=static_cast<double>(size);
        result.mean=std::max(result.mean,std::abs(mean));
        result.rms+=std::sqrt(energy/static_cast<double>(size))/4;
        // Probe 64 log-spaced periods from 1.5 to 40 ms, plus the actual
        // periodic control's period/neighbours. Strided dot products reduce
        // cost; no downsampled waveform is used for spectral measurements.
        const auto correlationAt=[&](int lag)
        {
            // Adjacent-sample correlation is filter memory, not evidence of a
            // repeating note. Apply the same minimum to control-period probes.
            if(lag<static_cast<int>(rate*0.0015) || lag>=static_cast<int>(size/2)) return;
            double aa=0,bb=0,ab=0;
            for(std::size_t i=static_cast<std::size_t>(lag);i<size;i+=4)
            { const auto a=audio[start+i]-mean,b=audio[start+i-static_cast<std::size_t>(lag)]-mean; aa+=a*a; bb+=b*b; ab+=a*b; }
            if(aa*bb>1e-25) result.correlation=std::max(result.correlation,std::abs(ab/std::sqrt(aa*bb)));
        };
        for(int p=0;p<64;++p) correlationAt(static_cast<int>(rate*0.0015*std::pow(0.04/0.0015,p/63.0)));
        for(int offset=-2;offset<=2;++offset) correlationAt(static_cast<int>(rate/controlHz)+offset);
        for(std::size_t i=0;i<size;++i)
            window[i]=static_cast<float>((audio[start+i]-mean)*(0.5-0.5*std::cos(6.283185307179586*i/static_cast<double>(size))));
        const auto bins=spectrum(window);
        double total=0,above=0; std::array<double,3> bands{};
        for(std::size_t bin=1;bin<size/2;++bin)
        {
            const auto hz=bin*rate/static_cast<double>(size),power=std::norm(bins[bin]);
            total+=power;
            if(hz>=100 && hz<1500) bands[0]+=power;
            if(hz>=1500 && hz<6000) bands[1]+=power;
            if(hz>=6000 && hz<14000) bands[2]+=power;
            if(hz>=18000) above+=power;
        }
        if(total<1e-20) throw std::invalid_argument("silent source cannot pass tonality analysis");
        for(std::size_t b=0;b<3;++b) result.bandFractions[b]+=bands[b]/total/4;
        result.above18k=std::max(result.above18k,above/total);
        // A five-bin neighbourhood includes Hann leakage and modest drift.
        for(std::size_t bin=3;bin<size/2-3;++bin)
        {
            double local=0; for(int offset=-2;offset<=2;++offset) local+=std::norm(bins[static_cast<std::size_t>(static_cast<int>(bin)+offset)]);
            minimumLine[bin]=std::min(minimumLine[bin],local/total);
        }
    }
    result.persistentLine=*std::max_element(minimumLine.begin()+3,minimumLine.end()-3);
    return result;
}
}
