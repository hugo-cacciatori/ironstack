#pragma once

#include <dsp_core/DiffuseNoise.h>

namespace ironstack
{
// The entire upper source is aperiodic. The protected sine is synthesized
// separately in BassVoice; no harmonic carrier remains in this path.
class BassExciter
{
public:
    struct Sources { double dry = 0.0, unison = 0.0; };
    // Match the old upper source's approximate raw RMS, independently of the
    // accepted layer gains and of the clean sine. Not the old quiet texture.
    static constexpr double bodyGain = 0.35;
    void prepare(double rate) noexcept { source.prepare(rate); reset(); }
    void reset() noexcept
    {
        // A canonical starting contour makes replay independent of the last
        // note/layer mode. nextSources smoothly moves it to the new register.
        source.setRegister(220.0);
        source.reset(UINT64_C(0x42415353424f4459));
    }
    double next(double hz, double rate, double amount) noexcept { return nextSources(hz, rate, amount).unison; }
    Sources nextSources(double hz, double, double) noexcept
    {
        source.setRegister(hz);
        const auto value = bodyGain * source.next();
        // Legacy field names describe the existing 55/45 processing paths.
        // Their distinct drive strengths remain; both receive the same body.
        return {value, value};
    }
private:
    dsp_core::DiffuseNoise source;
};
}
