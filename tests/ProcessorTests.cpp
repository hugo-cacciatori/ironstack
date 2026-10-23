#include "plugin/PluginProcessor.h"
#include "plugin/MidiInput.h"
#include "ui/NumericKnob.h"
#include "ui/LayerTab.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <locale>
#if JUCE_LINUX
#include "X11TestDisplay.h"
#endif

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void setOutput(IronstackProcessor& processor, float db)
{
    auto* parameter = processor.parameterState().getParameter(ironstack::parameters::output);
    require(parameter != nullptr, "stable output parameter exists");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(db));
}

float outputDb(IronstackProcessor& processor)
{
    return processor.parameterState().getRawParameterValue(ironstack::parameters::output)->load();
}

void restoreXml(IronstackProcessor& processor, const juce::String& text)
{
    auto xml = juce::parseXML(text);
    require(xml != nullptr, "test state XML parses");
    juce::MemoryBlock bytes;
    juce::AudioProcessor::copyXmlToBinary(*xml, bytes);
    processor.setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
}

void setToggle(IronstackProcessor& processor, const char* id, bool enabled)
{
    auto* parameter = processor.parameterState().getParameter(id);
    require(parameter && parameter->isBoolean(), "host toggle exists and is boolean");
    parameter->setValueNotifyingHost(enabled ? 1.0f : 0.0f);
}

bool toggle(IronstackProcessor& processor, const char* id)
{
    return processor.parameterState().getRawParameterValue(id)->load() >= 0.5f;
}

void refreshUi()
{
    juce::Thread::sleep(60);
    juce::Timer::callPendingTimersSynchronously();
}

void rightClickIcon(LayerTab& tab, int x = 18)
{
    const juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(),
        {static_cast<float>(x), 20.0f}, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier),
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &tab, &tab, {}, {}, {}, 1, false);
    tab.mouseDown(event);
    tab.mouseUp(event);
}

