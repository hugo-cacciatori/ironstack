#include "PluginEditor.h"
#include "plugin/PluginProcessor.h"

IronstackEditor::IronstackEditor(IronstackProcessor& processor)
    : AudioProcessorEditor(processor), owner(processor),
      outputAttachment(processor.parameterState(), ironstack::parameters::output, output),
      splitAttachment(processor.parameterState(), ironstack::parameters::crossover, split),
      destructionAttachment(processor.parameterState(), ironstack::parameters::destruction, destruction),
      bassAttachment(processor.parameterState(), ironstack::parameters::bassEnabled, bassPower),
      guitarAttachment(processor.parameterState(), ironstack::parameters::guitarEnabled, guitarPower),
      monoAttachment(processor.parameterState(), ironstack::parameters::guitarMono, mono)
{
    title.setText(JucePlugin_Name, juce::dontSendNotification);
    title.setFont(juce::FontOptions(28.0f));
    bassTab.onClick = [this] { selectPage(false); };
    guitarTab.onClick = [this] { selectPage(true); };
    bassTab.setRadioGroupId(1);
    guitarTab.setRadioGroupId(1);
    bassTab.setClickingTogglesState(true);
    guitarTab.setClickingTogglesState(true);
    bassTab.onPowerToggle = [this] { bassPower.setToggleState(!bassPower.getToggleState(), juce::sendNotificationSync); timerCallback(); };
    guitarTab.onPowerToggle = [this] { guitarPower.setToggleState(!guitarPower.getToggleState(), juce::sendNotificationSync); timerCallback(); };
    bassTab.setTooltip("Select the Bass page. Right-click the instrument icon to turn bass on or off.");
    guitarTab.setTooltip("Select the Guitars page. Right-click the instrument icon to turn guitars on or off.");
    bassPower.setTooltip("Turn bass on or off. Bass alone holds its first note; with guitars on it follows the lowest held note.");
    guitarPower.setTooltip("Turn guitars on or off. Guitars play your exact notes; bass follows the lowest held note when both are on.");
    mono.setTooltip("Keep the left guitar sound, center it, and match its level. Bass is unchanged.");
    mode.setTitle("Playing mode");
    mode.setFont(juce::FontOptions(14.0f));
    pageHelp.setFont(juce::FontOptions(14.0f));
    pageHelp.setJustificationType(juce::Justification::topLeft);
    outputLabel.setText("Output", juce::dontSendNotification);
    splitLabel.setText("Bass Split", juce::dontSendNotification);
    destructionLabel.setText("Destruction", juce::dontSendNotification);
    for (auto* label : {&outputLabel, &splitLabel, &destructionLabel})
        label->setJustificationType(juce::Justification::centred);
    for (auto* knob : {&output, &split, &destruction})
    {
        knob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 130, 28);
        knob->setNumDecimalPlacesToDisplay(3);
        knob->setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xffe9a153));
    }
    output.setTextValueSuffix(" dB");
    output.setTitle("Output gain");
    output.setTooltip("Master output gain. Double-click to type an exact dB value.");
    split.setTextValueSuffix(" Hz");
    split.setTitle("Bass Split");
    split.setTooltip("High-pass cutoff around the bass upper drive. The clean sine stays intact. Double-click to type Hz.");
    destruction.setTitle("Destruction");
    destruction.setTooltip("Adds drive to both layers while keeping the clean bass foundation intact, from 0 to 1. Double-click to type a value.");
    instructions.setText("Fixed attack/release\nBend: +/-2 semitones\nLatency: 32 samples",
                         juce::dontSendNotification);
    instructions.setFont(juce::FontOptions(13.0f));
    instructions.setJustificationType(juce::Justification::topLeft);
    panic.setTooltip("Release all sounding notes.");
    panic.onClick = [&processor] { processor.requestPanic(); };
    help.setTooltip("Help and information about Ironstack.");
    const auto safeEditor = juce::Component::SafePointer<IronstackEditor>(this);
    help.onClick = [safeEditor]
    {
        if (safeEditor == nullptr)
            return;
        juce::PopupMenu menu;
        menu.addItem(1, "About Ironstack...");
        menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(&safeEditor->help),
            [safeEditor](int result)
            {
                if (result == 1 && safeEditor != nullptr)
                    safeEditor->showAbout();
            });
    };
    for (auto* child : std::array<juce::Component*, 17>{&title, &mode, &outputLabel, &splitLabel, &destructionLabel,
            &instructions, &pageHelp, &output, &split, &destruction, &panic, &help,
            &bassTab, &guitarTab, &bassPower, &guitarPower, &mono})
        addAndMakeVisible(child);
    for (auto* button : std::array<juce::Button*, 7>{&bassTab, &guitarTab, &bassPower, &guitarPower, &mono, &panic, &help})
    {
        button->setWantsKeyboardFocus(true);
        button->setTitle(button->getButtonText());
        button->setHelpText(button->getTooltip());
    }
    for (auto* knob : {&output, &split, &destruction})
    {
        knob->setHelpText(knob->getTooltip());
        knob->setWantsKeyboardFocus(true);
    }
    // Follow the visible instrument/power pairs, then the current page and
    // common strip, rather than the order the components were constructed.
    bassTab.setExplicitFocusOrder(1);
    bassPower.setExplicitFocusOrder(2);
    guitarTab.setExplicitFocusOrder(3);
    guitarPower.setExplicitFocusOrder(4);
    split.setExplicitFocusOrder(5);
    mono.setExplicitFocusOrder(5);
    destruction.setExplicitFocusOrder(6);
    output.setExplicitFocusOrder(7);
    panic.setExplicitFocusOrder(8);
    help.setExplicitFocusOrder(9);
    setResizable(true, true);
    setResizeLimits(560, 460, 1000, 700);
    setSize(680, 480);
    timerCallback();
    startTimerHz(20);
}

void IronstackEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff17191d));
    graphics.setColour(juce::Colour(0xff22252a));
    graphics.fillRect(0, getHeight() - 160, getWidth(), 160);
}

void IronstackEditor::resized()
{
    const auto width = getWidth(), commonTop = getHeight() - 160;
    const auto groupWidth = (width - 64) / 2;
    title.setBounds(24, 18, width - 144, 40);
    help.setBounds(width - 80, 26, 56, 28);
    bassTab.setBounds(24, 76, groupWidth - 96, 44);
    bassPower.setBounds(24 + groupWidth - 92, 82, 92, 32);
    guitarTab.setBounds(40 + groupWidth, 76, groupWidth - 96, 44);
    guitarPower.setBounds(40 + groupWidth * 2 - 92, 82, 92, 32);
    mode.setBounds(24, 132, width - 48, 24);
    splitLabel.setBounds(24, 168, 152, 24);
    split.setBounds(24, 192, 152, commonTop - 200);
    mono.setBounds(24, 192, 200, 32);
    pageHelp.setBounds(216, 174, width - 240, commonTop - 182);
    destructionLabel.setBounds(24, commonTop + 8, 152, 24);
    outputLabel.setBounds(204, commonTop + 8, 152, 24);
    destruction.setBounds(24, commonTop + 32, 152, 116);
    output.setBounds(204, commonTop + 32, 152, 116);
    instructions.setBounds(380, commonTop + 16, width - 404, 72);
    panic.setBounds(width - 152, getHeight() - 56, 128, 36);
}

void IronstackEditor::selectPage(bool guitars)
{
    owner.selectGuitarPage(guitars);
    timerCallback();
}

void IronstackEditor::timerCallback()
{
    const bool guitars = owner.isGuitarPageSelected();
    const bool bassOn = owner.parameterState().getRawParameterValue(ironstack::parameters::bassEnabled)->load() >= 0.5f;
    const bool guitarOn = owner.parameterState().getRawParameterValue(ironstack::parameters::guitarEnabled)->load() >= 0.5f;
    bassTab.setToggleState(!guitars, juce::dontSendNotification);
    guitarTab.setToggleState(guitars, juce::dontSendNotification);
    bassTab.setLayerEnabled(bassOn);
    guitarTab.setLayerEnabled(guitarOn);
    bassPower.setButtonText(bassOn ? "Bass on" : "Bass off");
    guitarPower.setButtonText(guitarOn ? "Guitars on" : "Guitars off");
    bassPower.setTitle(bassPower.getButtonText());
    guitarPower.setTitle(guitarPower.getButtonText());
    bassTab.setDescription(juce::String(guitars ? "Bass page. " : "Bass page selected. ") + (bassOn ? "Layer on." : "Layer off; still editable."));
    guitarTab.setDescription(juce::String(guitars ? "Guitars page selected. " : "Guitars page. ") + (guitarOn ? "Layer on." : "Layer off; still editable."));
    const juce::String routing = bassOn ? (guitarOn ? "Both" : "Bass") : (guitarOn ? "Guitars" : "Off");
    mode.setText("Mode: " + routing + "  |  " + bassPower.getButtonText() + "  |  " + guitarPower.getButtonText(), juce::dontSendNotification);
    mode.setDescription("Playing mode is derived from the two layer power controls.");
    split.setVisible(!guitars);
    splitLabel.setVisible(!guitars);
    mono.setVisible(guitars);
    pageHelp.setText(guitars ? "Eight notes at the pitches you play.\nMono centers the left guitar sound."
                             : "One octave below your input.\nBass alone holds its first note; Both follows the lowest held note.",
                     juce::dontSendNotification);
}

void IronstackEditor::showAbout()
{
    // Tie the asynchronous dialog to the editor so unloading the plugin closes it.
    aboutBox = juce::NativeMessageBox::showScopedAsync(
        juce::MessageBoxOptions{}
            .withTitle("About Ironstack")
            .withMessage(juce::String(JucePlugin_Name) + " " + JucePlugin_VersionString
                         + "\n\nInstrument prototype\nHugo Cacciatori")
            .withButton("Close")
            .withAssociatedComponent(this), {});
}
