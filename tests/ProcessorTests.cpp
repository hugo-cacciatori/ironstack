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
    require(source.getParameters().size() == 8, "levels, enables, output and three compatibility IDs exist");
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
            const auto expected = juce::String(id) == ironstack::parameters::crossover ? 140.0f : 0.5f;
            juce::MemoryBlock bytes;
            source.getStateInformation(bytes);
            destination.setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
            require(destination.parameterState().getRawParameterValue(id)->load() == expected,
                    "retired voice parameters migrate to fixed production defaults");
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
        for (const auto* id : {ironstack::parameters::bassEnabled, ironstack::parameters::guitarEnabled})
            require(toggle(source, id) == toggle(destination, id), "all boolean values restore exactly");
        for (const auto* id : {ironstack::parameters::output, ironstack::parameters::bassVolume, ironstack::parameters::guitarVolume})
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
    for (const auto* id : {ironstack::parameters::crossover, ironstack::parameters::destruction, ironstack::parameters::guitarMono})
        require(!source.parameterState().getParameter(id)->isAutomatable(), "retired voice controls are not automatable");
    require(!toggle(destination, ironstack::parameters::guitarMono), "old Mono cannot restore centering");
    std::cout << "state round-trips, fixed voice migration and malformed/legacy checks passed\n";
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
            core.setBassParameters(140.0f, 0.5f);
            auto hostMidi = midi;
            processor.processBlock(host, hostMidi);
            ironstack::processMidiBlock(core, offline, midi);
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < count; ++sample)
                    require(host.getSample(channel, sample) == offline.getSample(channel, sample),
                            "headless core exactly matches plugin MIDI/gain across changing blocks");
        }
    }
    // Fixed voicing ignores stale automation while real level automation stays live.
    IronstackProcessor actual, neutral;
    for (auto* processor : {&actual, &neutral}) processor->prepareToPlay(48000, 512);
    for (int block = 0; block < 12; ++block)
    {
        for (const auto* id : {ironstack::parameters::crossover, ironstack::parameters::destruction, ironstack::parameters::guitarMono})
            actual.parameterState().getParameter(id)->setValueNotifyingHost(block % 2 ? 0.0f : 1.0f);
        juce::AudioBuffer<float> a(2, 512), b(2, 512);
        juce::MidiBuffer ma, mb;
        if (block == 0) ma.addEvent(juce::MidiMessage::noteOn(1, 52, static_cast<juce::uint8>(127)), 0);
        mb = ma; actual.processBlock(a, ma); neutral.processBlock(b, mb);
        for (int channel = 0; channel < 2; ++channel) for (int i = 0; i < 512; ++i)
            require(a.getSample(channel, i) == b.getSample(channel, i), "retired parameters cannot change production audio");
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
        reference.prepare(rate);
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
                reference.setGuitarMono(false);
            }
            if (index == 4)
            {
                // Restore through the real host state path while keys are held.
                processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
                reference.setLayersEnabled(bassOn, guitarOn);
                reference.setGuitarMono(false);
            }
            if (index == 5)
            {
                processor.reset();
                reference.reset(0.0f);
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
    // Host solo mode must match the explicit newest-note routing policy.
    IronstackProcessor bassProcessor;
    setToggle(bassProcessor, ironstack::parameters::guitarEnabled, false);
    bassProcessor.prepareToPlay(48000.0, 512);
    ironstack::Engine legacy;
    legacy.setLayersEnabled(true, false);
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
                    "explicit host solo uses newest-note samples, including overlap/handover");
    }
    std::cout << "host_routing_matrix=48 rate/mode/Mono cases; live automation/held recall/page/reset/panic exact\n";
}