void stateTests()
{
    IronstackProcessor source, destination;
    struct HostChanges final : juce::AudioProcessorListener
    {
        int pageChanges = 0, parameterChanges = 0;
        void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override { ++parameterChanges; }
        void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& changes) override
        {
            if (changes.nonParameterStateChanged) ++pageChanges;
        }
    } changes;
    source.addListener(&changes);
    source.selectGuitarPage(false);
    source.selectGuitarPage(true);
    source.selectGuitarPage(true);
    require(changes.pageChanges == 1 && changes.parameterChanges == 0, "page change marks saved state dirty once without audio automation");
    restoreXml(source, "<IronstackState/>");
    require(changes.pageChanges == 1 && !source.isGuitarPageSelected(), "recall restores page without marking project dirty");
    source.removeListener(&changes);
    require(source.getParameters().size() == 6, "three continuous controls plus enables/Mono are exposed");
    for (const auto* retired : {"bass_mute", "guitar_mute", "play_mode"})
        require(source.parameterState().getParameter(retired) == nullptr, "no conflicting mute or mode parameter");
    require(toggle(source, ironstack::parameters::bassEnabled) && toggle(source, ironstack::parameters::guitarEnabled)
            && !toggle(source, ironstack::parameters::guitarMono) && !source.isGuitarPageSelected(),
            "default Both, stereo guitar and Bass page");
    require(outputDb(source) == 0.0f, "output defaults to unity");
    for (const auto* id : {ironstack::parameters::crossover, ironstack::parameters::destruction})
    {
        auto* parameter = source.parameterState().getParameter(id);
        for (const auto normalised : {0.0f, 0.1234567f, 0.5f, 0.9876543f, 1.0f})
        {
            parameter->setValueNotifyingHost(normalised);
            const auto expected = source.parameterState().getRawParameterValue(id)->load();
            juce::MemoryBlock bytes;
            source.getStateInformation(bytes);
            destination.setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
            require(destination.parameterState().getRawParameterValue(id)->load() == expected,
                    "new bass parameters round-trip exactly at boundaries and fractional values");
        }
    }
    for (const float value : {-12.0f, -9.876543f, -6.0f, 0.0f, 1.234567f, 6.0f})
    {
        setOutput(source, value);
        const auto expected = outputDb(source);
        juce::MemoryBlock saved;
        source.getStateInformation(saved);
        destination.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        require(outputDb(destination) == expected, "state restores exact parameter value");
        juce::MemoryBlock repeated;
        destination.getStateInformation(repeated);
        source.setStateInformation(repeated.getData(), static_cast<int>(repeated.getSize()));
        require(outputDb(source) == expected, "repeated state round-trip is exact");
    }
    for (int bits = 0; bits < 16; ++bits)
    {
        setToggle(source, ironstack::parameters::bassEnabled, (bits & 1) != 0);
        setToggle(source, ironstack::parameters::guitarEnabled, (bits & 2) != 0);
        setToggle(source, ironstack::parameters::guitarMono, (bits & 4) != 0);
        source.selectGuitarPage((bits & 8) != 0);
        juce::MemoryBlock saved, repeated;
        source.getStateInformation(saved);
        destination.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        destination.getStateInformation(repeated);
        require(destination.isGuitarPageSelected() == source.isGuitarPageSelected(), "selected page restores independently");
        for (const auto* id : {ironstack::parameters::bassEnabled, ironstack::parameters::guitarEnabled, ironstack::parameters::guitarMono})
            require(toggle(source, id) == toggle(destination, id), "all boolean values restore exactly");
        for (const auto* id : {ironstack::parameters::output, ironstack::parameters::crossover, ironstack::parameters::destruction})
            require(source.parameterState().getRawParameterValue(id)->load() == destination.parameterState().getRawParameterValue(id)->load(),
                    "disabled-page sound settings restore exactly");
        source.setStateInformation(repeated.getData(), static_cast<int>(repeated.getSize()));
        source.getStateInformation(saved);
        require(saved == repeated, "canonical repeated states are byte-exact");
    }
    setOutput(destination, -6.0f);
    const auto before = outputDb(destination);
    for (const auto* bad : {
        "<OtherState/>",
        "<IronstackState editorPage='effects'><PARAM id='output_db' value='3'/></IronstackState>",
        "<IronstackState><PARAM id='output_db' value='3'/><PARAM id='bass_enabled' value='0.5'/></IronstackState>",
        "<IronstackState><PARAM id='output_db' value='3'/><PARAM id='guitar_enabled' value='nan'/></IronstackState>",
        "<IronstackState><PARAM id='output_db' value='3'/><PARAM id='guitar_mono' value='2'/></IronstackState>",
        "<IronstackState><PARAM id='output_db' value='3'/><PARAM id='bass_enabled' value='0'/><PARAM id='bass_enabled' value='1'/></IronstackState>",
        "<IronstackState schemaVersion='2'><PARAM id='output_db' value='3'/></IronstackState>",
        "<IronstackState><PARAM id='output_db' value='3'/><PARAM id='xover_hz' value='nan'/></IronstackState>",
        "<IronstackState><PARAM id='output_db' value='3'/><PARAM id='destruction' value='0.5'/><PARAM id='destruction' value='1'/></IronstackState>",
        "<IronstackState schemaVersion='1'/>",
        "<IronstackState><PARAM id='output_db' value='nan'/></IronstackState>",
        "<IronstackState><PARAM id='output_db' value='inf'/></IronstackState>",
        "<IronstackState><PARAM id='output_db' value='oops'/></IronstackState>",
        "<IronstackState><PARAM id='output_db' value='2junk'/></IronstackState>",
        "<IronstackState><PARAM id='output_db' value='2'/><PARAM id='output_db' value='3'/></IronstackState>"})
    {
        juce::MemoryBlock initial, after;
        destination.getStateInformation(initial);
        restoreXml(destination, bad);
        destination.getStateInformation(after);
        require(initial == after, "invalid state cannot partially publish parameters or selected page");
        require(outputDb(destination) == before, "invalid/unknown state preserves current settings");
    }
    destination.setStateInformation(nullptr, 0);
    const char truncated[] = "bad state";
    destination.setStateInformation(truncated, sizeof(truncated));
    require(outputDb(destination) == before, "truncated/null state is ignored");
    restoreXml(destination, "<IronstackState><PARAM id='output_db' value='99'/></IronstackState>");
    require(outputDb(destination) == 6.0f, "out-of-range finite state clamps safely");
    restoreXml(destination, "<IronstackState/>");
    require(outputDb(destination) == 0.0f, "empty pre-parameter state restores old unity gain");
    require(toggle(destination, ironstack::parameters::bassEnabled) && toggle(destination, ironstack::parameters::guitarEnabled)
            && !toggle(destination, ironstack::parameters::guitarMono) && !destination.isGuitarPageSelected(),
            "legacy states use Both/stereo/Bass-page defaults");
    require(destination.parameterState().getRawParameterValue(ironstack::parameters::crossover)->load() == 140.0f
            && destination.parameterState().getRawParameterValue(ironstack::parameters::destruction)->load() == 0.5f,
            "legacy state restores defaults for new bass parameters");
    struct CommaDecimal final : std::numpunct<char>
    {
        char do_decimal_point() const override { return ','; }
    };
    const auto previousLocale = std::locale::global(std::locale(std::locale::classic(), new CommaDecimal));
    restoreXml(destination, "<IronstackState><PARAM id='output_db' value='-3.25'/></IronstackState>");
    const auto recalled = outputDb(destination);
    std::locale::global(previousLocale);
    require(recalled == -3.25f, "state decimals are independent of the host C++ locale");
    std::cout << "state_round_trips=6 exact; malformed/future/legacy checks passed\n";
}

