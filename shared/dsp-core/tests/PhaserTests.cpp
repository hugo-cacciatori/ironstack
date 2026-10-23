#include <dsp_core/Phaser.h>
#include <array>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>

namespace
{
bool forbidAllocation = false;
constexpr double pi = 3.14159265358979323846;
void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}
void* operator new(std::size_t size)
{
    if (forbidAllocation) std::abort();
    if (auto* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

int main()
{
    double worstResponse = 0.0, maximum = 0.0, motionDb = 0.0;
    for (const auto rate : {44100, 48000, 88200, 96000, 176400, 192000})
    {
        // A fixed all-pass cascade must conserve impulse energy; its parallel
        // blend must match the independently evaluated transfer function.
        dsp_core::Phaser wet, blend;
        wet.prepare(rate, 350, 2300, 0); blend.prepare(rate, 350, 2300, 0);
        constexpr std::array<double, 5> frequencies{100, 350, 700, 1400, 5000};
        std::array<std::complex<double>, 5> sums{};
        double energy = 0.0;
        forbidAllocation = true;
        for (int i = 0; i < rate; ++i)
        {
            const auto input = i == 0 ? 1.0 : 0.0;
            const auto allpass = wet.next(input, 1.0);
            const auto parallel = blend.next(input, 0.5);
            energy += allpass * allpass;
            for (std::size_t j = 0; j < frequencies.size(); ++j)
                sums[j] += parallel * std::polar(1.0, -2*pi*frequencies[j]*i/rate);
        }
        forbidAllocation = false;
        require(std::abs(energy-1.0) < 1e-10, "static all-pass conserves energy");
        for (std::size_t j = 0; j < frequencies.size(); ++j)
        {
            std::complex<double> response{1, 0};
            for (const auto ratio : {0.5, 0.7, 1.0, 1.4, 2.0, 2.8})
            {
                const auto r = std::tan(pi*frequencies[j]/rate) / std::tan(pi*350*ratio/rate);
                response *= std::complex<double>(1, -r) / std::complex<double>(1, r);
            }
            const auto error = std::abs(sums[j] - (1.0+response)*0.5);
            worstResponse = std::max(worstResponse, error);
            require(error < 1e-9, "parallel notches match analytical magnitude and phase");
        }
        dsp_core::Phaser moving, bypass, replay;
        moving.prepare(rate); bypass.prepare(rate); replay.prepare(rate);
        for (int i = 0; i < 4096; ++i)
            require(replay.next(0.0, 0.5, false) == 0.0, "silent parked modulation stays silent");
        std::array<double, 16> power{};
        forbidAllocation = true;
        for (int i = 0; i < rate*8; ++i)
        {
            const auto input = std::sin(2*pi*700*i/rate);
            const auto value = moving.next(input, 0.5);
            require(value == replay.next(input, 0.5), "reset/replay deterministic");
            require(bypass.next(input, 0.0) == input, "zero blend is exact bypass");
            maximum = std::max(maximum, std::abs(value));
            require(std::isfinite(value) && std::fpclassify(value) != FP_SUBNORMAL && std::abs(value) < 1.1,
                    "moving phaser stays finite, normal and bounded");
            power[static_cast<std::size_t>(i/(rate/2))] += value*value;
        }
        moving.reset(); bypass.reset();
        for (int i = 0; i < rate; ++i)
        {
            const auto input = std::sin(2*pi*1800*i/rate);
            require(moving.next(input, 0.5) == bypass.next(input, 0.5), "reset clears phase and state");
        }
        forbidAllocation = false;
        const auto bounds = std::minmax_element(power.begin()+1, power.end());
        motionDb = 10*std::log10(*bounds.second / *bounds.first);
        require(motionDb > 3.0, "moving notches produce measurable sustained-tone modulation");
        moving.prepare(std::numeric_limits<double>::quiet_NaN(), -1, 1e9, 1e9);
        for (int i = 0; i < 100000; ++i)
            require(std::isfinite(moving.next(i % 2 ? 1.0 : std::numeric_limits<double>::infinity(),
                                            i % 3 ? 1e9 : std::numeric_limits<double>::quiet_NaN())),
                    "invalid controls/input sanitized");
    }
    std::cout << "phaser_complex_error=" << worstResponse << " moving_peak=" << maximum
              << " last_rate_motion_db=" << motionDb << " rates=6 allocation_free=yes\n";
}
