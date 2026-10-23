#include "dsp/Engine.h"
#include <juce_core/juce_core.h>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
struct Event { int frame, kind, channel, note, value; };
double maximumLevelError = 0.0, maximumPeak = 0.0;
double minimumGain = 10.0, maximumGain = 0.0;
void compare(const juce::String& id, const std::vector<Event>& events, int frames, int rate, int begin = 0)
{
    ironstack::Engine stereo, mono;
    mono.setGuitarMono(true);
    stereo.prepare(rate); mono.prepare(rate);
    std::array<float, 512> left{}, right{}, centeredLeft{}, centeredRight{}, bass{}, monoBass{}, mixedL{}, mixedR{};
    double reference = 0.0, actual = 0.0;
    int at = 0;
    std::size_t event = 0;
    while (at < frames)
    {
        while (event < events.size() && events[event].frame == at)
        {
            const auto& e = events[event++];
            for (auto* engine : {&stereo, &mono})
            {
                if (e.kind == 0) engine->noteOn(e.channel, e.note, e.value);
                if (e.kind == 1) engine->noteOff(e.channel, e.note);
                if (e.kind == 2) engine->pitchBend(e.channel, e.value);
            }
        }
        const auto next = event < events.size() ? events[event].frame : frames;
        const auto count = std::min({512, frames - at, next - at});
        require(count > 0, "valid ordered fixture events");
        stereo.process(mixedL.data(), mixedR.data(), count, ironstack::LayerMask::both,
                       {bass.data(), nullptr, left.data(), right.data()});
        mono.process(mixedL.data(), mixedR.data(), count, ironstack::LayerMask::both,
                     {monoBass.data(), nullptr, centeredLeft.data(), centeredRight.data()});
        minimumGain = std::min(minimumGain, mono.guitarMonoGain());
        maximumGain = std::max(maximumGain, mono.guitarMonoGain());
        require(minimumGain > 0.502 && maximumGain < 1.994, "real synthesis never needs the compensation bounds");
        for (int i = 0; i < count; ++i)
        {
            require(centeredLeft[i] == centeredRight[i], "mono guitar channels are exactly equal from startup");
            require(bass[i] == monoBass[i], "Mono never changes bass audio/routing");
            require(std::isfinite(mixedL[i]) && std::abs(mixedL[i]) < 1.0f
                    && std::isfinite(mixedR[i]) && std::abs(mixedR[i]) < 1.0f, "full-velocity compensated mix stays finite/unclipped");
            maximumPeak = std::max({maximumPeak, std::abs(static_cast<double>(mixedL[i])), std::abs(static_cast<double>(mixedR[i]))});
            if (at + i >= begin)
            {
                reference += 0.5 * (left[i] * static_cast<double>(left[i]) + right[i] * static_cast<double>(right[i]));
                actual += centeredLeft[i] * static_cast<double>(centeredLeft[i]);
            }
        }
        at += count;
    }
    require(reference > 1.0e-10 && actual > 1.0e-10, "comparison contains audible numerical signal");
    const auto error = std::abs(10.0 * std::log10(actual / reference));
    maximumLevelError = std::max(maximumLevelError, error);
    std::cout << id << " mono_rate=" << rate << " rms_error_db=" << error << std::endl;
    require(error <= 1.0, "Mono integrated RMS stays within 1 dB of stereo");
}
void transitions()
{
    ironstack::Engine reference, toggled;
    reference.prepare(48000); toggled.prepare(48000);
    for (auto* engine : {&reference, &toggled})
        for (auto note : {40, 47, 52}) engine->noteOn(1, note, 127);
    float previousL = 0.0f, previousR = 0.0f;
    double step = 0.0;
    double controlStep = 0.0, previousBlend = 0.0, previousGain = 1.0;
    for (int i = 0; i < 96000; ++i)
    {
        const bool on = (i / 2400) % 2 != 0;
        toggled.setGuitarMono(on); // Repeated host values must not restart fades.
        float left, right, a, b, bassA, bassB;
        reference.process(&a, &b, 1, ironstack::LayerMask::guitar, {&bassA});
        toggled.process(&left, &right, 1, ironstack::LayerMask::guitar, {&bassB});
        // Compare the actual output against today's audio with yesterday's
        // mode/gain. This isolates control movement from natural saw attacks.
        const auto heldLeft = a + previousBlend * (a * previousGain - a);
        const auto heldRight = b + previousBlend * (a * previousGain - b);
        controlStep = std::max({controlStep, std::abs(left - heldLeft), std::abs(right - heldRight)});
        const auto phase = std::min(1.0, (i % 2400) / 240.0);
        previousBlend = i < 2400 ? 0.0 : (on ? phase : 1.0 - phase);
        previousGain = toggled.guitarMonoGain();
        require(bassA == bassB, "live Mono transitions preserve bass exactly");
        if (i % 2400 > 241)
        {
            if (on) require(left == right, "settled live Mono is centered");
            else require(left == a && right == b, "stereo resumes exactly with no synthesis reset/lost voices");
        }
        step = std::max({step, std::abs(static_cast<double>(left - previousL)), std::abs(static_cast<double>(right - previousR))});
        previousL = left; previousR = right;
    }
    // Natural waveform slope scales with the final mix gain. Compare its shape
    // at the original output level; keep the actual control-click bound below.
    const auto referenceStep = step * (0.088 * ironstack::outputTrim) / ironstack::GuitarBus::outputGain;
    std::cout << "mono_transition_step=" << step << " reference_level_step=" << referenceStep
              << " control_step=" << controlStep << std::endl;
    require(referenceStep < 0.04, "live fades retain bounded waveform shape at the reference level");
    require(controlStep < 0.002, "Mono adds no large control-induced step or hard-right transient");
    for (const auto& target : toggled.guitarTargets())
        if (target.active) require(target.held, "Mono toggles preserve held voices");
}
}
int main(int argc, char** argv)
{
    if (argc == 2 && juce::String(argv[1]) == "--transitions") { transitions(); return 0; }
    if (argc != 2) return 2;
    const auto spec = juce::JSON::parse(juce::File(argv[1]).loadFileAsString());
    for (int rate : {44100, 48000, 88200, 96000, 176400, 192000})
    {
        const auto samplesPerBeat = rate * 60.0 / static_cast<double>(spec["tempo_bpm"]);
        for (const auto& sequence : *spec["sequences"].getArray())
        {
            std::vector<Event> events;
            for (const auto& row : *sequence["notes"].getArray())
            {
                const auto start = static_cast<double>(row[0]);
                events.push_back({static_cast<int>(std::llround(start * samplesPerBeat)), 0, static_cast<int>(row[2]), static_cast<int>(row[3]), static_cast<int>(row[4])});
                events.push_back({static_cast<int>(std::llround((start + static_cast<double>(row[1])) * samplesPerBeat)), 1, static_cast<int>(row[2]), static_cast<int>(row[3]), 0});
            }
            if (const auto* bends = sequence["bends"].getArray())
                for (const auto& row : *bends) events.push_back({static_cast<int>(std::llround(static_cast<double>(row[0]) * samplesPerBeat)), 2, static_cast<int>(row[1]), 0, static_cast<int>(row[2])});
            std::stable_sort(events.begin(), events.end(), [](const auto& a, const auto& b) { return a.frame < b.frame; });
            compare(sequence["id"].toString(), events, static_cast<int>(std::llround(static_cast<double>(sequence["length_beats"]) * samplesPerBeat + rate * 0.1)), rate);
        }
        for (const auto& notes : {std::vector<int>{24}, {48}, {69}, {127}, {40,47,52}, {24,31,36,40,43,48,52,55}})
        {
            std::vector<Event> events;
            juce::String id = "hold";
            for (auto note : notes) { events.push_back({0, 0, 1, note, 127}); id += "-" + juce::String(note); }
            compare(id, events, rate * 6, rate, rate * 2);
        }
    }
    transitions();
    std::cout << "mono_level_cases=66 max_rms_error_db=" << maximumLevelError << " mixed_peak=" << maximumPeak
              << " gain_min=" << minimumGain << " gain_max=" << maximumGain << '\n';
}