void processorTests()
{
    IronstackProcessor reference, changed;
    for (auto* processor : {&reference, &changed})
    {
        processor->setRateAndBufferSizeDetails(48000.0, 512);
        processor->prepareToPlay(48000.0, 512);
        require(processor->getLatencySamples() == 32, "aligned oversampling latency is 32 samples");
        require(std::abs(processor->getTailLengthSeconds() - (0.006 + 32.0 / 48000.0)) < 1.0e-9,
                "fixed release plus aligned delay reported");
    }
    juce::AudioBuffer<float> a(2, 512), b(2, 512);
    juce::MidiBuffer midiA, midiB;
    midiA.addEvent(juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(127)), 0);
    midiB = midiA;
    reference.processBlock(a, midiA);
    changed.processBlock(b, midiB);
    require(midiA.isEmpty() && midiB.isEmpty(), "instrument consumes MIDI rather than echoing it");
    for (int i = 0; i < 512; ++i)
        require(a.getSample(0, i) == b.getSample(0, i), "default output gain leaves audio unchanged");
    setOutput(changed, -6.0f);
    reference.processBlock(a, midiA);
    changed.processBlock(b, midiB);
    double maximumGainError = 0.0;
    const auto target = std::pow(10.0, -6.0 / 20.0);
    for (int i = 0; i < 512; ++i)
    {
        const auto gain = i < 240 ? 1.0 + (target - 1.0) * i / 240.0 : target;
        maximumGainError = std::max(maximumGainError,
            std::abs(b.getSample(0, i) - a.getSample(0, i) * gain));
        maximumGainError = std::max(maximumGainError,
            std::abs(b.getSample(1, i) - a.getSample(1, i) * gain));
    }
    require(maximumGainError < 1.0e-7, "gain ramps over exactly 5 ms without an instantaneous step");

    // Recall while held uses the same smoothed parameter path.
    juce::MemoryBlock unity;
    reference.getStateInformation(unity);
    changed.setStateInformation(unity.getData(), static_cast<int>(unity.getSize()));
    reference.processBlock(a, midiA);
    changed.processBlock(b, midiB);
    for (int i = 240; i < 512; ++i)
        require(a.getSample(0, i) == b.getSample(0, i), "state recall reaches unity during a held note");
    changed.requestPanic();
    changed.processBlock(b, midiB);
    require(b.getMagnitude(0, 340, 172) == 0.0f, "UI panic releases all voices within release plus latency");
    changed.reset();
    changed.processBlock(b, midiB);
    require(b.getMagnitude(0, 512) == 0.0f, "host reset clears held notes");

    for (const double rate : {44100.0, 96000.0, 192000.0})
    {
        changed.prepareToPlay(rate, 1);
        for (const int size : {1, 17, 0, 1024, 3})
        {
            b.setSize(2, size);
            setOutput(changed, size % 2 ? -12.0f : 6.0f);
            midiB.addEvent(juce::MidiMessage::noteOn(1, 127, static_cast<juce::uint8>(127)), 0);
            changed.processBlock(b, midiB);
            for (int i = 0; i < size; ++i)
                require(std::isfinite(b.getSample(0, i)), "parameter extremes/rates/changing blocks stay finite");
        }
        require(changed.getLatencySamples() == 32, "latency is stable across rates/block lengths");
    }
    std::cout << "output_ramp_max_error=" << maximumGainError << " latency_samples=32\n";
}

