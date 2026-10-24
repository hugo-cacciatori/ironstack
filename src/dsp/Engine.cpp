#include "Engine.h"

#include <algorithm>
#include <cmath>

namespace ironstack
{
void Engine::prepare(double sampleRate) noexcept
{
    rate = std::isfinite(sampleRate) && sampleRate >= 44100.0 && sampleRate <= 192000.0
        ? sampleRate : 44100.0;
    attackSamples = static_cast<int>(std::ceil(rate * 0.001));
    releaseSamples = static_cast<int>(std::ceil(rate * releaseSeconds));
    transitionSamples = static_cast<int>(std::ceil(rate * 0.002));
    fadeSamples = static_cast<int>(std::ceil(rate * 0.005));
    reset();
}

void Engine::reset() noexcept
{
    serial = 0;
    for (auto& wheel : bend)
        wheel.reset(1.0);
    bassVoice.prepare(rate);
    for (auto& bus : guitarBus) bus.prepare(rate);
    guitarMono.prepare(rate);
    clearGuitar();
    bassChannel = 1;
    bassBendOffset.reset(0.0);
    bassNotes.reset();
    bassNotes.setPolicy(!routingConfigured ? BassNotes::Policy::locked
        : guitarEnabled ? BassNotes::Policy::lowest : BassNotes::Policy::newest);
    bassMixLevel.reset(bassMixTarget);
    guitarMixLevel.reset(guitarMixTarget);
    bassFade.reset(bassEnabled ? 1.0 : 0.0);
    guitarFade.reset(guitarEnabled ? 1.0 : 0.0);
    bassSuspended = !bassEnabled;
    guitarSuspended = !guitarEnabled;
}

void Engine::clearGuitar() noexcept
{
    for (auto& voice : voices)
    {
        voice = Voice{};
        voice.oscillators.prepare(rate);
        voice.envelope.prepare(attackSamples, releaseSamples);
    }
    for (auto& bus : guitarBus) bus.reset();
    guitarMono.reset();
    guitarLevel.reset(1.0); guitarLevelTarget = 1.0; guitarWasActive = false;
}

void Engine::setLayersEnabled(bool bass, bool guitar) noexcept
{
    if (routingConfigured && bass == bassEnabled && guitar == guitarEnabled) return;
    routingConfigured = true;
    const auto bassWasEnabled = bassEnabled;
    const auto guitarWasEnabled = guitarEnabled;
    bassEnabled = bass;
    guitarEnabled = guitar;
    const auto change = bassNotes.setPolicy(guitar ? BassNotes::Policy::lowest : BassNotes::Policy::newest);
    if (bass != bassWasEnabled)
    {
        bassFade.setTarget(bass ? 1.0 : 0.0, fadeSamples);
        if (bass)
        {
            bassSuspended = false;
            updateBass(bassNotes.target().note >= 0 ? BassNotes::Change::attack : BassNotes::Change::release);
        }
    }
    else if (bass) updateBass(change);
    if (guitar != guitarWasEnabled)
    {
        guitarFade.setTarget(guitar ? 1.0 : 0.0, fadeSamples);
        if (guitar)
        {
            guitarSuspended = false;
            // Preserve continuity during a rapid reversal; a fully faded layer
            // already had its history cleared at zero gain.
            for (auto& voice : voices)
                if (voice.keyDown) { voice.keyDown = false; voice.envelope.stop(); }
            bassNotes.forEachNewestHeldKey(guitarVoiceCount, [this](int channel, int note, int velocity) noexcept
            {
                startGuitar(channel, note, velocity);
            });
        }
    }
}

void Engine::noteOn(int channel, int note, int velocity) noexcept
{
    if (channel < 1 || channel > 16 || note < 0 || note > 127)
        return;
    if (velocity <= 0)
    {
        noteOff(channel, note);
        return;
    }
    updateBass(bassNotes.noteOn(channel, note, velocity));
    if (guitarEnabled) startGuitar(channel, note, velocity);
}

void Engine::startGuitar(int channel, int note, int velocity) noexcept
{
    auto selected = voices.end();
    for (auto it = voices.begin(); it != voices.end(); ++it)
        if (it->active() && it->channel == channel && it->note == note)
        {
            selected = it;
            break;
        }
    if (selected == voices.end())
        selected = std::find_if(voices.begin(), voices.end(), [](const Voice& v) { return !v.active(); });
    if (selected == voices.end())
        selected = std::min_element(voices.begin(), voices.end(), [](const Voice& a, const Voice& b)
        {
            // Reuse released voices before interrupting a held note, then oldest first.
            if (a.keyDown != b.keyDown)
                return !a.keyDown;
            return a.age < b.age;
        });
    auto& voice = *selected;
    voice.transitionStart = voice.lastOutput;
    voice.transitionRemaining = transitionSamples;
    voice.oscillators.reset(channel, note);
    voice.envelope.start();
    voice.frequency = 440.0 * std::exp2((note - 69.0) / 12.0);
    voice.velocity = std::min(velocity, 127) / 127.0;
    voice.channel = channel;
    voice.note = note;
    voice.keyDown = true;
    voice.age = ++serial;
}

void Engine::noteOff(int channel, int note) noexcept
{
    updateBass(bassNotes.noteOff(channel, note));
    for (auto& voice : voices)
        if (voice.keyDown && voice.channel == channel && voice.note == note)
        {
            voice.keyDown = false;
            voice.envelope.stop();
        }
}

void Engine::allNotesOff(int channel) noexcept
{
    updateBass(bassNotes.allNotesOff(channel));
    for (auto& voice : voices)
        if (voice.channel == channel && voice.keyDown)
        {
            voice.keyDown = false;
            voice.envelope.stop();
        }
}

void Engine::updateBass(BassNotes::Change change) noexcept
{
    if (!bassEnabled) return;
    if (change == BassNotes::Change::attack)
    {
        const auto target = bassNotes.target();
        bassChannel = target.channel;
        bassBendOffset.reset(0.0);
        bassVoice.start(target.frequency, target.velocity);
    }
    else if (change == BassNotes::Change::retarget)
    {
        const auto target = bassNotes.target();
        const auto oldRatio = bend[static_cast<std::size_t>(bassChannel - 1)].value() + bassBendOffset.value();
        bassChannel = target.channel;
        bassBendOffset.reset(oldRatio - bend[static_cast<std::size_t>(bassChannel - 1)].value());
        bassBendOffset.setTarget(0.0, transitionSamples);
        bassVoice.retargetVelocity(target.velocity);
    }
    else if (change == BassNotes::Change::release)
    {
        bassVoice.stop();
    }
}

void Engine::pitchBend(int channel, int wheel) noexcept
{
    if (channel >= 1 && channel <= 16)
    {
        const auto position = std::clamp(wheel, 0, 16383) - 8192;
        const auto semitones = 2.0 * position / (position < 0 ? 8192.0 : 8191.0);
        bend[static_cast<std::size_t>(channel - 1)].setTarget(std::exp2(semitones / 12.0), transitionSamples);
    }
}

void Engine::setLayerLevels(float bassDb, float guitarDb) noexcept
{
    const auto gain = [](float db) noexcept
    {
        db = std::isfinite(db) ? std::clamp(db, -60.0f, 6.0f) : 0.0f;
        return db <= -60.0f ? 0.0 : std::pow(10.0, db / 20.0);
    };
    const auto bass = gain(bassDb), guitar = gain(guitarDb);
    if (bass != bassMixTarget) { bassMixTarget = bass; bassMixLevel.setTarget(bass, fadeSamples); }
    if (guitar != guitarMixTarget) { guitarMixTarget = guitar; guitarMixLevel.setTarget(guitar, fadeSamples); }
}

void Engine::process(float* left, float* right, int numSamples, LayerMask outputMask, LayerTaps taps,
                     BassComponent component) noexcept
{
    for (int sample = 0; sample < numSamples; ++sample)
    {
        std::array<double, 16> ratios{};
        for (std::size_t channel = 0; channel < bend.size(); ++channel)
            ratios[channel] = bend[channel].next();
        const auto guitar = guitarSuspended ? GuitarOscillators::Stereo{} : nextGuitar(ratios);
        const auto guitarGain = guitarFade.next() * guitarMixLevel.next();
        const auto guitarLeft = static_cast<float>(guitar.left * guitarGain);
        const auto guitarRight = static_cast<float>(guitar.right * guitarGain);
        const auto bassGain = bassFade.next() * bassMixLevel.next();
        const auto bass = bassSuspended ? 0.0f : static_cast<float>(bassGain *
            bassVoice.next(ratios[static_cast<std::size_t>(bassChannel - 1)] + bassBendOffset.next(), component).output);
        // Once inaudible, discard histories and stop synthesis. MIDI tracking
        // continues so re-entry uses held keys, never stale oscillator tails.
        if (!bassEnabled && bassGain == 0.0 && !bassSuspended)
        {
            bassVoice.reset(); bassBendOffset.reset(0.0); bassSuspended = true;
        }
        if (!guitarEnabled && guitarGain == 0.0 && !guitarSuspended)
        {
            clearGuitar(); guitarSuspended = true;
        }
        if (taps.bassLeft) taps.bassLeft[sample] = bass;
        if (taps.bassRight) taps.bassRight[sample] = bass;
        if (taps.guitarLeft) taps.guitarLeft[sample] = guitarLeft;
        if (taps.guitarRight) taps.guitarRight[sample] = guitarRight;
        const auto mask = static_cast<unsigned>(outputMask);
        left[sample] = ((mask & 1u) != 0 ? bass : 0.0f) + ((mask & 2u) != 0 ? guitarLeft : 0.0f);
        right[sample] = ((mask & 1u) != 0 ? bass : 0.0f) + ((mask & 2u) != 0 ? guitarRight : 0.0f);
    }
}

GuitarOscillators::Stereo Engine::nextGuitar(const std::array<double, 16>& ratios) noexcept
{
    GuitarOscillators::Stereo sum;
    double guitarEnvelope = 0.0;
    double guitarVelocity = 0.0;
    for (auto& voice : voices)
    {
        if (!voice.active())
        {
            voice.lastOutput = {};
            continue;
        }
        auto output = voice.oscillators.next(voice.frequency * ratios[static_cast<std::size_t>(voice.channel - 1)]);
        const auto envelope = voice.envelope.next();
        guitarVelocity = std::max(guitarVelocity, voice.velocity);
        guitarEnvelope = std::max(guitarEnvelope, envelope);
        const auto gain = envelope * voice.velocity;
        output.left *= gain;
        output.right *= gain;
        if (voice.transitionRemaining > 0)
        {
            // A convex blend from the last sample bounds even rapid steals,
            // without resetting a live waveform abruptly or adding a clipper.
            const auto oldWeight = static_cast<double>(voice.transitionRemaining--) / transitionSamples;
            guitarEnvelope = std::max(guitarEnvelope, oldWeight);
            output.left = oldWeight * voice.transitionStart.left + (1.0 - oldWeight) * output.left;
            output.right = oldWeight * voice.transitionStart.right + (1.0 - oldWeight) * output.right;
        }
        voice.lastOutput = output;
        sum.left += output.left;
        sum.right += output.right;
    }
    // Factor the strongest note's velocity out of the nonlinear drive and
    // restore it afterwards: single-note velocity changes loudness only.
    // Other chord notes retain their relative weights in the summed bus.
    // Smooth reference changes when a louder/quieter note joins or leaves.
    if (guitarVelocity > 0.0)
    {
        if (!guitarWasActive) guitarLevel.reset(guitarVelocity);
        else if (guitarVelocity != guitarLevelTarget) guitarLevel.setTarget(guitarVelocity, transitionSamples);
        guitarLevelTarget = guitarVelocity;
    }
    guitarWasActive = guitarVelocity > 0.0;
    const auto level = guitarLevel.next();
    const auto processedLeft = guitarBus[0].next(sum.left / level, guitarEnvelope, level);
    const auto processedRight = guitarBus[1].next(sum.right / level, guitarEnvelope, level);
    // Width still comes from independent synthesis. This final bounded
    // blend reserves common energy after nonlinear processing as well.
    const auto guitarMid = 0.5 * (processedLeft + processedRight);
    const auto guitarSide = 0.5 * (processedLeft - processedRight) * GuitarOscillators::spread;
    const auto guitar = guitarMono.next(guitarMid + guitarSide, guitarMid - guitarSide);
    return {guitar.left, guitar.right};
}

std::array<Engine::GuitarTarget, Engine::guitarVoiceCount> Engine::guitarTargets() const noexcept
{
    std::array<GuitarTarget, guitarVoiceCount> result{};
    for (std::size_t i = 0; i < voices.size(); ++i)
    {
        const auto& v = voices[i];
        result[i] = {v.channel, v.note, v.channel > 0 ? v.frequency * bend[static_cast<std::size_t>(v.channel - 1)].value() : 0.0,
                     v.keyDown, v.active()};
    }
    return result;
}
}
