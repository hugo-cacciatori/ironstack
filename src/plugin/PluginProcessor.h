#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "InstrumentCore.h"
#include "Parameters.h"
#include <atomic>

class IronstackProcessor final : public juce::AudioProcessor
{
public:
    IronstackProcessor();
    const juce::String getName() const override { return JucePlugin_Name; }
    void prepareToPlay(double sampleRate, int maximumBlockSize) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return ironstack::Engine::releaseSeconds + ironstack::Engine::latencySamples / preparedRate; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destination) override;
    void setStateInformation(const void* data, int size) override;
    juce::AudioProcessorValueTreeState& parameterState() noexcept { return state; }
    void requestPanic() noexcept { panicRequested.store(true, std::memory_order_relaxed); }
    bool isGuitarPageSelected() const noexcept { return guitarPageSelected.load(std::memory_order_relaxed); }
    void selectGuitarPage(bool selected);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameters();
    float readOutputDb() const noexcept;
    float readSplit() const noexcept;
    float readDestruction() const noexcept;
    static bool readToggle(const std::atomic<float>* parameter, bool fallback) noexcept;
    ironstack::InstrumentCore core;
    juce::AudioProcessorValueTreeState state;
    std::atomic<float>* outputDb = nullptr;
    std::atomic<float>* splitHz = nullptr;
    std::atomic<float>* destruction = nullptr;
    std::atomic<float>* bassEnabled = nullptr;
    std::atomic<float>* guitarEnabled = nullptr;
    std::atomic<float>* guitarMono = nullptr;
    double preparedRate = 44100.0;
    std::atomic<bool> panicRequested{false};
    // UI preference is independent of automatable sound state; no ValueTree
    // mutation is required when the musician just selects an editing page.
    std::atomic<bool> guitarPageSelected{false};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IronstackProcessor)
};