void headlessParityTests()
{
    for (const double rate : {44100.0, 48000.0, 192000.0})
    {
        IronstackProcessor processor;
        ironstack::InstrumentCore core;
        processor.prepareToPlay(rate, 512);
        core.setLayersEnabled(true, true);
        core.prepare(rate);
        int index = 0;
        for (const int count : {0, 1, 17, 512, 3, 1024, 4096})
        {
            juce::AudioBuffer<float> host(2, count), offline(2, count);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 45 + index, static_cast<juce::uint8>(127)), 0);
            midi.addEvent(juce::MidiMessage::pitchWheel(1, 10000), count / 2);
            midi.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), count / 2);
            const auto db = index++ % 2 == 0 ? -9.0f : 6.0f;
            setOutput(processor, db);
            core.setOutputDb(outputDb(processor)); // Use the exact APVTS-normalised value.
            for (const auto* id : {ironstack::parameters::crossover, ironstack::parameters::destruction})
                processor.parameterState().getParameter(id)->setValueNotifyingHost(static_cast<float>(index % 3) * 0.5f);
            core.setBassParameters(processor.parameterState().getRawParameterValue(ironstack::parameters::crossover)->load(),
                                   processor.parameterState().getRawParameterValue(ironstack::parameters::destruction)->load());
            auto hostMidi = midi;
            processor.processBlock(host, hostMidi);
            ironstack::processMidiBlock(core, offline, midi);
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < count; ++sample)
                    require(host.getSample(channel, sample) == offline.getSample(channel, sample),
                            "headless core exactly matches plugin MIDI/gain across changing blocks");
        }
    }
    std::cout << "headless_plugin_sample_parity=exact\n";
}

void routingBridgeTests()
{
    for (const double rate : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0})
    for (int initial = 0; initial < 8; ++initial)
    {
        IronstackProcessor processor;
        ironstack::InstrumentCore reference;
        const bool bassOn = (initial & 1) != 0, guitarOn = (initial & 2) != 0, mono = (initial & 4) != 0;
        setToggle(processor, ironstack::parameters::bassEnabled, bassOn);
        setToggle(processor, ironstack::parameters::guitarEnabled, guitarOn);
        setToggle(processor, ironstack::parameters::guitarMono, mono);
        processor.prepareToPlay(rate, 512);
        reference.setLayersEnabled(bassOn, guitarOn);
        reference.prepare(rate, 0.0f, 140.0f, 0.5f, mono);
        juce::MemoryBlock saved;
        processor.getStateInformation(saved);
        int index = 0;
        for (const int count : {0, 1, 17, 73, 512, 3, 1024, 4096})
        {
            if (index == 3)
            {
                setToggle(processor, ironstack::parameters::bassEnabled, !bassOn);
                setToggle(processor, ironstack::parameters::guitarEnabled, !guitarOn);
                setToggle(processor, ironstack::parameters::guitarMono, !mono);
                reference.setLayersEnabled(!bassOn, !guitarOn);
                reference.setGuitarMono(!mono);
            }
            if (index == 4)
            {
                // Restore through the real host state path while keys are held.
                processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
                reference.setLayersEnabled(bassOn, guitarOn);
                reference.setGuitarMono(mono);
            }
            if (index == 5)
            {
                processor.reset();
                reference.reset(0.0f, 140.0f, 0.5f, mono);
            }
            if (index == 6) { processor.requestPanic(); reference.panic(); }
            processor.selectGuitarPage(index % 2 != 0); // Page has no sound counterpart.
            juce::AudioBuffer<float> host(2, count), expected(2, count);
            juce::MidiBuffer midi;
            if (index < 3 || index == 5)
                midi.addEvent(juce::MidiMessage::noteOn(1, index == 2 ? 57 : 69, static_cast<juce::uint8>(127)), 0);
            if (index == 3)
            {
                midi.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
                midi.addEvent(juce::MidiMessage::noteOff(1, 69), count / 2);
            }
            if (index == 4)
            {
                midi.addEvent(juce::MidiMessage::pitchWheel(1, 10000), 0);
                midi.addEvent(juce::MidiMessage::noteOn(2, 62, static_cast<juce::uint8>(96)), count / 2);
            }
            auto hostMidi = midi;
            processor.processBlock(host, hostMidi);
            ironstack::processMidiBlock(reference, expected, midi);
            require(processor.getLatencySamples() == 32, "enable/Mono/recall preserve fixed latency");
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < count; ++sample)
                    require(host.getSample(channel, sample) == expected.getSample(channel, sample),
                            "host enables/Mono/held recall/reset match explicit renderer policy exactly");
            if (index == 7) require(host.getMagnitude(count - 256, 256) == 0.0f, "panic clears held notes in all modes");
            ++index;
        }
    }
    // A never-configured Engine retains the previously accepted locked-bass
    // diagnostic policy. Explicit host Bass mode must preserve those samples.
    IronstackProcessor bassProcessor;
    setToggle(bassProcessor, ironstack::parameters::guitarEnabled, false);
    bassProcessor.prepareToPlay(48000.0, 512);
    ironstack::Engine legacy;
    legacy.prepare(48000.0);
    juce::AudioBuffer<float> bassAudio(2, 512), legacyAudio(2, 512);
    for (const auto& message : {juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(127)),
                               juce::MidiMessage::noteOn(1, 57, static_cast<juce::uint8>(96)),
                               juce::MidiMessage::noteOff(1, 69), juce::MidiMessage::noteOff(1, 57)})
    {
        juce::MidiBuffer midi;
        midi.addEvent(message, 0);
        bassProcessor.processBlock(bassAudio, midi);
        ironstack::dispatchMidi(legacy, message.getRawData(), message.getRawDataSize());
        legacy.process(legacyAudio.getWritePointer(0), legacyAudio.getWritePointer(1), 512, ironstack::LayerMask::bass);
        for (int sample = 0; sample < 512; ++sample)
            require(bassAudio.getSample(0, sample) == legacyAudio.getSample(0, sample)
                    && bassAudio.getSample(0, sample) == bassAudio.getSample(1, sample),
                    "explicit host Bass preserves accepted locked-bass samples, including overlap/handover");
    }
    std::cout << "host_routing_matrix=48 rate/mode/Mono cases; live automation/held recall/page/reset/panic exact\n";
}

