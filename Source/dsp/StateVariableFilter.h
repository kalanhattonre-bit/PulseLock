#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>

namespace pulselock
{
    /** Stereo trapezoidal state-variable filter (topology-preserving), stable under per-sample cutoff changes. */
    class StateVariableFilter
    {
    public:
        enum class Mode { lowPass = 0, highPass, bandPass };

        void reset() noexcept
        {
            for (auto& s : state)
                s = { 0.0f, 0.0f };
        }

        /** cutoffHz is clamped to 10 Hz .. 0.45 * sampleRate; q > 0. */
        void setCoefficients (double sampleRate, float cutoffHz, float q) noexcept
        {
            const double fc = juce::jlimit (10.0, 0.45 * sampleRate, (double) cutoffHz);
            const double g = std::tan (juce::MathConstants<double>::pi * fc / sampleRate);
            k = (float) (1.0 / juce::jmax (0.1, (double) q));
            a1 = (float) (1.0 / (1.0 + g * (g + (double) k)));
            a2 = (float) g * a1;
            a3 = (float) g * a2;
        }

        float process (int channel, float x, Mode mode) noexcept
        {
            auto& s = state[(size_t) (channel & 1)];
            const float v3 = x - s.ic2;
            const float v1 = a1 * s.ic1 + a2 * v3;
            const float v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
            s.ic1 = 2.0f * v1 - s.ic1;
            s.ic2 = 2.0f * v2 - s.ic2;

            if (! std::isfinite (s.ic1) || ! std::isfinite (s.ic2))
            {
                s = { 0.0f, 0.0f };
                return 0.0f;
            }

            switch (mode)
            {
                case Mode::lowPass:  return v2;
                case Mode::highPass: return x - k * v1 - v2;
                case Mode::bandPass: return v1;
            }
            return v2;
        }

    private:
        struct Channel { float ic1 = 0.0f, ic2 = 0.0f; };
        std::array<Channel, 2> state {};
        float k = 1.41421356f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    };
}
