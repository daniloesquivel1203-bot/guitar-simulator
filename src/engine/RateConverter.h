#pragma once

#include <juce_core/juce_core.h>

#include <vector>

/** Streams audio from one sample rate to another, a block at a time.

    Used when an amp capture was recorded at a different rate from the one
    the audio device runs at. Feed it input with push() and take output with
    pull(). It uses 4-point cubic interpolation, which is cheap and adds only
    a couple of samples of delay.

    No memory is allocated after prepare(), so it is safe on the audio thread.
*/
class RateConverter
{
public:
    /** `maxInputBlock` is the most samples one push() will be given. */
    void prepare (double inputRate, double outputRate, int maxInputBlock, int primingSamples = 0)
    {
        step = inputRate / outputRate;

        const auto maxOutputBlock = (int) std::ceil (maxInputBlock / step) + 4;
        capacity = (size_t) (4 * (maxInputBlock + maxOutputBlock) + 64 + primingSamples);
        history.assign (capacity, 0.0f);
        priming = primingSamples;
        reset();
    }

    void reset() noexcept
    {
        std::fill (history.begin(), history.end(), 0.0f);

        // One zero before the first real sample gives the interpolator its
        // left neighbour. Priming zeros hold output back so pull() never runs dry.
        count = (size_t) (1 + priming);
        position = 1.0;
    }

    /** The most output samples that one push of `inputSamples` can produce. */
    int getMaxOutputFor (int inputSamples) const noexcept   { return (int) std::ceil (inputSamples / step) + 4; }

    void push (const float* input, int numSamples) noexcept
    {
        // Never happens in normal use. If it did, drop the oldest audio
        // rather than write past the buffer.
        if (count + (size_t) numSamples > capacity)
            discard (count + (size_t) numSamples - capacity);

        std::copy (input, input + numSamples, history.begin() + (std::ptrdiff_t) count);
        count += (size_t) numSamples;
    }

    /** Produces up to `maxSamples` and returns how many it produced. */
    int pull (float* output, int maxSamples) noexcept
    {
        int produced = 0;

        // Each output needs two input samples to the right of its position.
        while (produced < maxSamples && position + 2.0 < (double) count)
        {
            const auto index = (size_t) position;
            const auto t = (float) (position - (double) index);

            const auto y0 = history[index - 1], y1 = history[index], y2 = history[index + 1], y3 = history[index + 2];

            // Catmull-Rom cubic through the four neighbours.
            output[produced++] = y1 + 0.5f * t * (y2 - y0
                                     + t * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3
                                     + t * (3.0f * (y1 - y2) + y3 - y0)));
            position += step;
        }

        // Keep one sample to the left of the current position.
        const auto used = (size_t) position;

        if (used > 1)
            discard (used - 1);

        return produced;
    }

private:
    void discard (size_t numSamples) noexcept
    {
        numSamples = juce::jmin (numSamples, count);
        std::copy (history.begin() + (std::ptrdiff_t) numSamples, history.begin() + (std::ptrdiff_t) count, history.begin());
        count -= numSamples;
        position = juce::jmax (1.0, position - (double) numSamples);
    }

    std::vector<float> history;
    size_t capacity = 0, count = 0;
    double step = 1.0, position = 1.0;
    int priming = 0;
};