void editorTests(const juce::String& snapshot)
{
    IronstackProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    require(editor && editor->getWidth() == 680 && editor->getHeight() == 480, "usable resizable editor dimensions");
    editor->addToDesktop(0); // Native accessibility handlers require a window peer.
    juce::Slider* slider = nullptr;
    juce::TextButton* button = nullptr;
    for (auto* child : editor->getChildren())
    {
        require(editor->getLocalBounds().contains(child->getBounds()), "editor controls fit inside its bounds");
        if (auto* candidate = dynamic_cast<juce::Slider*>(child))
            if (candidate->getTitle() == "Output gain") slider = candidate;
        if (auto* candidate = dynamic_cast<juce::TextButton*>(child))
            if (candidate->getButtonText() == "Panic") button = candidate;
    }
    require(slider && button, "Output and Panic controls exist");
    LayerTab* bassTab = nullptr;
    LayerTab* guitarTab = nullptr;
    juce::ToggleButton* bassPower = nullptr;
    juce::ToggleButton* guitarPower = nullptr;
    juce::ToggleButton* mono = nullptr;
    juce::Label* mode = nullptr;
    for (auto* child : editor->getChildren())
    {
        if (auto* tab = dynamic_cast<LayerTab*>(child))
            (tab->getButtonText() == "Bass" ? bassTab : guitarTab) = tab;
        if (auto* power = dynamic_cast<juce::ToggleButton*>(child))
        {
            if (power->getButtonText() == "Bass on") bassPower = power;
            if (power->getButtonText() == "Guitars on") guitarPower = power;
            if (power->getButtonText() == "Guitar Mono") mono = power;
        }
        if (auto* label = dynamic_cast<juce::Label*>(child))
            if (label->getTitle() == "Playing mode") mode = label;
        if (auto* control = dynamic_cast<juce::Button*>(child))
        {
            require(control->getTooltip().isNotEmpty() && control->getWantsKeyboardFocus(), "every button has tooltip and keyboard focus");
            auto* accessible = control->getAccessibilityHandler();
            require(accessible && accessible->getTitle().isNotEmpty() && accessible->getHelp().isNotEmpty(),
                    "every button exposes accessible name and help");
        }
    }
    require(bassTab && guitarTab && bassPower && guitarPower && mono && mode, "tabs, powers, Mono and derived status exist");
    require(bassTab->getToggleState() && !guitarTab->getToggleState() && !mono->isVisible(), "Bass page initially selected");
    int numericKnobs = 0;
    for (auto* child : editor->getChildren())
        if (auto* knob = dynamic_cast<NumericKnob*>(child))
        {
            ++numericKnobs;
            const auto before = knob->getValue();
            const juce::MouseEvent doubleClick(juce::Desktop::getInstance().getMainMouseSource(), {}, {},
                0.0f, 0.0f, 0.0f, 0.0f, 0.0f, knob, knob, {}, {}, {}, 2, false);
            knob->mouseDoubleClick(doubleClick);
            juce::Label* box = nullptr;
            for (auto* candidate : knob->getChildren())
                if (auto* label = dynamic_cast<juce::Label*>(candidate)) box = label;
            require(box && box->getCurrentTextEditor(), "double-click opens numerical text editor on every knob");
            auto* entry = box->getCurrentTextEditor();
            require(entry->getHighlightedRegion().getLength() == entry->getText().length(), "current value starts selected");
            entry->setText("garbage", false);
            knob->hideTextBox(false); // Commit path used by the editable Label.
            require(knob->getValue() == before && knob->getTooltip().contains("unchanged"), "invalid input keeps value with feedback");
            require(knob->getValueFromText("99999") == before, "out-of-range numerical input is rejected rather than silently clamped");
            knob->mouseDoubleClick(doubleClick);
            entry = box->getCurrentTextEditor();
            entry->setText(juce::String(knob->getMinimum()), false);
            knob->hideTextBox(true); // Cancellation path; keyboard dispatch belongs to JUCE.
            require(knob->getValue() == before, "cancelling preserves the numerical value");
            knob->mouseDoubleClick(doubleClick);
            entry = box->getCurrentTextEditor();
            entry->setText(juce::String(knob->getMinimum()), false);
            knob->hideTextBox(false); // Commit path used by the editable Label.
            require(std::abs(knob->getValue() - knob->getMinimum()) < 1.0e-6, "committing applies valid boundary through attachment");
            knob->mouseDoubleClick(doubleClick);
            entry = box->getCurrentTextEditor();
            const auto typed = knob->getTitle() == "Output gain" ? 1.234 : knob->getTitle() == "Bass Split" ? 123.456 : 0.321;
            entry->setText(juce::String(typed, 3) + knob->getTextValueSuffix(), false);
            knob->hideTextBox(false);
            require(std::abs(knob->getValue() - typed) < 1.0e-4, "decimal entry with displayed units commits through APVTS");
            knob->setValue(before, juce::sendNotificationSync);
        }
    require(numericKnobs == 3, "all three functional knobs use numerical-entry policy");
    require(slider->getMinimum() == -12.0 && slider->getMaximum() == 6.0, "Output slider uses specified dB range");
    slider->setValue(-9.0, juce::sendNotificationSync);
    require(outputDb(processor) == -9.0f, "editor knob updates APVTS");
    setOutput(processor, 3.0f);
    require(std::abs(slider->getValue() - 3.0) < 1.0e-5, "host automation updates the editor knob");
    require(slider->getTextFromValue(3.0).contains("3.000"), "gain display preserves numerical entry precision");

    {
        IronstackProcessor unchangedPage;
        juce::MemoryBlock settings;
        processor.getStateInformation(settings);
        unchangedPage.setStateInformation(settings.getData(), static_cast<int>(settings.getSize()));
        for (auto* instrument : {&processor, &unchangedPage}) instrument->prepareToPlay(48000.0, 512);
        juce::AudioBuffer<float> selected(2, 512), unchanged(2, 512);
        juce::MidiBuffer a, b;
        a.addEvent(juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(127)), 0);
        a.addEvent(juce::MidiMessage::noteOn(1, 57, static_cast<juce::uint8>(127)), 17);
        b = a;
        processor.processBlock(selected, a);
        unchangedPage.processBlock(unchanged, b);
        for (auto* tab : {guitarTab, bassTab, guitarTab, bassTab})
        {
            tab->onClick();
            processor.processBlock(selected, a);
            unchangedPage.processBlock(unchanged, b);
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < 512; ++sample)
                    require(selected.getSample(channel, sample) == unchanged.getSample(channel, sample),
                            "actual page clicks leave held-note plugin audio byte-exact");
        }
    }

    const auto outputBounds = slider->getBounds(), panicBounds = button->getBounds();
    const auto bassBefore = toggle(processor, ironstack::parameters::bassEnabled);
    guitarTab->onClick();
    require(processor.isGuitarPageSelected() && mono->isVisible() && mono->isEnabled(), "Guitars selection shows Mono");
    require(toggle(processor, ironstack::parameters::bassEnabled) == bassBefore
            && toggle(processor, ironstack::parameters::guitarEnabled), "page selection cannot change layer enables");
    require(slider->getBounds() == outputBounds && button->getBounds() == panicBounds, "common strip geometry does not move between pages");
    rightClickIcon(*bassTab, 70);
    require(toggle(processor, ironstack::parameters::bassEnabled), "right-click outside icon cannot change power");
    rightClickIcon(*bassTab);
    require(!toggle(processor, ironstack::parameters::bassEnabled) && processor.isGuitarPageSelected(),
            "right-click Bass icon changes power without selecting it");
    refreshUi();
    require(bassPower->getButtonText() == "Bass off" && mode->getText().contains("Mode: Guitars"), "disabled power and derived mode have readable text");
    require(!bassPower->getAccessibilityHandler()->getCurrentState().isChecked()
            && bassTab->getAccessibilityHandler()->getDescription().contains("Layer off"), "accessible power and tab states distinguish disable from selection");
    bassPower->setToggleState(true, juce::sendNotificationSync);
    refreshUi();
    require(toggle(processor, ironstack::parameters::bassEnabled), "visible power equals icon right-click");
    rightClickIcon(*guitarTab);
    require(!toggle(processor, ironstack::parameters::guitarEnabled) && processor.isGuitarPageSelected(), "selected disabled page remains selected");
    mono->setToggleState(true, juce::sendNotificationSync);
    require(toggle(processor, ironstack::parameters::guitarMono) && mono->isEnabled(), "disabled guitar page still edits saved Mono");
    setToggle(processor, ironstack::parameters::bassEnabled, false);
    refreshUi();
    require(mode->getText().contains("Mode: Off"), "both-off status is derived, not a third saved control");
    juce::MemoryBlock disabledPage;
    processor.getStateInformation(disabledPage);
    bassTab->onClick();
    processor.setStateInformation(disabledPage.getData(), static_cast<int>(disabledPage.getSize()));
    refreshUi();
    require(guitarTab->getToggleState() && mono->isVisible() && mono->getToggleState()
            && !bassPower->getToggleState() && !guitarPower->getToggleState(), "held editor restores disabled Guitars page, Mono and both-off");
    {
        std::unique_ptr<juce::AudioProcessorEditor> reopened(processor.createEditor());
        require(processor.isGuitarPageSelected(), "closing/reopening editor retains selected page");
        bool recalledMono = false;
        for (auto* child : reopened->getChildren())
            if (auto* control = dynamic_cast<juce::ToggleButton*>(child))
                if (control->getButtonText() == "Guitar Mono") recalledMono = control->isVisible() && control->getToggleState();
        require(recalledMono, "new editor recalls disabled-page settings");
    }
    // The accessible action uses the same attachment/gesture as a mouse click.
    require(guitarPower->getAccessibilityHandler()->getActions().invoke(juce::AccessibilityActionType::toggle), "power exposes accessible toggle");
    refreshUi();
    require(toggle(processor, ironstack::parameters::guitarEnabled), "accessible power action reaches host parameter");
    for (const auto size : {juce::Point<int>{560, 460}, juce::Point<int>{1000, 700}, juce::Point<int>{680, 480}})
    {
        editor->setSize(size.x, size.y);
        for (auto* child : editor->getChildren())
            require(editor->getLocalBounds().contains(child->getBounds()), "resized controls fit bounds on both pages");
        const auto common = slider->getBounds();
        bassTab->onClick();
        require(slider->getBounds() == common, "resizing retains shared geometry on Bass page");
        guitarTab->onClick();
        require(slider->getBounds() == common, "resizing retains shared geometry on Guitars page");
    }
    setToggle(processor, ironstack::parameters::bassEnabled, true);
    setToggle(processor, ironstack::parameters::guitarEnabled, true);
    setToggle(processor, ironstack::parameters::guitarMono, false);
    bassTab->onClick();
    refreshUi();

    processor.prepareToPlay(48000.0, 512);
    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(127)), 0);
    processor.processBlock(audio, midi);
    button->onClick();
    processor.processBlock(audio, midi);
    require(audio.getMagnitude(0, 340, 172) == 0.0f, "editor Panic action reaches audio without touching voice state on UI thread");

    if (snapshot.isNotEmpty())
    {
        setOutput(processor, 0.0f);
        editor->setVisible(true);
        const auto capture = [&](const juce::File& file, int scale)
        {
            // Use CPU images for repeatable offscreen QA: repeated 2x native
            // GPU captures were incomplete on the development machine.
            juce::Image image(juce::Image::ARGB, editor->getWidth() * scale, editor->getHeight() * scale, true,
                              juce::SoftwareImageType{});
            {
                juce::Graphics graphics(image);
                graphics.addTransform(juce::AffineTransform::scale(static_cast<float>(scale)));
                editor->paintEntireComponent(graphics, false);
            }
            require(image.getPixelAt(0, 0) == juce::Colour(0xff17191d), "preview includes the painted editor");
            int paintedOutputPixels = 0;
            const auto bounds = slider->getBounds() * scale;
            for (int y = bounds.getY(); y < bounds.getBottom(); ++y)
                for (int x = bounds.getX(); x < bounds.getRight(); ++x)
                    if (image.getPixelAt(x, y) != juce::Colour(0xff22252a)) ++paintedOutputPixels;
            require(paintedOutputPixels > 100 * scale * scale, "every page/scale preview includes the shared Output control");
            juce::MemoryOutputStream stream;
            require(juce::PNGImageFormat{}.writeImageToStream(image, stream)
                    && file.replaceWithData(stream.getData(), stream.getDataSize()), "editor preview renders");
        };
        const juce::File base(snapshot);
        capture(base, 1);
        capture(base.getSiblingFile("m4.3u2-bass-2x.png"), 2);
        guitarTab->onClick();
        capture(base.getSiblingFile("m4.3u2-guitars.png"), 1);
        capture(base.getSiblingFile("m4.3u2-guitars-2x.png"), 2);
        editor->setSize(560, 460);
        capture(base.getSiblingFile("m4.3u2-guitars-min.png"), 1);
        bassTab->onClick();
        capture(base.getSiblingFile("m4.3u2-bass-min.png"), 1);
    }
    std::cout << "editor_controls/attachments/panic/layout passed\n";
}

