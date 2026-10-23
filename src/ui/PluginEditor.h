#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "NumericKnob.h"
#include "LayerTab.h"

class IronstackProcessor;

class IronstackEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit IronstackEditor(IronstackProcessor& processor);
    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void showAbout();
    void selectPage(bool guitars);
    void timerCallback() override;

    IronstackProcessor& owner;
    juce::Label title, mode, outputLabel, splitLabel, destructionLabel, instructions, pageHelp;
    NumericKnob output, split, destruction;
    LayerTab bassTab{"Bass"}, guitarTab{"Guitars"};
    juce::ToggleButton bassPower{"Bass on"}, guitarPower{"Guitars on"}, mono{"Guitar Mono"};
    juce::TextButton panic{"Panic"};
    juce::TextButton help{"Help"};
    juce::AudioProcessorValueTreeState::SliderAttachment outputAttachment;
    juce::AudioProcessorValueTreeState::SliderAttachment splitAttachment, destructionAttachment;
    juce::AudioProcessorValueTreeState::ButtonAttachment bassAttachment, guitarAttachment, monoAttachment;
    juce::TooltipWindow tooltips{nullptr, 500};
    juce::ScopedMessageBox aboutBox;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IronstackEditor)
};
