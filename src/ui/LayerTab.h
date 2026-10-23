#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// The icon has a secondary power action; the rest of the tab only selects a
// page. Keep both actions keyboard-accessible through the separate power button.
class LayerTab final : public juce::TextButton
{
public:
    explicit LayerTab(const juce::String& name) : TextButton(name) {}
    std::function<void()> onPowerToggle;
    void setLayerEnabled(bool enabled) { if (layerEnabled != enabled) { layerEnabled = enabled; repaint(); } }
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu())
        {
            suppressMouseUp = true;
            if (event.x < iconWidth && onPowerToggle) onPowerToggle();
            return;
        }
        suppressMouseUp = false;
        TextButton::mouseDown(event);
    }
    void mouseUp(const juce::MouseEvent& event) override
    {
        if (suppressMouseUp) { suppressMouseUp = false; return; }
        if (!event.mods.isPopupMenu()) TextButton::mouseUp(event);
    }
    void paintButton(juce::Graphics& graphics, bool highlighted, bool down) override
    {
        const auto accent = juce::Colour(0xffe9a153);
        getLookAndFeel().drawButtonBackground(graphics, *this,
            getToggleState() ? juce::Colour(0xff34383e) : juce::Colour(0xff24272c), highlighted, down);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(16.0f));
        graphics.drawFittedText(getButtonText(), iconWidth, 0, getWidth() - iconWidth - 8, getHeight(),
                                juce::Justification::centredLeft, 1);
        graphics.setColour(layerEnabled ? accent : juce::Colour(0xff999999));
        graphics.drawEllipse(10.0f, 20.0f, 18.0f, 17.0f, 1.5f);
        graphics.drawLine(19.0f, 8.0f, 19.0f, 31.0f, 2.0f);
        graphics.drawLine(15.0f, 8.0f, 23.0f, 8.0f, 2.0f);
        if (!layerEnabled) graphics.drawLine(8.0f, 38.0f, 29.0f, 6.0f, 2.0f);
        if (getToggleState())
        {
            graphics.setColour(accent);
            graphics.fillRect(8, getHeight() - 4, getWidth() - 16, 3);
        }
        if (hasKeyboardFocus(true))
        {
            graphics.setColour(juce::Colours::white);
            graphics.drawRect(getLocalBounds().reduced(2), 1);
        }
    }
private:
    static constexpr int iconWidth = 38;
    bool layerEnabled = true;
    bool suppressMouseUp = false;
};
