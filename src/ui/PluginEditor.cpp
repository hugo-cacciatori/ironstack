#include "PluginEditor.h"
#include "plugin/PluginProcessor.h"

IronstackEditor::IronstackEditor(IronstackProcessor& processor)
    : AudioProcessorEditor(processor), owner(processor),
      outputAttachment(processor.parameterState(), ironstack::parameters::output, output)
{
    title.setText(JucePlugin_Name, juce::dontSendNotification);
    title.setFont(juce::FontOptions(28.0f));
    foundationTab.onClick = [this] { selectPage(false); };
    swarmTab.onClick = [this] { selectPage(true); };
    foundationTab.onPowerToggle = [this] { toggleLayer(ironstack::parameters::bassEnabled); };
    swarmTab.onPowerToggle = [this] { toggleLayer(ironstack::parameters::guitarEnabled); };
    foundationTab.setTooltip("Foundation: select tab; right-click icon to enable or disable. Alt+Enter also toggles power.");
    swarmTab.setTooltip("Swarm: select tab; right-click icon to enable or disable. Alt+Enter also toggles power.");
    for (auto* tab : {&foundationTab, &swarmTab})
    {
        tab->setRadioGroupId(1);
        tab->setClickingTogglesState(true);
    }
    outputLabel.setText("Output", juce::dontSendNotification);
    outputLabel.setJustificationType(juce::Justification::centred);
    outputLabel.setFont(juce::FontOptions(15.0f));
    hoverHelp.setTitle("Control description");
    hoverHelp.setFont(juce::FontOptions(12.0f));
    hoverHelp.setJustificationType(juce::Justification::centredLeft);
    hoverHelp.setMinimumHorizontalScale(1.0f);
    output.setTitle("Output gain");
    output.setTooltip("Master output level. Double-click resets to 0 dB; right-click types an exact value.");
    outputLabel.setTooltip(output.getTooltip());
    output.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    output.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    output.setNumDecimalPlacesToDisplay(3);
    output.setTextValueSuffix(" dB");
    output.setDoubleClickReturnValue(true, 0.0);
    output.setPopupDisplayEnabled(false, false, nullptr);
    output.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xffe9a153));
    output.setHelpText(output.getTooltip());
    output.setWantsKeyboardFocus(true);
    auto* parameter = processor.parameterState().getParameter(ironstack::parameters::output);
    output.onEntryGesture = [parameter](bool begin)
    {
        if (begin) parameter->beginChangeGesture(); else parameter->endChangeGesture();
    };
    panic.setTooltip("Stop all sounding notes.");
    panic.onClick = [&processor] { processor.requestPanic(); };
    help.setTooltip("About Ironstack.");
    help.onClick = [this] { showAbout(); };
    for (auto* child : std::array<juce::Component*, 8>{&title, &outputLabel, &hoverHelp,
            &output, &foundationTab, &swarmTab, &panic, &help})
        addAndMakeVisible(child);
    for (auto* button : std::array<juce::Button*, 4>{&foundationTab, &swarmTab, &panic, &help})
    {
        button->setWantsKeyboardFocus(true);
        button->setTitle(button->getButtonText());
        button->setHelpText(button->getTooltip());
    }
    foundationTab.setExplicitFocusOrder(1); swarmTab.setExplicitFocusOrder(2);
    output.setExplicitFocusOrder(3); panic.setExplicitFocusOrder(4); help.setExplicitFocusOrder(5);
    addMouseListener(this, true);
    setWantsKeyboardFocus(true);
    setResizable(true, true);
    setResizeLimits(560, 360, 1000, 600);
    setSize(680, 400);
    timerCallback();
    startTimerHz(30);
}

IronstackEditor::~IronstackEditor() { removeMouseListener(this); }

void IronstackEditor::mouseDown(const juce::MouseEvent& event)
{
    output.commitIfOutside(event.eventComponent);
}

void IronstackEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff17191d));
    g.setColour(juce::Colour(0xff292d33));
    g.fillRoundedRectangle(24.0f, 186.0f, getWidth() - 48.0f, getHeight() - 228.0f, 8.0f);
    // Connect the selected tab to the page rather than drawing separate buttons.
    g.fillRect(24, 186, getWidth() - 48, 12);
    g.setColour(juce::Colour(0xff101216));
    g.fillRect(0, getHeight() - 30, getWidth(), 30);
    g.setColour(juce::Colour(0xff353a42));
    g.drawHorizontalLine(getHeight() - 30, 0.0f, static_cast<float>(getWidth()));
}

void IronstackEditor::resized()
{
    const auto width = getWidth();
    title.setBounds(24, 25, width / 2 - 110, 40);
    help.setBounds(width - 84, 25, 60, 30);
    outputLabel.setBounds(width / 2 - 70, 12, 140, 24);
    output.setBounds(width / 2 - 70, 38, 140, 92);
    panic.setBounds(width - 112, 70, 88, 28);
    const auto tabWidth = (width - 48) / 2;
    foundationTab.setBounds(24, 142, tabWidth, 46);
    swarmTab.setBounds(24 + tabWidth, 142, width - 48 - tabWidth, 46);
    hoverHelp.setBounds(16, getHeight() - 28, width - 48, 26);
}

void IronstackEditor::toggleLayer(const char* id)
{
    auto* parameter = owner.parameterState().getParameter(id);
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->getValue() >= 0.5f ? 0.0f : 1.0f);
    parameter->endChangeGesture();
    timerCallback();
}

void IronstackEditor::selectPage(bool swarm)
{
    owner.selectGuitarPage(swarm);
    timerCallback();
}

void IronstackEditor::timerCallback()
{
    const auto swarm = owner.isGuitarPageSelected();
    foundationTab.setToggleState(!swarm, juce::dontSendNotification);
    swarmTab.setToggleState(swarm, juce::dontSendNotification);
    foundationTab.setLayerEnabled(owner.parameterState().getRawParameterValue(ironstack::parameters::bassEnabled)->load() >= 0.5f);
    swarmTab.setLayerEnabled(owner.parameterState().getRawParameterValue(ironstack::parameters::guitarEnabled)->load() >= 0.5f);
    for (auto* tab : {&foundationTab, &swarmTab})
        tab->setDescription(tab->getButtonText() + (tab->getToggleState() ? " page selected. " : " page. ")
                            + (tab->isLayerEnabled() ? "Layer on." : "Layer off; still editable."));
    const auto describe = [this](juce::Component* target) -> juce::String
    {
        if (target == nullptr || (target != this && !isParentOf(target))) return {};
        for (auto* component = target; component != nullptr && component != this; component = component->getParentComponent())
            if (auto* tooltip = dynamic_cast<juce::TooltipClient*>(component))
                if (const auto text = tooltip->getTooltip(); text.isNotEmpty()) return text;
        return {};
    };
    auto description = describe(juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse());
    if (description.isEmpty()) description = describe(juce::Component::getCurrentlyFocusedComponent());
    hoverHelp.setText(description.replaceCharacter('\n', ' '), juce::dontSendNotification);
}

void IronstackEditor::showAbout()
{
    aboutBox = juce::NativeMessageBox::showScopedAsync(
        juce::MessageBoxOptions{}.withTitle("About Ironstack")
            .withMessage(juce::String(JucePlugin_Name) + " " + JucePlugin_VersionString + "\n\nHugo Cacciatori")
            .withButton("Close").withAssociatedComponent(this), {});
}
