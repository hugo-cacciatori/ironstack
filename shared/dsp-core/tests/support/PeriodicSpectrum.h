#pragma once
#include "StereoMetrics.h"

namespace dsp_core::test
{
// Offline coherent-tone analysis only. All valid integer harmonics are allowed,
// including even harmonics from asymmetric drive. Moving/noisy sources cannot
// use this mask to distinguish intended sidebands from aliasing.
struct PeriodicSpectrum
{
    double strongestHarmonicAmplitude = 0.0;
    double nonHarmonicPeakDb = -300.0, nonHarmonicTotalDb = -300.0;
    int peakBin = 0;
};
inline PeriodicSpectrum measurePeriodic(const std::vector<float>& input, int fundamentalBin)
{
    const auto size = input.size();
    if (size < 4 || (size & (size - 1)) != 0 || fundamentalBin < 1
        || fundamentalBin >= static_cast<int>(size / 2)
        || !std::all_of(input.begin(), input.end(), [](float v) { return std::isfinite(v); }))
        throw std::invalid_argument("finite power-of-two buffer and valid coherent fundamental required");
    const auto bins = spectrum(input);
    double strongest = 0.0, peak = 0.0, total = 0.0;
    PeriodicSpectrum result;
    // DC and exactly Nyquist are excluded; the tool reports this boundary.
    for (int bin = 1; bin < static_cast<int>(size / 2); ++bin)
    {
        const auto power = std::norm(bins[static_cast<std::size_t>(bin)]);
        if (bin % fundamentalBin == 0) strongest = std::max(strongest, power);
        else
        {
            total += power;
            if (power > peak) { peak = power; result.peakBin = bin; }
        }
    }
    if (!(strongest > 0.0)) throw std::invalid_argument("coherent harmonic reference is silent");
    result.strongestHarmonicAmplitude = 2.0 * std::sqrt(strongest) / static_cast<double>(size);
    result.nonHarmonicPeakDb = powerDb(peak / strongest);
    result.nonHarmonicTotalDb = powerDb(total / strongest);
    return result;
}
}
