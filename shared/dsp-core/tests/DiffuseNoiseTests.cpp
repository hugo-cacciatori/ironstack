#include <dsp_core/DiffuseNoise.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace {
bool forbidAllocation = false;
void require(bool ok, const char* message) { if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); } }
}
void* operator new(std::size_t n) { if (forbidAllocation) std::abort(); if (auto* p=std::malloc(n ? n : 1)) return p; throw std::bad_alloc{}; }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main()
{
    static_assert(noexcept(std::declval<dsp_core::DiffuseNoise&>().next()));
    static_assert(noexcept(std::declval<dsp_core::DiffuseNoise&>().prepare(48000)));
    static_assert(noexcept(std::declval<dsp_core::DiffuseNoise&>().reset(0)));
    static_assert(noexcept(std::declval<dsp_core::DiffuseNoise&>().setRegister(440)));
    double minRms=10, maxRms=0, maxPeak=0, maxSeedCorrelation=0;
    for (const auto rate : {44100,48000,88200,96000,176400,192000})
        for (const auto hz : {27.5,55.0,220.0,880.0,3520.0,14080.0})
        {
            dsp_core::DiffuseNoise a,b,other;
            a.prepare(rate); b.prepare(rate); other.prepare(rate);
            a.setRegister(hz); b.setRegister(hz); other.setRegister(hz);
            a.reset(0); b.reset(0); other.reset(UINT64_C(0xffffffffffffffff));
            std::vector<double> reference(static_cast<std::size_t>(rate));
            double aa=0, bb=0, ab=0, mean=0;
            forbidAllocation=true;
            for (int i=0; i<rate; ++i)
            {
                const auto x=a.next(), y=b.next(), z=other.next();
                require(x==y, "same seed/rate/register replays exactly");
                require(std::isfinite(x) && std::fpclassify(x)!=FP_SUBNORMAL && std::abs(x)<8,
                        "finite normal bounded stochastic output");
                reference[static_cast<std::size_t>(i)]=x;
                aa+=x*x; bb+=z*z; ab+=x*z; mean+=x;
                maxPeak=std::max(maxPeak,std::abs(x));
            }
            forbidAllocation=false;
            minRms=std::min(minRms,std::sqrt(aa/rate)); maxRms=std::max(maxRms,std::sqrt(aa/rate));
            maxSeedCorrelation=std::max(maxSeedCorrelation,std::abs(ab/std::sqrt(aa*bb)));
            require(std::abs(mean/rate)<0.01,"no persistent DC");

            // A sample API must be invariant to arbitrary callback partitions,
            // including one sample and sizes changing on every callback.
            a.reset(0);
            std::size_t cursor=0, block=0;
            constexpr int sizes[]{1,31,512,7,2048,3,64};
            forbidAllocation=true;
            while(cursor<reference.size())
            {
                const auto count=std::min(reference.size()-cursor,static_cast<std::size_t>(sizes[block++%7]));
                for(std::size_t i=0;i<count;++i,++cursor)
                    require(a.next()==reference[cursor],"reset and changing block partitions are bit exact");
            }
            // Same control events at the same sample, regardless of partitions
            // and unrelated instance activity. Repeated setters must not stall.
            a.reset(17); b.reset(17);
            auto currentTarget=hz;
            for(int i=0;i<rate;++i)
            {
                if(i%997==0) { currentTarget=i%2 ? 55.0:3520.0; b.setRegister(currentTarget); }
                a.setRegister(currentTarget);
                if(i%19==0) other.next();
                const auto x=a.next(), y=b.next();
                require(x==y && std::isfinite(x),"smooth register changes and independent instances");
            }
            forbidAllocation=false;
        }
    // These source-level body limits are independently refined by the spectral
    // calibration suite. They prevent a future near-silent implementation.
    require(minRms>0.15 && maxRms<0.45,"substantial rate/register-independent body");
    require(maxSeedCorrelation<0.08,"independent streams without seed-zero lockup");
    dsp_core::DiffuseNoise reused;
    for(const auto rate : {48000,192000,44100})
    {
        reused.prepare(rate); reused.setRegister(880); reused.reset(29);
        dsp_core::DiffuseNoise fresh; fresh.prepare(rate); fresh.setRegister(880); fresh.reset(29);
        forbidAllocation=true;
        for(int i=0;i<10000;++i) require(reused.next()==fresh.next(),"prepare clears prior rate and filter state");
        forbidAllocation=false;
    }
    for(const auto rate : {0.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), 384000.0})
    {
        dsp_core::DiffuseNoise a; a.prepare(rate); a.reset(1);
        for(int i=0;i<10000;++i)
        {
            if(i%97==0) a.setRegister(i%2 ? std::numeric_limits<double>::quiet_NaN():std::numeric_limits<double>::infinity());
            require(std::isfinite(a.next()),"invalid rate/register containment");
        }
    }
    std::cout<<"diffuse_noise_rates=6 register_cases=36 allocations=0 replay=exact rms_min="<<minRms
             <<" rms_max="<<maxRms<<" peak="<<maxPeak<<" seed_correlation_max="<<maxSeedCorrelation<<'\n';
}
