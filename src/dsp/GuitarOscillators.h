#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <dsp_core/DiffuseNoise.h>

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
    // Match the new broad spectrum's drive input statistics without changing
    // the accepted post-bus level. Raw/processed comparisons live in tests.
    static constexpr double sourceGain = 0.80;
    void prepare(double rate) noexcept { for (auto& source : sources) source.prepare(rate); }
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
    }
    Stereo next(double hz) noexcept
    {
        for (auto& source : sources) source.setRegister(hz);
        const auto common = std::sqrt(commonPower) * sources[0].next();
        const auto independent = std::sqrt(1.0 - commonPower);
        return {sourceGain * (common + independent * sources[1].next()),
                sourceGain * (common + independent * sources[2].next())};
    }
private:
    std::array<dsp_core::DiffuseNoise, 3> sources;
};
}
