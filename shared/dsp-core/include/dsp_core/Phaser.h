#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace dsp_core
{
// Linear, six-stage mono phaser. A convex dry/all-pass blend makes moving
// notches without feedback gain or another nonlinear/oversampling stage.
class Phaser
{
public:
    void prepare(double sampleRate, double lowHz = 350.0, double highHz = 2300.0,
                 double speedHz = 0.27) noexcept
    {
        rate = std::isfinite(sampleRate) ? std::clamp(sampleRate, 44100.0, 192000.0) : 44100.0;
        low = std::clamp(std::isfinite(lowHz) ? lowHz : 350.0, 20.0, rate * 0.1);
        high = std::clamp(std::isfinite(highHz) ? highHz : 2300.0, low, rate * 0.15);
        speed = std::clamp(std::isfinite(speedHz) ? speedHz : 0.27, 0.0, 2.0);
        reset();
    }
    void reset() noexcept
    {
        state.fill(0.0); step.fill(0.0);
        phase = -0.25 * twoPi;
        coefficients = atPhase(phase);
        remaining = 0;
    }
    double next(double input, double mix, bool moving = true) noexcept
    {
        input = std::isfinite(input) ? input : 0.0;
        mix = std::isfinite(mix) ? std::clamp(mix, 0.0, 1.0) : 0.0;
        if (moving && remaining == 0)
        {
            phase += twoPi * speed * controlSamples / rate;
            if (phase >= twoPi) phase -= twoPi;
            const auto target = atPhase(phase);
            for (std::size_t i = 0; i < coefficients.size(); ++i)
                step[i] = (target[i] - coefficients[i]) / controlSamples;
            remaining = controlSamples;
        }
        auto wet = input;
        for (std::size_t i = 0; i < coefficients.size(); ++i)
        {
            if (moving) coefficients[i] += step[i];
            const auto output = quiet(coefficients[i] * wet + state[i]);
            state[i] = quiet(wet - coefficients[i] * output);
            wet = output;
        }
        if (moving) --remaining;
        // Keep history moving even in bypass: opening the blend needs no
        // state reset, and exact bypass preserves analytical source checks.
        return mix == 0.0 ? input : quiet((1.0 - mix) * input + mix * wet);
    }
private:
    static constexpr double twoPi = 6.28318530717958647692;
    static constexpr int controlSamples = 32;
    std::array<double, 6> atPhase(double angle) const noexcept
    {
        const auto center = low * std::pow(high / low, 0.5 + 0.5 * std::sin(angle));
        constexpr std::array<double, 6> ratios{0.5, 0.7, 1.0, 1.4, 2.0, 2.8};
        std::array<double, 6> result{};
        for (std::size_t i = 0; i < result.size(); ++i)
        {
            const auto hz = std::min(center * ratios[i], rate * 0.45);
            const auto k = std::tan(0.5 * twoPi * hz / rate);
            result[i] = (k - 1.0) / (k + 1.0);
        }
        return result;
    }
    static double quiet(double value) noexcept
    {
        return std::isfinite(value) && std::abs(value) >= 1.0e-30 ? value : 0.0;
    }
    std::array<double, 6> state{}, coefficients{}, step{};
    double rate = 44100.0, low = 350.0, high = 2300.0, speed = 0.27, phase = 0.0;
    int remaining = 0;
};
}
