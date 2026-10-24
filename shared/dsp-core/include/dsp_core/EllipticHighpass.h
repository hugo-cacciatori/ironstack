#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace dsp_core
{
// Seventh-order high-pass: <=0.1 dB passband ripple, >=60 dB stopband
// rejection at <=5/7 of the passband edge. The fixed analogue prototype is
// transformed at prepare, not designed or allocated in the audio callback.
// tools/design_separation_guard.py documents an independent offline oracle.
class EllipticHighpass
{
public:
    static constexpr double rippleDb = 0.1, rejectionDb = 60.0;
    void prepare(double passbandHz, double sampleRate) noexcept
    {
        const auto rate = std::isfinite(sampleRate) && sampleRate >= 44100.0 && sampleRate <= 192000.0
            ? sampleRate : 44100.0;
        const auto hz = std::clamp(std::isfinite(passbandHz) ? passbandHz : 350.0, 10.0, rate * 0.45);
        const auto t = std::tan(3.14159265358979323846 * hz / rate);
        // Low-pass -> high-pass -> bilinear transform. Normalise each section
        // to unity at Nyquist to avoid large intermediate section gains.
        const auto alpha = -t / 0.479558715647122;
        const auto realPole = (1.0 + alpha) / (1.0 - alpha);
        sections[0].set((1.0 + realPole) * 0.5, -(1.0 + realPole) * 0.5, 0.0, -realPole, 0.0);
        constexpr std::array<double, 3> poleReal{-0.3663801398758195, -0.18020621795445937, -0.04988473512821161};
        constexpr std::array<double, 3> poleImag{0.5836924799442206, 0.9087785050785181, 1.0284567154485633};
        constexpr std::array<double, 3> zeroImag{2.5574299560491345, 1.5521865675593132, 1.3295064031036625};
        for (std::size_t i = 0; i < 3; ++i)
        {
            const auto norm = poleReal[i] * poleReal[i] + poleImag[i] * poleImag[i];
            const auto ar = t * poleReal[i] / norm, ai = t * poleImag[i] / norm;
            const auto power = ar * ar + ai * ai, denominator = 1.0 - 2.0 * ar + power;
            const auto a1 = -2.0 * (1.0 - power) / denominator;
            const auto a2 = (1.0 + 2.0 * ar + power) / denominator;
            const auto z = t / zeroImag[i], zr = (1.0 - z * z) / (1.0 + z * z);
            const auto b0 = (1.0 - a1 + a2) / (2.0 + 2.0 * zr);
            sections[i + 1].set(b0, -2.0 * zr * b0, b0, a1, a2);
        }
        reset();
    }
    void reset() noexcept { for (auto& section : sections) section.reset(); }
    double next(double input) noexcept
    {
        auto value = std::isfinite(input) ? input : 0.0;
        for (auto& section : sections) value = section.next(value);
        return value;
    }
private:
    struct Section
    {
        void set(double b0In, double b1In, double b2In, double a1In, double a2In) noexcept
        { b0 = b0In; b1 = b1In; b2 = b2In; a1 = a1In; a2 = a2In; }
        void reset() noexcept { z1 = z2 = 0.0; }
        static double quiet(double x) noexcept { return !std::isfinite(x) || std::abs(x) < 1e-30 ? 0.0 : x; }
        double next(double x) noexcept
        {
            const auto y = quiet(b0 * x + z1);
            z1 = quiet(b1 * x - a1 * y + z2);
            z2 = quiet(b2 * x - a2 * y);
            // Terminate the whole tiny state together. Clipping individual
            // feedback values alone can sustain an inaudible rounding cycle.
            if (std::abs(y) + std::abs(z1) + std::abs(z2) < 1e-24)
            { reset(); return 0.0; }
            return y;
        }
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;
    };
    std::array<Section, 4> sections{};
};
}
