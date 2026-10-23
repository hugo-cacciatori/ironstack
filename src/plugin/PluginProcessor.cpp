#include "PluginProcessor.h"
#include "MidiInput.h"
#include "ui/PluginEditor.h"
#include <cmath>
#include <locale>
#include <sstream>

static_assert(std::atomic<float>::is_always_lock_free && std::atomic<bool>::is_always_lock_free,
              "Audio parameter and panic reads must be lock-free");

juce::AudioProcessorValueTreeState::ParameterLayout IronstackProcessor::createParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ironstack::parameters::output, 1}, "Output",
        juce::NormalisableRange<float>{-12.0f, 6.0f}, 0.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("dB")
            .withStringFromValueFunction([](float value, int) { return juce::String(value, 2); })
            .withValueFromStringFunction([](const juce::String& text) { return text.getFloatValue(); })));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ironstack::parameters::crossover, 1}, "Split",
        juce::NormalisableRange<float>{80.0f, 250.0f}, 140.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ironstack::parameters::destruction, 1}, "Destruction",
        juce::NormalisableRange<float>{0.0f, 1.0f}, 0.5f));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ironstack::parameters::bassEnabled, 1}, "Bass enabled", true));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ironstack::parameters::guitarEnabled, 1}, "Guitars enabled", true));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ironstack::parameters::guitarMono, 1}, "Guitar Mono", false));
    return layout;
}

IronstackProcessor::IronstackProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "IronstackState", createParameters())
{
    outputDb = state.getRawParameterValue(ironstack::parameters::output);
    splitHz = state.getRawParameterValue(ironstack::parameters::crossover);
    destruction = state.getRawParameterValue(ironstack::parameters::destruction);
    bassEnabled = state.getRawParameterValue(ironstack::parameters::bassEnabled);
    guitarEnabled = state.getRawParameterValue(ironstack::parameters::guitarEnabled);
    guitarMono = state.getRawParameterValue(ironstack::parameters::guitarMono);
    core.setLayersEnabled(true, true);
    core.reset(0.0f);
}

void IronstackProcessor::prepareToPlay(double sampleRate, int)
{
    preparedRate = std::isfinite(sampleRate) && sampleRate >= 44100.0 && sampleRate <= 192000.0 ? sampleRate : 44100.0;
    core.setLayersEnabled(readToggle(bassEnabled, true), readToggle(guitarEnabled, true));
    core.prepare(preparedRate, readOutputDb(), readSplit(), readDestruction(), readToggle(guitarMono, false));
    reset();
    setLatencySamples(ironstack::Engine::latencySamples);
}

void IronstackProcessor::releaseResources() { reset(); }

float IronstackProcessor::readOutputDb() const noexcept
{
    const auto value = outputDb->load(std::memory_order_relaxed);
    return std::isfinite(value) ? std::clamp(value, -12.0f, 6.0f) : 0.0f;
}

float IronstackProcessor::readSplit() const noexcept
{
    const auto value = splitHz->load(std::memory_order_relaxed);
    return std::isfinite(value) ? std::clamp(value, 80.0f, 250.0f) : 140.0f;
}

float IronstackProcessor::readDestruction() const noexcept
{
    const auto value = destruction->load(std::memory_order_relaxed);
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.5f;
}

void IronstackProcessor::reset()
{
    core.setLayersEnabled(readToggle(bassEnabled, true), readToggle(guitarEnabled, true));
    core.reset(readOutputDb(), readSplit(), readDestruction(), readToggle(guitarMono, false));
    panicRequested.store(false, std::memory_order_relaxed);
}

bool IronstackProcessor::readToggle(const std::atomic<float>* parameter, bool fallback) noexcept
{
    const auto value = parameter->load(std::memory_order_relaxed);
    return std::isfinite(value) ? value >= 0.5f : fallback;
}

bool IronstackProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet().isDisabled()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void IronstackProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    if (panicRequested.exchange(false, std::memory_order_relaxed))
        core.panic();
    core.setOutputDb(readOutputDb());
    core.setBassParameters(readSplit(), readDestruction());
    core.setLayersEnabled(readToggle(bassEnabled, true), readToggle(guitarEnabled, true));
    core.setGuitarMono(readToggle(guitarMono, false));
    ironstack::processMidiBlock(core, buffer, midi);
    midi.clear();
}

juce::AudioProcessorEditor* IronstackProcessor::createEditor()
{
    return new IronstackEditor(*this);
}

void IronstackProcessor::selectGuitarPage(bool selected)
{
    if (guitarPageSelected.exchange(selected, std::memory_order_relaxed) != selected)
        updateHostDisplay(juce::AudioProcessorListener::ChangeDetails{}.withNonParameterStateChanged(true));
}

void IronstackProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    auto saved = state.copyState();
    saved.setProperty("schemaVersion", 1, nullptr);
    saved.setProperty("editorPage", isGuitarPageSelected() ? "guitars" : "bass", nullptr);
    if (auto xml = saved.createXml())
        copyXmlToBinary(*xml, destination);
}

void IronstackProcessor::setStateInformation(const void* data, int size)
{
    if (data == nullptr || size <= 0)
        return;
    auto xml = getXmlFromBinary(data, size);
    if (!xml || !xml->hasTagName("IronstackState"))
        return;
    if (xml->hasAttribute("schemaVersion") && xml->getStringAttribute("schemaVersion") != "1")
        return;

    // Missing newly added parameters use their defaults in legacy states.
    // Validate every recognized entry before publishing any part of a recall.
    const auto page = xml->getStringAttribute("editorPage", "bass");
    if (page != "bass" && page != "guitars") return;
    const std::array<const char*, 6> ids{ironstack::parameters::output,
        ironstack::parameters::crossover, ironstack::parameters::destruction,
        ironstack::parameters::bassEnabled, ironstack::parameters::guitarEnabled, ironstack::parameters::guitarMono};
    std::array<float, 6> values{0.0f, 140.0f, 0.5f, 1.0f, 1.0f, 0.0f};
    const std::array<float, 6> minimum{-12.0f, 80.0f, 0.0f, 0.0f, 0.0f, 0.0f},
                               maximum{6.0f, 250.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    std::array<bool, 6> found{};
    for (auto* child : xml->getChildIterator())
        if (child->hasTagName("PARAM"))
            for (std::size_t i = 0; i < ids.size(); ++i)
                if (child->getStringAttribute("id") == ids[i])
                {
                    if (found[i] || !child->hasAttribute("value")) return;
                    found[i] = true;
                    const auto text = child->getStringAttribute("value").toStdString();
                    std::istringstream parser(text);
                    parser.imbue(std::locale::classic());
                    parser >> std::noskipws >> values[i];
                    if (!parser || parser.peek() != std::char_traits<char>::eof() || !std::isfinite(values[i])) return;
                    if (i >= 3 && values[i] != 0.0f && values[i] != 1.0f) return;
                    values[i] = std::clamp(values[i], minimum[i], maximum[i]);
                }
    if (!found[0] && xml->hasAttribute("schemaVersion")) return;
    juce::ValueTree clean("IronstackState");
    clean.setProperty("schemaVersion", 1, nullptr);
    for (std::size_t i = 0; i < ids.size(); ++i)
    {
        juce::ValueTree parameter("PARAM");
        parameter.setProperty("id", ids[i], nullptr);
        parameter.setProperty("value", values[i], nullptr);
        clean.appendChild(parameter, nullptr);
    }
    state.replaceState(clean);
    // Loading a project must not mark that project dirty again.
    guitarPageSelected.store(page == "guitars", std::memory_order_relaxed);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new IronstackProcessor();
}
