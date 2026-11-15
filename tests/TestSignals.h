#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

/** Helpers for generating and measuring test signals. */
namespace test
{
inline std::vector<float> sine (double hz, float amplitude, double sampleRate, int numSamples)
{
    std::vector<float> out ((size_t) numSamples);

    for (int i = 0; i < numSamples; ++i)
        out[(size_t) i] = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / sampleRate);

    return out;
}

inline float peak (const std::vector<float>& signal, size_t from = 0)
{
    float result = 0.0f;

    for (size_t i = from; i < signal.size(); ++i)
        result = juce::jmax (result, std::abs (signal[i]));

    return result;
}

inline double rms (const std::vector<float>& signal, size_t from = 0)
{
    double sum = 0.0;

    for (size_t i = from; i < signal.size(); ++i)
        sum += (double) signal[i] * signal[i];

    return std::sqrt (sum / (double) (signal.size() - from));
}

inline bool allFinite (const std::vector<float>& signal)
{
    for (auto sample : signal)
        if (! std::isfinite (sample))
            return false;

    return true;
}

/** Amplitude of one frequency component, measured over signal[from..]. */
inline double amplitudeAt (const std::vector<float>& signal, double hz, double sampleRate, size_t from)
{
    double re = 0.0, im = 0.0;

    for (size_t i = from; i < signal.size(); ++i)
    {
        const auto phase = juce::MathConstants<double>::twoPi * hz * (double) i / sampleRate;
        re += signal[i] * std::cos (phase);
        im += signal[i] * std::sin (phase);
    }

    return 2.0 * std::sqrt (re * re + im * im) / (double) (signal.size() - from);
}

/** Share of the signal's energy that is not at the fundamental, from 0 to 1.
    Use a whole number of cycles in signal[from..] for an accurate result. */
inline double distortion (const std::vector<float>& signal, double fundamentalHz, double sampleRate, size_t from)
{
    const auto fundamental = amplitudeAt (signal, fundamentalHz, sampleRate, from) / std::sqrt (2.0);
    const auto total = rms (signal, from);
    return total <= 0.0 ? 0.0 : std::sqrt (juce::jmax (0.0, total * total - fundamental * fundamental)) / total;
}

/** Runs a mono in-place processor over a whole signal in fixed-size blocks. */
template <typename Processor>
std::vector<float> render (Processor& processor, std::vector<float> signal, int blockSize)
{
    for (size_t offset = 0; offset < signal.size(); offset += (size_t) blockSize)
        processor.process (signal.data() + offset, (int) juce::jmin ((size_t) blockSize, signal.size() - offset));

    return signal;
}
}
