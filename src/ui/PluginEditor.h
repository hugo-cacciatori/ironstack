#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "NumericKnob.h"
#include "LayerTab.h"

class IronstackProcessor;
class IronstackEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit IronstackEditor(IronstackProcessor& processor);
    ~IronstackEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
private:
    void showAbout();
    void selectPage(bool swarm);
    void timerCallback() override;
    void toggleLayer(const char* id);
    IronstackProcessor& owner;
    juce::Label title, levelLabel, outputLabel, hoverHelp;
    NumericKnob foundationLevel, swarmLevel, output;
    LayerTab foundationTab{"Foundation"}, swarmTab{"Swarm", true};
    juce::TextButton panic{"Panic"}, help{"Help"};
    juce::AudioProcessorValueTreeState::SliderAttachment foundationAttachment, swarmAttachment, outputAttachment;
    juce::ScopedMessageBox aboutBox;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IronstackEditor)
};
