#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

/** A brickwall peak limiter that protects headphones and ears.

    Signals below the ceiling pass through untouched. Anything louder is turned
    down instantly and released smoothly, so the output never exceeds the
    ceiling no matter what the rest of the chain does.
*/
class SafetyLimiter
{
public:
    /** Just under full scale, about -1 dB. */
    static constexpr float ceiling = 0.89f;

    void prepare (double sampleRate) noexcept
    {
        releaseCoefficient = (float) std::exp (-1.0 / (releaseSeconds * sampleRate));
        reset();
    }

    void reset() noexcept   { envelope = 0.0f; }

    float processSample (float sample) noexcept
    {
        // Treat a broken value from upstream as silence instead of passing it on.
        if (! std::isfinite (sample))
            sample = 0.0f;

        envelope = juce::jmax (std::abs (sample), envelope * releaseCoefficient);
        return envelope > ceiling ? sample * (ceiling / envelope) : sample;
    }

    /** Limits a stereo pair with one shared gain, so the image does not shift. */
    void processStereo (float& left, float& right) noexcept
    {
        if (! std::isfinite (left))   left = 0.0f;
        if (! std::isfinite (right))  right = 0.0f;

        envelope = juce::jmax (std::abs (left), std::abs (right), envelope * releaseCoefficient);

        if (envelope > ceiling)
        {
            const auto gain = ceiling / envelope;
            left *= gain;
            right *= gain;
        }
    }

private:
    static constexpr double releaseSeconds = 0.08;

    float releaseCoefficient = 0.999f;
    float envelope = 0.0f;
};
