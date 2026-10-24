#include <dsp_core/EllipticHighpass.h>
#include "support/SeparationGuardReference.h"
#include "support/StereoMetrics.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <utility>

namespace {
bool forbidAllocation=false;
void require(bool ok,const char* message) { if(!ok) { std::cerr<<"FAIL: "<<message<<'\n'; std::exit(1); } }
}
void* operator new(std::size_t n) { if(forbidAllocation) std::abort(); if(auto* p=std::malloc(n?n:1))return p; throw std::bad_alloc{}; }
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}

int main()
{
    static_assert(noexcept(std::declval<dsp_core::EllipticHighpass&>().next(0)));
    static_assert(noexcept(std::declval<dsp_core::EllipticHighpass&>().prepare(350,48000)));
    static_assert(noexcept(std::declval<dsp_core::EllipticHighpass&>().reset()));
    double maximumError=0,maximumStop=0,minimumPass=2,maximumPass=0,maximumPeak=0;
    // Independent SciPy oracle, including complex phase. Python is not needed
    // to compile/run these committed numeric references.
    for(const auto& ref:dsp_core::test::guardReferences)
    {
        dsp_core::EllipticHighpass filter; filter.prepare(350,ref.rate);
        std::complex<double> sum{};
        forbidAllocation=true;
        for(int i=0;i<static_cast<int>(ref.rate*1.5);++i)
        {
            const auto phase=6.28318530717958647692*ref.hz*i/ref.rate;
            const auto y=filter.next(std::cos(phase));
            if(i>=static_cast<int>(ref.rate/2)) sum+=y*std::polar(1.0,-phase);
        }
        forbidAllocation=false;
        const auto error=std::abs(2.0*sum/ref.rate-std::complex<double>(ref.real,ref.imag));
        maximumError=std::max(maximumError,error);
        require(error<2e-8,"magnitude AND phase match independent elliptic reference at six rates");
    }
    for(const int rate:{44100,48000,88200,96000,176400,192000})
    {
        dsp_core::EllipticHighpass filter,replay; filter.prepare(350,rate); replay.prepare(350,rate);
        constexpr std::size_t size=65536;
        std::vector<float> impulse(size);
        forbidAllocation=true;
        for(std::size_t i=0;i<size;++i)
        {
            const auto x=i==0 ? 1.0:0.0,y=filter.next(x);
            require(y==replay.next(x),"independent instances replay exactly");
            require(std::isfinite(y) && std::fpclassify(y)!=FP_SUBNORMAL,"impulse response is finite/normal");
            impulse[i]=static_cast<float>(y); maximumPeak=std::max(maximumPeak,std::abs(y));
        }
        forbidAllocation=false;
        const auto bins=dsp_core::test::spectrum(impulse);
        for(std::size_t bin=0;bin<=size/2;++bin)
        {
            const auto hz=bin*static_cast<double>(rate)/size,amplitude=std::abs(bins[bin]);
            if(hz<=250) maximumStop=std::max(maximumStop,amplitude);
            if(hz>=350){minimumPass=std::min(minimumPass,amplitude);maximumPass=std::max(maximumPass,amplitude);}
        }
        filter.reset(); replay.reset();
        std::vector<double> reference(10000);
        for(std::size_t i=0;i<reference.size();++i) reference[i]=filter.next(std::sin(i*0.137)+0.3*std::cos(i*0.013));
        constexpr std::array<int,7> sizes{1,73,512,7,2048,31,3};
        std::size_t at=0,block=0;
        forbidAllocation=true;
        while(at<reference.size())
        {
            const auto end=std::min(reference.size(),at+static_cast<std::size_t>(sizes[block++%sizes.size()]));
            for(;at<end;++at) require(replay.next(std::sin(at*0.137)+0.3*std::cos(at*0.013))==reference[at],"arbitrary/changing blocks are bit exact");
        }
        for(int i=0;i<rate*2;++i) require(std::isfinite(filter.next(0)),"long decay remains finite");
        require(filter.next(0)==0,"ringing decays to exact normal silence without a denormal guard");
        filter.reset(); require(filter.next(0)==0,"reset clears all sections");
        forbidAllocation=false;
    }
    require(maximumStop<0.0010001,"at least 60 dB rejection throughout 0-250 Hz");
    require(minimumPass>std::pow(10.0,-0.10001/20.0) && maximumPass<1.000001,"350 Hz and above retain <=0.1 dB ripple, unity ceiling");
    for(const auto rate:{0.0,384000.0,std::numeric_limits<double>::quiet_NaN()})
        for(const auto hz:{-1.0,1e30,std::numeric_limits<double>::infinity()})
        {
            dsp_core::EllipticHighpass filter; filter.prepare(hz,rate);
            for(int i=0;i<10000;++i)
                require(std::isfinite(filter.next(i%23==0 ? std::numeric_limits<double>::quiet_NaN():std::cos(i*0.17))),"invalid controls/input are contained");
        }
    std::cout<<"elliptic_guard_rates=6 oracle_cases="<<dsp_core::test::guardReferences.size()<<" complex_error="<<maximumError
             <<" stop_max_db="<<20*std::log10(maximumStop)<<" pass_min_db="<<20*std::log10(minimumPass)
             <<" pass_max_db="<<20*std::log10(maximumPass)<<" impulse_peak="<<maximumPeak<<" allocations=0 replay=exact\n";
}
