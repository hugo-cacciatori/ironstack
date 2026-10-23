#pragma once

#include "MidiDispatch.h"
#include "dsp/OutputGain.h"

namespace ironstack
{
// The plugin and offline renderer share MIDI, voice state and master smoothing.
// No host, JUCE, file I/O or GUI types enter this real-time path.
class InstrumentCore
{
public:
    void prepare(double sampleRate, float outputDb = 0.0f, float splitHz = 140.0f, float destruction = 0.5f,
                 bool mono = false) noexcept
    {
        engine.setGuitarMono(mono);
        engine.prepare(sampleRate);
        engine.setBassParameters(splitHz, destruction);
        engine.setGuitarParameters(destruction);
        engine.reset();
        output.prepare(sampleRate, outputDb);
    }
    void reset(float outputDb, float splitHz = 140.0f, float destruction = 0.5f, bool mono = false) noexcept
    {
        engine.setGuitarMono(mono);
        engine.setBassParameters(splitHz, destruction);
        engine.setGuitarParameters(destruction);
        engine.reset();
        output.reset(outputDb);
    }
    void setOutputDb(float db) noexcept { output.setTarget(db); }
    void setGuitarMono(bool mono) noexcept { engine.setGuitarMono(mono); }
    // Enables choose playing policy; output masks below only isolate that policy.
    void setLayersEnabled(bool bass, bool guitar) noexcept { engine.setLayersEnabled(bass, guitar); }
    // Retain the existing bridge name; Destruction now drives both independent buses.
    void setBassParameters(float splitHz, float destruction) noexcept
    {
        engine.setBassParameters(splitHz, destruction);
        engine.setGuitarParameters(destruction);
    }
    void panic() noexcept
    {
        for (int channel = 1; channel <= 16; ++channel)
            engine.allNotesOff(channel);
    }
    void midi(const std::uint8_t* bytes, int size) noexcept { dispatchMidi(engine, bytes, size); }
    void process(float* left, float* right, int samples,
                 LayerMask outputMask = LayerMask::both, LayerTaps taps = {},
                 BassComponent component = BassComponent::all) noexcept
    {
        engine.process(left, right, samples, outputMask, taps, component);
        float* channels[]{left, right};
        output.process(channels, 2, samples);
    }

private:
    Engine engine;
    OutputGain output;
};

inline void dispatchMidi(InstrumentCore& core, const std::uint8_t* bytes, int size) noexcept
{
    core.midi(bytes, size);
}
}
