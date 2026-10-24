#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <dsp_core/Butterworth.h>
#include <dsp_core/SmoothChaos.h>

namespace ironstack
{
// Quiet unpitched texture above the separate protected low sine. A strong
// harmonic body made the foundation read as an organ rather than low weight.
class BassExciter
{
public:
    struct Sources { double dry = 0.0, unison = 0.0; };
    struct Parts { Sources body, texture; };
    static constexpr double textureGain = 0.01; // -40 dB before drive, against the fixed 0.20 reference scale.
    void prepare(double rate) noexcept
    {
        for (auto& filter : bodyBand) filter.prepare(dsp_core::Butterworth::Type::lowpass, 2200.0, rate);
        for (auto& filter : edgeBand) filter.prepare(dsp_core::Butterworth::Type::lowpass, 5200.0, rate);
        reset();
    }
    void reset() noexcept
    {
        body.reset(0.137619); edge.reset(0.731293);
        for (auto& filter : bodyBand) filter.reset();
        for (auto& filter : edgeBand) filter.reset();
    }
    double next(double hz, double rate, double amount) noexcept { return nextSources(hz, rate, amount).unison; }
    Sources nextSources(double hz, double rate, double amount) noexcept
    {
        const auto parts = nextParts(hz, rate, amount);
        return {parts.body.dry + parts.texture.dry, parts.body.unison + parts.texture.unison};
    }
    // Diagnostic decomposition before drive; no exposed sound control.
    Parts nextParts(double hz, double rate, double amount) noexcept
    {
        const auto root = std::isfinite(hz) ? std::clamp(hz, 1.0, 24000.0) : 55.0;
        // Root changes bandwidth/roughness gently, never creates a pitched
        // oscillator. Cubic target transitions and two LP sections per stream
        // keep the chaotic texture coloured and remove sharp clock edges.
        const auto speed = std::clamp(1.0 + 0.08 * std::log2(root / 55.0), 0.7, 1.4);
        auto low = body.next(1733.0 * speed, rate);
        auto high = edge.next(5171.0 * speed, rate);
        for (auto& filter : bodyBand) low = filter.next(low);
        for (auto& filter : edgeBand) high = filter.next(high);
        const auto edgeShare = 0.25 + 0.25 * std::clamp(2.0 * amount, 0.0, 1.0);
        const auto dry = 0.20 * textureGain * (0.75 * low + 0.25 * high);
        const auto wet = 0.20 * textureGain * ((1.0 - edgeShare) * low + edgeShare * high);
        // Preserve the diagnostic decomposition, with the rejected pitched
        // body absent. The existing quiet texture and its level stay intact.
        return {{0.0, 0.0}, {dry, wet}};
    }
private:
    dsp_core::SmoothChaos body, edge;
    std::array<dsp_core::Butterworth, 2> bodyBand, edgeBand;
};
}
