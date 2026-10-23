#pragma once

#include "LinearRamp.h"
#include <algorithm>
#include <cmath>

namespace dsp_core
{
// Select left audio only; right supplies a power reference, never mono audio.
// Keep the estimator running in stereo so a live toggle has a warm reference.
class LeftChannelMono
{
public:
    struct Stereo { double left, right; };
    void prepare(double sampleRate) noexcept
    {
        const auto rate = std::isfinite(sampleRate) && sampleRate >= 44100.0 && sampleRate <= 192000.0
            ? sampleRate : 44100.0;
        coefficient = 1.0 - std::exp(-1.0 / (rate * 0.250));
        maximumGainRatio = std::pow(10.0, 6.0 / (20.0 * rate));
        transitionSamples = static_cast<int>(std::ceil(rate * 0.005));
        reset();
    }
    void reset() noexcept
    {
        leftPower = rightPower = 0.0;
        compensation = 1.0;
        blend.reset(enabled ? 1.0 : 0.0);
    }
    void setEnabled(bool value) noexcept
    {
        if (value != enabled)
        {
            enabled = value;
            blend.setTarget(value ? 1.0 : 0.0, transitionSamples);
        }
    }
    double gain() const noexcept { return compensation; }
    Stereo next(double left, double right) noexcept
    {
        left = std::isfinite(left) ? std::clamp(left, -64.0, 64.0) : 0.0;
        right = std::isfinite(right) ? std::clamp(right, -64.0, 64.0) : 0.0;
        // Freeze both the power reference and gain below the silence floor.
        // This avoids amplifying numerical tails and preserves the next attack.
        if (left * left + right * right > 1.0e-10)
        {
            leftPower += coefficient * (left * left - leftPower);
            rightPower += coefficient * (right * right - rightPower);
            if (leftPower > 1.0e-12)
            {
                const auto target = std::sqrt(std::clamp((leftPower + rightPower) / (2.0 * leftPower),
                                                        0.251188643150958, 3.981071705534972));
                const auto smoothed = compensation + coefficient * (target - compensation);
                compensation = std::clamp(smoothed, compensation / maximumGainRatio,
                                           compensation * maximumGainRatio);
            }
        }
        const auto amount = blend.next();
        if (amount == 0.0) return {left, right}; // Exact stereo baseline null.
        const auto mono = left * compensation;
        if (amount == 1.0) return {mono, mono}; // Exact centered output/provenance.
        return {left + amount * (mono - left), right + amount * (mono - right)};
    }
private:
    LinearRamp blend;
    double leftPower = 0.0, rightPower = 0.0, compensation = 1.0, coefficient = 0.00009;
    double maximumGainRatio = 1.00002; // At most 6 dB/second, including abnormal imbalance.
    int transitionSamples = 221;
    bool enabled = false;
};
}
