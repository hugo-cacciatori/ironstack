#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <dsp_core/FixedEnvelope.h>
#include <dsp_core/PolyBlepSaw.h>
#include <dsp_core/LeftChannelMono.h>
#include "BassNotes.h"
#include "BassVoice.h"
#include "GuitarOscillators.h"
#include "GuitarBus.h"

namespace ironstack
{
// Diagnostic contribution masks, not user-facing routing/enables. All synthesis
// and MIDI state advances even when a contribution is omitted from the output.
enum class LayerMask : unsigned { none = 0, bass = 1, guitar = 2, both = 3 };
struct LayerTaps
{
    // Optional, caller-owned non-aliasing buffers of numSamples floats each.
    // These observe both contributions before the mask and master output gain.
    float* bassLeft = nullptr;
    float* bassRight = nullptr;
    float* guitarLeft = nullptr;
    float* guitarRight = nullptr;
};

// Eight stereo guitar notes plus monophonic bass. Explicit layer enables enter
// mode-aware routing; older diagnostic callers retain their locked-bass baseline.
// MIDI decoding stays in plugin; this interface has no JUCE/GUI dependency.
class Engine
{
public:
    static constexpr std::size_t guitarVoiceCount = 8;
    static constexpr double releaseSeconds = 0.006;
    static constexpr int latencySamples = BassVoice::latencySamples;
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setLayersEnabled(bool bass, bool guitar) noexcept;
    void setGuitarMono(bool mono) noexcept { guitarMono.setEnabled(mono); }
    double guitarMonoGain() const noexcept { return guitarMono.gain(); }
    void setBassParameters(float splitHz, float destruction) noexcept { bassVoice.setParameters(splitHz, destruction); }
    void setGuitarParameters(float destruction, float tight = 0.5f) noexcept
    {
        for (auto& bus : guitarBus) bus.setParameters(destruction, tight);
    }
    void noteOn(int channel, int note, int velocity) noexcept;
    void noteOff(int channel, int note) noexcept;
    void allNotesOff(int channel) noexcept;
    void pitchBend(int channel, int wheel) noexcept;
    BassNotes::Target bassTarget() const noexcept { return bassEnabled ? bassNotes.target() : BassNotes::Target{}; }
    struct GuitarTarget { int channel, note; double frequency; bool held, active; };
    std::array<GuitarTarget, guitarVoiceCount> guitarTargets() const noexcept;
    void process(float* left, float* right, int numSamples,
                 LayerMask outputMask = LayerMask::both, LayerTaps taps = {},
                 BassComponent component = BassComponent::all) noexcept;

private:
    struct Voice
    {
        GuitarOscillators oscillators;
        dsp_core::FixedEnvelope envelope;
        double frequency = 0.0, velocity = 0.0;
        GuitarOscillators::Stereo lastOutput{}, transitionStart{};
        int channel = 0, note = -1, transitionRemaining = 0;
        std::uint64_t age = 0;
        bool keyDown = false;
        bool active() const noexcept { return envelope.active() || transitionRemaining > 0; }
    };
    std::array<Voice, guitarVoiceCount> voices{};
    BassVoice bassVoice;
    std::array<GuitarBus, 2> guitarBus;
    dsp_core::LeftChannelMono guitarMono;
    dsp_core::LinearRamp guitarLevel;
    double guitarLevelTarget = 1.0;
    bool guitarWasActive = false;
    bool bassEnabled = true, guitarEnabled = true, routingConfigured = false;
    bool bassSuspended = false, guitarSuspended = false;
    dsp_core::LinearRamp bassFade, guitarFade, bassBendOffset;
    static_assert(GuitarBus::latencySamples == latencySamples, "layer latency must agree");
    int bassChannel = 1; // Retain selected bend during the note-off release.
    BassNotes bassNotes;
    void updateBass(BassNotes::Change change) noexcept;
    void startGuitar(int channel, int note, int velocity) noexcept;
    void clearGuitar() noexcept;
    GuitarOscillators::Stereo nextGuitar(const std::array<double, 16>& ratios) noexcept;
    std::array<dsp_core::LinearRamp, 16> bend{};
    double rate = 44100.0;
    int attackSamples = 45, releaseSamples = 265, transitionSamples = 89, fadeSamples = 221;
    std::uint64_t serial = 0;
};
}
