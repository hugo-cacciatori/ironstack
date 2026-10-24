#pragma once

#include "Butterworth.h"
#include "LinearRamp.h"
#include "SmoothChaos.h"
#include <array>
#include <cstdint>

namespace dsp_core
{
// Aperiodic excitation, not an oscillator with added noise. Register controls
// broad colour weights only: there is no note-period clock or resonant ladder.
// Envelope and layer-specific separation filters belong to the caller.
class DiffuseNoise
{
public:
    void prepare(double sampleRate) noexcept
    {
        rate = std::isfinite(sampleRate) && sampleRate >= 44100.0 && sampleRate <= 192000.0
            ? sampleRate : 44100.0;
        constexpr std::array<double, 3> low{100.0, 550.0, 3200.0};
        constexpr std::array<double, 3> high{1600.0, 6000.0, 13000.0};
        for (std::size_t i = 0; i < bands.size(); ++i)
        {
            bands[i].hp.prepare(Butterworth::Type::highpass, low[i], rate);
            // Two Butterworth sections, with fourth-order Qs, roll off the
            // stochastic source before Nyquist. No clipping/nonlinear audio.
            bands[i].lp1.prepare(Butterworth::Type::lowpass, high[i], rate, 0.541196100146197);
            bands[i].lp2.prepare(Butterworth::Type::lowpass, high[i], rate, 1.306562964876377);
        }
        // White-noise power in a fixed-Hz band scales inversely with rate.
        rateGain = std::sqrt(rate / 48000.0);
        reset(0);
    }

    void reset(std::uint64_t seed) noexcept
    {
        auto state = seed;
        for (auto& band : bands)
        {
            band.state = randomBits(state);
            band.hp.reset(); band.lp1.reset(); band.lp2.reset();
        }
        motion.reset(0.1 + 0.7 * static_cast<double>(randomBits(state) >> 11) / 9007199254740992.0);
        for (std::size_t i = 0; i < weights.size(); ++i) weights[i].reset(targets[i]);
    }

    // Call with the exact MIDI/bend target; a repeated target does not restart
    // the 5 ms ramp. Its broad contour never follows harmonic frequencies.
    void setRegister(double hz) noexcept
    {
        const auto safe = std::clamp(std::isfinite(hz) ? hz : 220.0, 8.0, 25000.0);
        const auto colour = std::clamp(std::log2(safe / 55.0) / 6.0, 0.0, 1.0);
        const std::array<double, 3> next{1.6 - 0.8 * colour, 0.65 + 0.15 * colour, 0.20 + 0.35 * colour};
        if (next == targets) return;
        targets = next;
        for (std::size_t i = 0; i < weights.size(); ++i)
            weights[i].setTarget(targets[i], static_cast<int>(rate * 0.005));
    }

    double next() noexcept
    {
        const auto movement = motion.next(2.3, rate);
        double sum = 0.0;
        for (std::size_t i = 0; i < bands.size(); ++i)
        {
            auto& band = bands[i];
            const auto white = 2.0 * static_cast<double>(randomBits(band.state) >> 11) / 9007199254740992.0 - 1.0;
            const auto value = band.lp2.next(band.lp1.next(band.hp.next(white)));
            // Gentle irregular colour motion, not an amplitude envelope or
            // a delayed copy. Independent bands maintain a continuous body.
            const auto colourMotion = i == 0 ? 1.0 + 0.15 * movement : 1.0 - 0.15 * movement;
            sum += weights[i].next() * colourMotion * value;
        }
        return rateGain * sum;
    }

private:
    // SplitMix64: defined unsigned wrap, full 64-bit state cycle, no library
    // distribution/global RNG. Seeds are supplied by note/channel/side owners.
    static std::uint64_t randomBits(std::uint64_t& state) noexcept
    {
        auto z = (state += UINT64_C(0x9e3779b97f4a7c15));
        z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
        return z ^ (z >> 31);
    }
    struct Band { Butterworth hp, lp1, lp2; std::uint64_t state = 0; };
    std::array<Band, 3> bands{};
    std::array<LinearRamp, 3> weights{};
    std::array<double, 3> targets{1.3333333333333333, 0.7, 0.31666666666666665};
    SmoothChaos motion;
    double rate = 48000.0, rateGain = 1.0;
};
}
