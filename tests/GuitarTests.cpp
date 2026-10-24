#include "dsp/Engine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
constexpr double pi = 3.14159265358979323846;
void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
struct Audio
{
    std::vector<float> left, right;
    explicit Audio(int n) : left(static_cast<std::size_t>(n)), right(left.size()) {}
};
Audio render(ironstack::Engine& engine, int count, int block = 512)
{
    Audio audio(count);
    for (int at = 0; at < count; at += block)
        engine.process(audio.left.data() + at, audio.right.data() + at, std::min(block, count - at), ironstack::LayerMask::guitar);
    return audio;
}
double magnitude(const std::vector<float>& samples, double frequency)
{
    std::complex<double> sum{};
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        const auto window = 0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(i) / (samples.size() - 1));
        sum += static_cast<double>(samples[i]) * window * std::polar(1.0, -2.0 * pi * frequency * static_cast<double>(i) / 48000.0);
    }
    return 4.0 * std::abs(sum) / static_cast<double>(samples.size());
}
void spectrum()
{
    ironstack::Engine engine; engine.prepare(48000); engine.noteOn(1,69,127);
    render(engine,4800);
    const auto audio=render(engine,192000);
    require(audio.left!=audio.right,"diffuse sides are synthesized independently");
    double ll=0,rr=0,lr=0;
    for(std::size_t i=0;i<audio.left.size();++i)
    { ll+=audio.left[i]*static_cast<double>(audio.left[i]); rr+=audio.right[i]*static_cast<double>(audio.right[i]); lr+=audio.left[i]*static_cast<double>(audio.right[i]); }
    const auto rms=std::sqrt(ll/audio.left.size());
    require(rms>0.01 && rms<0.3,"Swarm sustains substantial diffuse body");
    require(magnitude(audio.left,440)<0.02*rms && magnitude(audio.right,440)<0.02*std::sqrt(rr/audio.right.size()),
            "played pitch is not a persistent carrier in the processed source");
    std::cout<<"guitar_diffuse_rms="<<rms<<" correlation="<<lr/std::sqrt(ll*rr)<<'\n';
}
void targets()
{
    ironstack::Engine engine;
    for (int note = 0; note < 128; ++note)
    {
        engine.prepare(48000);
        engine.noteOn(3, note, 127);
        int count = 0;
        for (const auto& target : engine.guitarTargets())
            if (target.active)
            {
                ++count;
                require(target.note == note && target.channel == 3 && target.held
                        && std::abs(target.frequency - 440.0 * std::exp2((note - 69.0) / 12.0)) < 1.0e-9,
                        "every input MIDI pitch has exactly one unchanged-pitch guitar note");
            }
        require(count == 1, "unison components do not consume playable note voices");
    }
    engine.prepare(48000);
    for (int note = 60; note < 68; ++note) engine.noteOn(1, note, 127);
    engine.noteOn(1, 80, 127);
    engine.noteOff(1, 60);
    auto current = engine.guitarTargets();
    require(std::count_if(current.begin(), current.end(), [](const auto& t) { return t.active && t.held; }) == 8,
            "ninth note steals oldest, stale release preserves all eight replacement notes");
    require(std::none_of(current.begin(), current.end(), [](const auto& t) { return t.active && t.note == 60; }),
            "oldest held note was stolen");
    for (int note = 61; note < 68; ++note) engine.noteOff(1, note);
    engine.noteOff(1, 80);
    const auto released = render(engine, 2000);
    require(std::abs(released.left.back()) < 1.0e-8f && std::abs(released.right.back()) < 1.0e-8f,
            "both guitar sides release including filter tails");
}
void bentAudio()
{
    ironstack::Engine original, otherChannel;
    for (auto* engine : {&original, &otherChannel}) { engine->prepare(48000); engine->noteOn(1, 69, 127); }
    otherChannel.pitchBend(2, 16383);
    const auto a = render(original, 4800), b = render(otherChannel, 4800, 73);
    require(a.left == b.left && a.right == b.right, "another channel's bend leaves both guitar sides unchanged");
    otherChannel.pitchBend(1,16383);
    const auto continued=render(original,48000), bent=render(otherChannel,48000);
    require(continued.left!=bent.left && continued.right!=bent.right,"owned bend changes broad spectral contour");
    const auto frequency=440.0*std::exp2(2.0/12.0);
    bool found=false;
    for(const auto& target:otherChannel.guitarTargets())
        if(target.active) { found=true; require(std::abs(target.frequency-frequency)<1e-9,"bent MIDI target remains exact"); }
    require(found,"bend retains its playable voice");
}
void repeatability()
{
    ironstack::Engine a, b, reference;
    for (auto* engine : {&a, &b, &reference})
    {
        engine->setGuitarParameters(0.0f); engine->prepare(48000);
    }
    a.noteOn(1, 60, 127); a.noteOn(1, 69, 127); // 69 gets slot 1.
    b.noteOn(1, 69, 127); reference.noteOn(1, 60, 127); // Both get slot 0.
    const auto chord = render(a, 48000, 1), single = render(b, 48000, 73), other = render(reference, 48000, 512);
    for (std::size_t i = 0; i < chord.left.size(); ++i)
        // Three final float roundings scale with the signal's magnitude.
        // A fixed 2e-8 bound can be smaller than one ULP of a broad source.
        require(std::abs(static_cast<double>(chord.left[i]) - single.left[i] - other.left[i])
                    <= std::numeric_limits<float>::epsilon()*(std::abs(chord.left[i])+std::abs(single.left[i])+std::abs(other.left[i]))+1e-12
                && std::abs(static_cast<double>(chord.right[i]) - single.right[i] - other.right[i])
                    <= std::numeric_limits<float>::epsilon()*(std::abs(chord.right[i])+std::abs(single.right[i])+std::abs(other.right[i]))+1e-12,
                "at zero drive the filtered chord is the sum of slot-independent notes");
    a.prepare(48000); b.prepare(48000);
    for (auto* engine : {&a, &b})
    {
        engine->noteOn(1, 40, 127); engine->noteOn(2, 64, 90); engine->pitchBend(2, 15000);
    }
    const auto whole = render(a, 48000, 48000), split = render(b, 48000, 73);
    require(whole.left == split.left && whole.right == split.right, "both stereo channels repeat exactly across callback sizes");
    b.reset(); b.noteOn(1, 40, 127); b.noteOn(2, 64, 90); b.pitchBend(2, 15000);
    const auto reset = render(b, 48000, 1);
    require(reset.left == whole.left && reset.right == whole.right, "reset reproduces both channels exactly");
}
}
int main()
{
    targets(); spectrum(); bentAudio(); repeatability();
    std::cout << "guitar_notes=8 pitches=0..127 allocation=deterministic stereo_repeat=exact\n";
}
