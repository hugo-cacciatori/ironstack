#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <dsp_core/DiffuseNoise.h>
#include <dsp_core/LinearRamp.h>

namespace ironstack
{
// One diffuse cloud per played note, with independent side excitation and a
// modest common-energy share. MIDI controls broad colour, never a carrier.
class GuitarOscillators
{
public:
    struct Stereo { double left = 0.0, right = 0.0; };
    static constexpr double spread = 0.84;
    static constexpr double commonPower = 0.15;
    void prepare(double rate) noexcept
    {
        rate = std::isfinite(rate) && rate >= 44100.0 && rate <= 192000.0 ? rate : 44100.0;
        for (auto& source : sources) source.prepare(rate);
        smoothingSamples = static_cast<int>(std::ceil(rate * 0.005));
        gainFrequency = -1.0;
        sourceLevel.reset(0.6582);
    }
    void reset(int channel, int note) noexcept
    {
        const auto key = (static_cast<std::uint64_t>(channel) << 32) ^ static_cast<std::uint64_t>(note);
        const auto hz = 440.0 * std::exp2((note - 69.0) / 12.0);
        constexpr std::array<std::uint64_t, 3> salts{
            UINT64_C(0x535741524d434f4d), UINT64_C(0x535741524d4c4546), UINT64_C(0x535741524d524947)};
        for (std::size_t i = 0; i < sources.size(); ++i)
        {
            sources[i].setRegister(hz);
            sources[i].reset(key ^ salts[i]);
        }
        gainFrequency = hz;
        sourceLevel.reset(registerGain(hz));
    }
    Stereo next(double hz) noexcept
    {
        hz = std::isfinite(hz) ? hz : 220.0;
        if (hz != gainFrequency)
        {
            gainFrequency = hz;
            sourceLevel.setTarget(registerGain(hz), smoothingSamples);
        }
        for (auto& source : sources) source.setRegister(hz);
        const auto common = std::sqrt(commonPower) * sources[0].next();
        const auto independent = std::sqrt(1.0 - commonPower);
        const auto level = sourceLevel.next();
        return {level * (common + independent * sources[1].next()),
                level * (common + independent * sources[2].next())};
    }
private:
    static double registerGain(double hz) noexcept
    {
        // Two-octave anchors match the saved low/mid levels. Bound the upper
        // boost to retain dense-chord headroom. This colours register loudness,
        // never creates a carrier or changes velocity/envelope behaviour.
        constexpr double low = 82.4068892282175, mid = 329.6275569128699, high = 1318.5102276514797;
        if (hz <= low) return 0.2623;
        if (hz >= high) return 1.4;
        if (hz <= mid) return 0.2623 * std::pow(0.6582 / 0.2623, 0.5 * std::log2(hz / low));
        return 0.6582 * std::pow(1.4 / 0.6582, 0.5 * std::log2(hz / mid));
    }
    std::array<dsp_core::DiffuseNoise, 3> sources;
    dsp_core::LinearRamp sourceLevel;
    double gainFrequency = -1.0;
    int smoothingSamples = 240;
};
}
