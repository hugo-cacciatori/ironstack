#include "dsp/BassVoice.h"
#include "dsp/GuitarBus.h"
#include "support/PeriodicSpectrum.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <iomanip>
#include <iostream>
#include <locale>
#include <string_view>

namespace
{
constexpr int count = 32768;
constexpr double tau = 6.28318530717958647692, targetDb = -50.0;
struct Probe
{
    bool foundation;
    int rate, fundamentalBin, warmup;
    double wantedHz, hz, amount, amplitude, peak;
    dsp_core::test::PeriodicSpectrum spectrum;
};
Probe measure(bool foundation, int rate, double wantedHz, double amount, double amplitude)
{
    // Odd bins are coprime with this power-of-two FFT, reducing low-order alias
    // collisions with valid harmonic bins. Report actual rather than requested Hz.
    auto bin = static_cast<int>(std::round(wantedHz * count / rate));
    if (bin % 2 == 0) ++bin;
    const auto hz = static_cast<double>(rate) * bin / count;
    const int warmup = rate; // Settle envelopes, ramps and recursive filters for one second.
    ironstack::BassVoice bass;
    ironstack::GuitarBus swarm;
    if (foundation)
    {
        bass.setParameters(140.0, amount); bass.prepare(rate); bass.start(hz, 127);
    }
    else { swarm.setParameters(amount, 0.5); swarm.prepare(rate); }
    std::vector<float> audio(count);
    double peak = 0.0;
    for (int i = -warmup; i < count; ++i)
    {
        const auto input = amplitude * std::sin(tau * bin * i / count);
        const ironstack::BassExciter::Sources excitation{input, input};
        const auto sample = foundation ? bass.next(1.0, ironstack::BassComponent::all, &excitation).output
                                       : swarm.next(input);
        if (!std::isfinite(sample)) throw std::runtime_error("probe generated nonfinite output");
        if (i >= 0)
        {
            audio[static_cast<std::size_t>(i)] = static_cast<float>(sample);
            peak = std::max(peak, std::abs(sample));
        }
    }
    return {foundation, rate, bin, warmup, wantedHz, hz, amount, amplitude, peak,
            dsp_core::test::measurePeriodic(audio, bin)};
}
}
int main(int argc, char** argv)
{
    if (argc != 1)
    {
        if (argc == 2 && std::string_view(argv[1]) == "--help")
        {
            std::cout << "alias_tool: fixed in-memory controlled drive probe; JSON on stdout.\n"
                "Exit 0 means measurement completed, not alias acceptance. No audio files are written.\n";
            return 0;
        }
        std::cerr << "Usage: alias_tool [--help]\n"; return 2;
    }
    juce::ScopedNoDenormals noDenormals;
    try
    {
        std::vector<Probe> probes;
        probes.reserve(240);
        for (const bool foundation : {true, false})
        for (const int rate : {44100, 48000, 88200, 96000, 176400, 192000})
        for (const double wanted : (foundation ? std::array<double, 5>{1100,2200,3550,5500,6200}
                                               : std::array<double, 5>{2200,3550,5500,7100,11000}))
        for (const double amount : {0.5, 1.0}) for (const double amplitude : {0.2, 0.8})
            probes.push_back(measure(foundation, rate, wanted, amount, amplitude));
        int exceedingPeak = 0, exceedingTotal = 0;
        double worstPeak = -300.0, worstTotal = -300.0;
        for (const auto& probe : probes)
        {
            exceedingPeak += probe.spectrum.nonHarmonicPeakDb > targetDb ? 1 : 0;
            exceedingTotal += probe.spectrum.nonHarmonicTotalDb > targetDb ? 1 : 0;
            worstPeak = std::max(worstPeak, probe.spectrum.nonHarmonicPeakDb);
            worstTotal = std::max(worstTotal, probe.spectrum.nonHarmonicTotalDb);
        }
        std::cout.imbue(std::locale::classic());
        std::cout << std::setprecision(17) << "{\n  \"sourceCommit\": \"" << IRONSTACK_COMMIT
            << "\", \"configuredDirty\": " << (IRONSTACK_DIRTY ? "true" : "false")
            << ",\n  \"scope\": \"controlled periodic excitation through current Foundation output and one Swarm bus\","
            "\n  \"limitations\": \"not actual moving/noisy sources, chords or continuous sweep; DC/Nyquist and aliases colliding with valid harmonic bins excluded; not full-instrument acceptance\","
            "\n  \"fftSamples\": " << count << ", \"window\": \"rectangular coherent tone; no padding\","
            "\n  \"targetDbc\": " << targetDb << ", \"cases\": " << probes.size()
            << ", \"peakCasesAboveTarget\": " << exceedingPeak << ", \"totalCasesAboveTarget\": " << exceedingTotal
            << ", \"worstPeakDbc\": " << worstPeak << ", \"worstTotalDbc\": " << worstTotal
            << ", \"fullInstrumentAcceptance\": false,\n  \"probes\": [\n";
        for (std::size_t i = 0; i < probes.size(); ++i)
        {
            const auto& p = probes[i];
            std::cout << (i ? ",\n" : "") << "    {\"layer\":\"" << (p.foundation ? "Foundation" : "Swarm")
                << "\",\"sampleRate\":" << p.rate << ",\"requestedHz\":" << p.wantedHz
                << ",\"actualHz\":" << p.hz << ",\"fundamentalBin\":" << p.fundamentalBin
                << ",\"warmupSamples\":" << p.warmup << ",\"destruction\":" << p.amount
                << ",\"inputAmplitude\":" << p.amplitude << ",\"outputPeak\":" << p.peak
                << ",\"strongestHarmonicAmplitude\":" << p.spectrum.strongestHarmonicAmplitude
                << ",\"peakNonHarmonicBin\":" << p.spectrum.peakBin
                << ",\"peakNonHarmonicHz\":" << static_cast<double>(p.spectrum.peakBin) * p.rate / count
                << ",\"peakDbc\":" << p.spectrum.nonHarmonicPeakDb
                << ",\"totalDbc\":" << p.spectrum.nonHarmonicTotalDb << '}';
        }
        std::cout << "\n  ]\n}\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