void singleKeyModeAudioTests()
{
    double worstError = 0.0;
    for (const double rate : {44100.0, 48000.0, 192000.0})
    for (const int note : {45, 57, 69, 81})
    {
        IronstackProcessor bass, swarm, both;
        setToggle(bass, ironstack::parameters::guitarEnabled, false);
        setToggle(swarm, ironstack::parameters::bassEnabled, false);
        for (auto* processor : {&bass, &swarm, &both}) processor->prepareToPlay(rate, 512);
        juce::AudioBuffer<float> b(2, 512), g(2, 512), mix(2, 512);
        for (int block = 0; block < 16; ++block)
        {
            juce::MidiBuffer mb, mg, mm;
            if (block == 0) mb.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(127)), 0);
            mg = mb; mm = mb;
            if (block == 4 || block == 12)
                for (auto* processor : {&swarm, &both}) setToggle(*processor, ironstack::parameters::guitarEnabled, false);
            if (block == 8)
                for (auto* processor : {&swarm, &both}) setToggle(*processor, ironstack::parameters::guitarEnabled, true);
            bass.processBlock(b, mb); swarm.processBlock(g, mg); both.processBlock(mix, mm);
            for (int channel = 0; channel < 2; ++channel) for (int i = 0; i < 512; ++i)
                worstError = std::max(worstError, std::abs(static_cast<double>(mix.getSample(channel, i))
                    - static_cast<double>(b.getSample(channel, i) + g.getSample(channel, i))));
        }
    }
    require(worstError == 0.0, "actual plugin single-key Both output reconstructs solo Foundation plus Swarm through live power changes");
    std::cout << "plugin_single_key_solo_both_reconstruction_error=" << worstError << " cases=12 octave_change=none_in_dsp\n";
}

void hiddenGainEditorParityTests()
{
    IronstackProcessor reference, visible;
    std::unique_ptr<juce::AudioProcessorEditor> editor(visible.createEditor());
    for (auto* processor : {&reference, &visible}) processor->prepareToPlay(48000, 512);
    for (int block = 0; block < 12; ++block)
    {
        for (auto* processor : {&reference, &visible})
        {
            auto* bass = processor->parameterState().getParameter(ironstack::parameters::bassVolume);
            auto* swarm = processor->parameterState().getParameter(ironstack::parameters::guitarVolume);
            bass->setValueNotifyingHost(bass->convertTo0to1(block < 4 ? -4.0f : block < 8 ? -60.0f : 6.0f));
            swarm->setValueNotifyingHost(swarm->convertTo0to1(block < 4 ? -7.0f : block < 8 ? 6.0f : -60.0f));
        }
        for (auto* child : editor->getChildren())
            if (auto* tab = dynamic_cast<LayerTab*>(child)) tab->onClick();
        juce::AudioBuffer<float> a(2, 512), b(2, 512);
        juce::MidiBuffer ma, mb;
        if (block == 0)
            for (int note : {45, 52, 57}) ma.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(127)), 0);
        mb = ma;
        reference.processBlock(a, ma); visible.processBlock(b, mb);
        for (int channel = 0; channel < 2; ++channel) for (int i = 0; i < 512; ++i)
            require(a.getSample(channel, i) == b.getSample(channel, i), "editor and page selection preserve exact audio during hidden-gain automation");
    }
    restoreXml(visible, "<IronstackState><PARAM id='output_db' value='0'/></IronstackState>");
    for (const auto* id : {ironstack::parameters::bassVolume, ironstack::parameters::guitarVolume})
        require(visible.parameterState().getRawParameterValue(id)->load() == 0.0f, "missing legacy hidden gains default to unity with editor open");
    std::cout << "hidden_gain_editor_audio_parity=exact; silent/nondefault/maximum automation and missing-gain recall passed\n";
}