void keyboardTests()
{
    IronstackProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    editor->addToDesktop(0);
    juce::Component* guitars = nullptr;
    juce::Button* power = nullptr;
    for (auto* child : editor->getChildren())
    {
        if (auto* tab = dynamic_cast<LayerTab*>(child))
            if (tab->getButtonText() == "Guitars") guitars = tab;
        if (auto* control = dynamic_cast<juce::ToggleButton*>(child))
            if (control->getButtonText() == "Bass on") power = control;
    }
    require(guitars && power, "keyboard controls found");
    bool completed = false;
    // Exercise JUCE's real queued button actions with its normal event loop;
    // production code does not need nested/modal message loops for these tests.
    juce::MessageManager::callAsync([&]
    {
        require(guitars->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)), "tab handles keyboard Enter");
        juce::Timer::callAfterDelay(100, [&]
        {
            require(processor.isGuitarPageSelected() && toggle(processor, ironstack::parameters::bassEnabled),
                    "keyboard selects page without changing sound state");
            require(static_cast<juce::Component&>(*power).keyPressed(juce::KeyPress(juce::KeyPress::returnKey)), "power handles keyboard Enter");
            juce::Timer::callAfterDelay(100, [&]
            {
                require(!toggle(processor, ironstack::parameters::bassEnabled) && processor.isGuitarPageSelected(),
                        "keyboard power toggles layer without changing page");
                require(power->getAccessibilityHandler()->getActions().invoke(juce::AccessibilityActionType::press), "accessible press is available");
                juce::Timer::callAfterDelay(100, [&]
                {
                    require(toggle(processor, ironstack::parameters::bassEnabled), "queued accessible press toggles same host parameter");
                    completed = true;
                    juce::MessageManager::getInstance()->stopDispatchLoop();
                });
            });
        });
    });
    juce::MessageManager::getInstance()->runDispatchLoop();
    require(completed, "keyboard event-loop checks completed");
    std::cout << "editor_keyboard/accessibility_actions passed\n";
}
}

int main(int argc, char** argv)
{
   #if JUCE_LINUX
    X11TestDisplay display;
   #endif
    juce::ScopedJuceInitialiser_GUI gui;
    stateTests();
    processorTests();
    headlessParityTests();
    routingBridgeTests();
    editorTests(argc > 1 ? juce::String(argv[1]) : juce::String{});
    keyboardTests();
    return 0;
}
