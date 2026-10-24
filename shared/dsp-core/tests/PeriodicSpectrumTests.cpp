#include "support/PeriodicSpectrum.h"
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
constexpr double tau = 6.28318530717958647692;
void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
template<class Function> void rejects(Function function)
{
    try { function(); }
    catch (const std::invalid_argument&) { return; }
    require(false, "invalid periodic measurement input rejected");
}
}
int main()
{
    constexpr int count = 4096, fundamental = 151, spur = 643;
    std::vector<float> clean(count);
    for (int i = 0; i < count; ++i)
        clean[static_cast<std::size_t>(i)] = static_cast<float>(0.2 + 0.8 * std::sin(tau * fundamental * i / count)
            + 0.2 * std::cos(tau * 2 * fundamental * i / count) + 0.1 * std::sin(tau * 3 * fundamental * i / count));
    const auto quiet = dsp_core::test::measurePeriodic(clean, fundamental);
    require(std::abs(quiet.strongestHarmonicAmplitude - 0.8) < 1.0e-6, "harmonic amplitude calibrated independently");
    require(quiet.nonHarmonicTotalDb < -130.0, "DC and intentional even/odd harmonics do not count as residual");
    for (const auto db : {-40.0, -60.0})
    {
        auto audio = clean;
        for (int i = 0; i < count; ++i)
            audio[static_cast<std::size_t>(i)] += static_cast<float>(0.8 * std::pow(10.0, db / 20.0) * std::sin(tau * spur * i / count));
        const auto measured = dsp_core::test::measurePeriodic(audio, fundamental);
        require(measured.peakBin == spur && std::abs(measured.nonHarmonicPeakDb - db) < 0.002
            && std::abs(measured.nonHarmonicTotalDb - db) < 0.002, "known spur amplitude and power recovered");
    }
    // A deliberately aliased third harmonic must be detected below Nyquist.
    constexpr int highFundamental = 1151, foldedThird = count - 3 * highFundamental;
    std::vector<float> folded(count);
    for (int i = 0; i < count; ++i)
        folded[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(tau * highFundamental * i / count)
            + 0.001 * std::sin(tau * 3 * highFundamental * i / count));
    const auto alias = dsp_core::test::measurePeriodic(folded, highFundamental);
    require(alias.peakBin == foldedThird && std::abs(alias.nonHarmonicPeakDb + 60.0) < 0.002,
            "known above-Nyquist harmonic folds to the expected measured bin");
    rejects([&] { dsp_core::test::measurePeriodic({}, fundamental); });
    rejects([&] { dsp_core::test::measurePeriodic(std::vector<float>(127), 3); });
    rejects([&] { dsp_core::test::measurePeriodic(clean, 0); });
    rejects([&] { dsp_core::test::measurePeriodic(clean, count / 2); });
    rejects([&] { dsp_core::test::measurePeriodic(std::vector<float>(count), fundamental); });
    clean[3] = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { dsp_core::test::measurePeriodic(clean, fundamental); });
    std::cout << "Periodic spectrum: calibrated harmonics, -40/-60 dBc spurs, folded harmonic and invalid inputs passed.\n";
}