void editorTests(const juce::String& snapshot)
{
    IronstackProcessor processor;
    auto setLevel = [&processor](const char* id, float db)
    {
        auto* parameter = processor.parameterState().getParameter(id);
        require(parameter->isAutomatable(), "hidden gains remain automatable");
        parameter->setValueNotifyingHost(parameter->convertTo0to1(db));
    };
    setLevel(ironstack::parameters::bassVolume, -4.0f);
    setLevel(ironstack::parameters::guitarVolume, -60.0f);
    struct HostChanges final : juce::AudioProcessorListener
    {
        int parameters = 0, state = 0;
        void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override { ++parameters; }
        void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override { ++state; }
    } hostChanges;
    processor.addListener(&hostChanges);
    juce::MemoryBlock beforeOpen, afterOpen;
    processor.getStateInformation(beforeOpen);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    processor.getStateInformation(afterOpen);
    require(beforeOpen == afterOpen, "opening editor preserves nondefault and silent hidden gains and state");
    require(hostChanges.parameters == 0 && hostChanges.state == 0, "opening editor does not notify host of parameter or state changes");
    require(editor && editor->getWidth() == 680 && editor->getHeight() == 400, "compact resizable editor");
    editor->setTopLeftPosition(-10000, -10000);
    editor->addToDesktop(0);
    editor->setVisible(true);
    LayerTab *foundation = nullptr, *swarm = nullptr;
    NumericKnob* output = nullptr;
    int knobCount = 0;
    juce::TextButton* panic = nullptr;
    juce::Label* footer = nullptr;
    for (auto* child : editor->getChildren())
    {
        if (auto* tab = dynamic_cast<LayerTab*>(child))
            (tab->getButtonText() == "Foundation" ? foundation : swarm) = tab;
        if (auto* knob = dynamic_cast<NumericKnob*>(child))
        {
            ++knobCount;
            if (knob->getTitle() == "Output gain") output = knob;
        }
        if (auto* button = dynamic_cast<juce::TextButton*>(child))
            if (button->getButtonText() == "Panic") panic = button;
        if (auto* label = dynamic_cast<juce::Label*>(child))
        {
            if (label->getTitle() == "Control description") footer = label;
            require(label->getText() != "Level", "no per-layer Level labels");
        }
        require(dynamic_cast<juce::ToggleButton*>(child) == nullptr, "no power checkboxes or Mono control");
        if (auto* button = dynamic_cast<juce::Button*>(child))
        {
            auto* handler = button->getAccessibilityHandler();
            require(button->getTooltip().isNotEmpty() && button->getWantsKeyboardFocus()
                && handler && handler->getHelp().isNotEmpty(), "all buttons have accessible help and focus");
        }
    }
    require(foundation && swarm && output && panic && footer && knobCount == 1, "tabs, only master knob, panic and footer exist");
    require(foundation->getToggleState(), "Foundation page selected initially");
    const auto outputBounds = output->getBounds();
    swarm->onClick();
    require(processor.isGuitarPageSelected() && swarm->getToggleState(), "Swarm page selects independently");
    require(output->getBounds() == outputBounds && toggle(processor, ironstack::parameters::bassEnabled), "page selection preserves common controls and power");
    rightClickIcon(*foundation, 70);
    require(toggle(processor, ironstack::parameters::bassEnabled), "right-click label does not toggle power");
    rightClickIcon(*foundation);
    require(!toggle(processor, ironstack::parameters::bassEnabled) && processor.isGuitarPageSelected(), "right-click icon toggles power without selecting page");
    require(!foundation->getAccessibilityHandler()->getCurrentState().isChecked(), "accessible power state updates immediately");
    foundation->onClick();
    require(foundation->getToggleState() && foundation->isEnabled(), "disabled page remains selectable");
    require(foundation->getAccessibilityHandler()->getActions().invoke(juce::AccessibilityActionType::toggle), "accessible power toggle exists");
    require(toggle(processor, ironstack::parameters::bassEnabled), "accessible toggle changes the same host parameter");
    for (auto* tab : {foundation, swarm})
    for (const bool selected : {false, true}) for (const bool highlighted : {false, true})
    for (const bool focused : {false, true})
    {
        // Power must preserve every painted pixel outside the icon and its shape inside it.
        (focused ? static_cast<juce::Component*>(tab) : static_cast<juce::Component*>(output))->grabKeyboardFocus();
        require(tab->hasKeyboardFocus(false) == focused, "power-paint comparison covers focused and unfocused tabs");
        tab->setToggleState(selected, juce::dontSendNotification);
        juce::Image on(juce::Image::ARGB, tab->getWidth(), tab->getHeight(), true);
        juce::Image off(on.createCopy());
        tab->setLayerEnabled(true);
        { juce::Graphics g(on); tab->paintButton(g, highlighted, false); }
        tab->setLayerEnabled(false);
        { juce::Graphics g(off); tab->paintButton(g, highlighted, false); }
        const auto background = selected ? juce::Colour(0xff292d33)
            : highlighted ? juce::Colour(0xff22262c) : juce::Colour(0xff1e2126);
        int changed = 0;
        for (int y = 0; y < on.getHeight(); ++y) for (int x = 0; x < on.getWidth(); ++x)
        {
            const auto a = on.getPixelAt(x, y), b = off.getPixelAt(x, y);
            if (x >= 38) require(a == b, "power does not change label or selected/hover/focus surfaces");
            else require((a == background) == (b == background), "power preserves icon silhouette without slash or badge");
            if (a != b) ++changed;
        }
        require(changed > 0, "power changes icon color");
    }
    foundation->onClick();
    for (auto* knob : {output})
    {
        require(knob->getTextBoxPosition() == juce::Slider::NoTextBox && !knob->valueEditor().isVisible(), "no persistent numeric field");
        knob->setValue(-6.0, juce::sendNotificationSync);
        const juce::MouseEvent doubleClick(juce::Desktop::getInstance().getMainMouseSource(), {}, {},
            0, 0, 0, 0, 0, knob, knob, {}, {}, {}, 2, false);
        knob->mouseDoubleClick(doubleClick);
        require(knob->getValue() == 0.0 && !knob->valueEditor().isVisible(), "double-click resets default without opening entry");
        const juce::MouseEvent rightClick(juce::Desktop::getInstance().getMainMouseSource(), {},
            juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier), 0, 0, 0, 0, 0, knob, knob, {}, {}, {}, 1, false);
        knob->mouseDown(rightClick); knob->mouseUp(rightClick);
        auto& entry = knob->valueEditor();
        require(entry.isVisible() && entry.hasKeyboardFocus(true), "right-click opens focused entry");
        require(entry.getHighlightedRegion().getLength() == entry.getText().length(), "entry selects the full current value");
        entry.setText("-3.125 dB", false); entry.onReturnKey();
        require(std::abs(knob->getValue() + 3.125) < 1e-5 && !entry.isVisible(), "Enter commits decimal units and hides field");
        knob->openEntry(); entry.setText("-4.25", false); entry.onFocusLost();
        require(std::abs(knob->getValue() + 4.25) < 1e-5 && !entry.isVisible(), "focus loss commits and hides field");
        knob->openEntry(); entry.setText("-5.5", false); knob->commitIfOutside(panic);
        require(std::abs(knob->getValue() + 5.5) < 1e-5 && !entry.isVisible(), "click-away commits even on a non-focus-taking control");
        knob->openEntry(); entry.setText("-10", false); entry.onEscapeKey();
        require(std::abs(knob->getValue() + 5.5) < 1e-5, "Escape cancels");
        for (const auto* invalid : {"garbage", "99999", "nan", "2.2.2"})
        {
            knob->openEntry(); entry.setText(invalid, false); entry.onReturnKey();
            require(std::abs(knob->getValue() + 5.5) < 1e-5 && !entry.isVisible(), "invalid values preserve setting and close field");
        }
        knob->setValue(0.0, juce::sendNotificationSync);
    }
    output->setValue(-9.0, juce::sendNotificationSync);
    require(outputDb(processor) == -9.0f, "output writes through APVTS");
    setOutput(processor, 3.0f);
    require(std::abs(output->getValue() - 3.0) < 1e-5, "host updates output knob");
    setLevel(ironstack::parameters::guitarVolume, -7.0f);
    rightClickIcon(*swarm);
    swarm->onClick();
    juce::MemoryBlock settings;
    processor.getStateInformation(settings);
    IronstackProcessor recalled;
    recalled.setStateInformation(settings.getData(), static_cast<int>(settings.getSize()));
    require(recalled.isGuitarPageSelected() && !toggle(recalled, ironstack::parameters::guitarEnabled)
        && recalled.parameterState().getRawParameterValue(ironstack::parameters::bassVolume)->load() == -4.0f
        && recalled.parameterState().getRawParameterValue(ironstack::parameters::guitarVolume)->load() == -7.0f,
        "disabled-page power, levels and selected page restore");
    for (const auto size : {juce::Point<int>{560, 360}, juce::Point<int>{1000, 600}, juce::Point<int>{680, 400}})
    {
        editor->setSize(size.x, size.y);
        const auto children = editor->getChildren();
        for (auto* child : children)
        {
            require(editor->getLocalBounds().contains(child->getBounds()), "all resized controls fit");
            for (auto* other : children)
                if (child != other) require(!child->getBounds().intersects(other->getBounds()), "controls never overlap");
        }
        require(output->getBottom() < foundation->getY() && output->getBottom() < swarm->getY(), "Output entirely above tabs");
        require(output->getLocalBounds().contains(output->valueEditor().getBounds()), "temporary Output entry fits above tabs");
        const auto common = output->getBounds();
        foundation->onClick(); swarm->onClick();
        require(output->getBounds() == common && footer->getHeight() == 26, "stable output and one-line footer geometry");
    }
    for (int powers = 0; powers < 4; ++powers) for (const bool selected : {false, true})
    {
        setToggle(processor, ironstack::parameters::bassEnabled, (powers & 1) != 0);
        setToggle(processor, ironstack::parameters::guitarEnabled, (powers & 2) != 0);
        std::array<float, 8> values{};
        for (int i = 0; i < 8; ++i) values[static_cast<std::size_t>(i)] = processor.getParameters()[i]->getValue();
        const auto parameterNotifications = hostChanges.parameters;
        (selected ? swarm : foundation)->onClick();
        require(hostChanges.parameters == parameterNotifications, "page selection sends no host parameter notification");
        for (int i = 0; i < 8; ++i)
            require(values[static_cast<std::size_t>(i)] == processor.getParameters()[i]->getValue(), "selecting any powered/disabled page writes no audio parameters");
        for (auto* tab : {foundation, swarm})
        {
            const auto state = tab->getAccessibilityHandler()->getCurrentState();
            require(state.isChecked() == tab->isLayerEnabled() && state.isSelected() == tab->getToggleState(),
                    "accessible power and selection remain independent in every combination");
            require(tab->getDescription().contains(tab->isLayerEnabled() ? "Layer on." : "Layer off; still editable."),
                    "accessible description provides non-color power status");
        }
    }
    // Keyboard focus gives the same footer text without requiring synthetic mouse movement.
    output->grabKeyboardFocus(); refreshUi();
    require(footer->getText().isNotEmpty() && !footer->getText().containsChar('\n'), "footer gives one-line contextual help");
    setToggle(processor, ironstack::parameters::bassEnabled, true);
    setToggle(processor, ironstack::parameters::guitarEnabled, true);
    processor.prepareToPlay(48000, 512);
    juce::AudioBuffer<float> audio(2, 512); juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(127)), 0);
    processor.processBlock(audio, midi); panic->onClick(); processor.processBlock(audio, midi);
    require(audio.getMagnitude(0, 340, 172) == 0.0f, "Panic clears audio through realtime request");
    if (snapshot.isNotEmpty())
    {
        setOutput(processor, 0);
        const juce::File base(snapshot);
        for (const auto size : {juce::Point<int>{560, 360}, juce::Point<int>{680, 400}, juce::Point<int>{1000, 600}})
        for (int powers = 0; powers < 4; ++powers)
        for (const bool selected : {false, true}) for (int scale : {1, 2})
        {
            editor->setSize(size.x, size.y);
            setToggle(processor, ironstack::parameters::bassEnabled, (powers & 1) != 0);
            setToggle(processor, ironstack::parameters::guitarEnabled, (powers & 2) != 0);
            (selected ? swarm : foundation)->onClick();
            juce::Image image(juce::Image::ARGB, editor->getWidth()*scale, editor->getHeight()*scale, true, juce::SoftwareImageType{});
            { juce::Graphics g(image); g.addTransform(juce::AffineTransform::scale(static_cast<float>(scale))); editor->paintEntireComponent(g, false); }
            juce::MemoryOutputStream stream;
            const auto file = base.getSiblingFile(juce::String("ui2-") + (selected ? "swarm" : "foundation")
                + "-power" + juce::String(powers) + "-" + juce::String(size.x) + "-" + juce::String(scale) + "x.png");
            require(juce::PNGImageFormat{}.writeImageToStream(image, stream) && file.replaceWithData(stream.getData(), stream.getDataSize()), "UI snapshot saved");
        }
    }
    processor.getStateInformation(beforeOpen);
    const auto parameterNotifications = hostChanges.parameters, stateNotifications = hostChanges.state;
    editor.reset();
    processor.getStateInformation(afterOpen);
    require(beforeOpen == afterOpen, "closing editor preserves state and hidden gains");
    require(hostChanges.parameters == parameterNotifications && hostChanges.state == stateNotifications,
            "closing editor does not dirty host state or parameters");
    processor.removeListener(&hostChanges);
    std::cout << "tab power, value entry, defaults, recall, footer and layout passed\n";
}

