#include <dsp_core/LeftChannelMono.h>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}
int main()
{
    double maximumRipple = 0.0, maximumStep = 0.0;
    for (int rate : {44100, 48000, 88200, 96000, 176400, 192000})
    {
        dsp_core::LeftChannelMono mono;
        mono.prepare(rate);
        for (int i = 0; i < 1000; ++i)
        {
            const auto a = i * 0.00001, b = -i * 0.00003;
            const auto out = mono.next(a, b);
            require(out.left == a && out.right == b, "disabled path is exactly unchanged");
        }
        mono.setEnabled(true); mono.reset();
        for (int i = 0; i < rate; ++i)
        {
            const auto out = mono.next(0.0, 0.4);
            require(out.left == 0.0 && out.right == 0.0, "right-only input cannot reach mono audio");
        }
        mono.reset();
        for (int i = 0; i < rate * 3; ++i) mono.next(0.1, 0.2);
        require(std::abs(mono.gain() - std::sqrt(2.5)) < 0.0001, "paired-channel mean power gives analytic compensation");
        const auto frozen = mono.gain();
        for (int i = 0; i < rate; ++i) mono.next(0.0, 0.0);
        require(mono.gain() == frozen, "silence freezes compensation exactly");
        for (const auto ratio : {0.0, 1.0, 1000.0})
        {
            mono.reset();
            for (int i = 0; i < rate * 3; ++i)
            {
                const auto previousGain = mono.gain();
                const auto out = mono.next(0.001, 0.001 * ratio);
                require(std::abs(20.0 * std::log10(mono.gain() / previousGain)) * rate <= 6.000001,
                        "compensation slew never exceeds 6 dB per second");
                require(out.left == out.right && out.left == 0.001 * mono.gain(), "mono is a gain-scaled left sample only");
                require(mono.gain() >= std::pow(10.0, -6.0 / 20.0) && mono.gain() <= std::pow(10.0, 6.0 / 20.0),
                        "compensation remains within +/-6 dB");
            }
        }
        mono.reset();
        double minimum = 10.0, maximum = 0.0, previous = 0.0;
        for (int i = 0; i < rate * 3; ++i)
        {
            const auto phase = 6.28318530717958647692 * 220.0 * i / rate;
            const auto out = mono.next(0.1 * std::sin(phase), 0.1 * std::cos(phase));
            if (i > rate * 2) { minimum = std::min(minimum, mono.gain()); maximum = std::max(maximum, mono.gain()); }
            if (i % (rate / 10) == 0) mono.setEnabled((i / (rate / 10)) % 2 == 0);
            maximumStep = std::max(maximumStep, std::abs(out.right - previous)); previous = out.right;
        }
        maximumRipple = std::max(maximumRipple, 20.0 * std::log10(maximum / minimum));
        require(maximumRipple < 0.05 && maximumStep < 0.01, "steady matching/toggles avoid gain ripple and large discontinuities");
        for (auto input : {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::max()})
        {
            const auto out = mono.next(input, -input);
            require(std::isfinite(out.left) && std::isfinite(out.right), "invalid/extreme inputs stay finite");
        }
    }
    std::cout << "left_mono_rates=6 provenance=exact gain_bound_db=6 ripple_db=" << maximumRipple
              << " toggle_step=" << maximumStep << '\n';
}
