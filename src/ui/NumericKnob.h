#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <locale>
#include <sstream>

// One entry policy for every numerical knob. JUCE's editable Label supplies
// selection, Enter commit and Escape cancellation; invalid text keeps the value.
class NumericKnob final : public juce::Slider
{
public:
    void mouseDoubleClick(const juce::MouseEvent&) override { showTextBox(); }
    juce::String getTextFromValue(double value) override
    {
        return juce::String(value, getNumDecimalPlacesToDisplay()) + getTextValueSuffix();
    }
    double getValueFromText(const juce::String& text) override
    {
        auto stripped = text.trim();
        const auto suffix = getTextValueSuffix().trim();
        if (suffix.isNotEmpty() && stripped.endsWithIgnoreCase(suffix))
            stripped = stripped.dropLastCharacters(suffix.length()).trim();
        std::istringstream parser(stripped.toStdString());
        parser.imbue(std::locale::classic());
        double value = 0.0;
        parser >> std::noskipws >> value;
        if (!parser || parser.peek() != std::char_traits<char>::eof() || !std::isfinite(value)
            || value < getMinimum() || value > getMaximum())
        {
            if (normalTooltip.isEmpty()) normalTooltip = getTooltip();
            setTooltip("Enter a number from " + juce::String(getMinimum()) + " to " + juce::String(getMaximum())
                       + getTextValueSuffix() + ". Value unchanged.");
            return getValue();
        }
        if (normalTooltip.isNotEmpty()) { setTooltip(normalTooltip); normalTooltip.clear(); }
        return value;
    }
private:
    juce::String normalTooltip;
};