void keyboardTests()
{
    IronstackProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    editor->setTopLeftPosition(-10000, -10000);
    editor->addToDesktop(0);
    LayerTab *foundation = nullptr, *swarm = nullptr;
    for (auto* child : editor->getChildren()) if (auto* tab = dynamic_cast<LayerTab*>(child))
        (tab->getButtonText() == "Foundation" ? foundation : swarm) = tab;
    require(foundation && swarm, "keyboard tabs exist");
    bool completed = false;
    juce::MessageManager::callAsync([&]
    {
        require(swarm->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)), "Enter selects tab");
        juce::Timer::callAfterDelay(100, [&]
        {
            require(processor.isGuitarPageSelected() && toggle(processor, ironstack::parameters::guitarEnabled), "keyboard tab selection is audio neutral");
            require(swarm->keyPressed(juce::KeyPress(juce::KeyPress::returnKey, juce::ModifierKeys::altModifier, 0)), "Alt+Enter toggles power");
            require(!toggle(processor, ironstack::parameters::guitarEnabled) && processor.isGuitarPageSelected(), "keyboard power leaves page selected");
            require(foundation->getAccessibilityHandler()->getActions().invoke(juce::AccessibilityActionType::press),
                    "accessible press selects page separately from power toggle");
            juce::Timer::callAfterDelay(100, [&]
            {
                require(!processor.isGuitarPageSelected() && toggle(processor, ironstack::parameters::bassEnabled)
                    && !toggle(processor, ironstack::parameters::guitarEnabled), "accessible selection leaves both power values unchanged");
                completed = true;
                juce::MessageManager::getInstance()->stopDispatchLoop();
            });
        });
    });
    juce::MessageManager::getInstance()->runDispatchLoop();
    require(completed, "keyboard checks completed");
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
    singleKeyModeAudioTests();
    hiddenGainEditorParityTests();
    editorTests(argc > 1 ? juce::String(argv[1]) : juce::String{});
    keyboardTests();
    return 0;
}
