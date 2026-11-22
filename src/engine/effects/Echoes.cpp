#include "Echoes.h"

#include <cmath>

using namespace pedal_dsp;

namespace
{
constexpr double smoothingSeconds = 0.05;
}

//==============================================================================
void TapeEcho::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    line.setMaxSamples ((int) std::ceil (time.maxValue * 1.02 * 0.001 * sampleRate) + 8);
    highPassCoefficient = onePoleCoefficient (sampleRate, 120.0);

    // Changing the time slides the pitch of the echoes, like moving a tape head.
    delaySamples.reset (sampleRate, 0.4);
    wet.reset (sampleRate, smoothingSeconds);
    feedbackGain.reset (sampleRate, smoothingSeconds);
    reset();
}

void TapeEcho::reset()
{
    line.clear();
    lowPassA.reset();
    lowPassB.reset();
    highPass.reset();
    wowLfo.reset();
    flutterLfo.reset (0.3);
    delaySamples.setCurrentAndTargetValue ((float) (tempo_sync::timeMs (time, sync, getTempo()) * 0.001 * sampleRate));
    wet.setCurrentAndTargetValue (mix.get() * 0.01f);
    feedbackGain.setCurrentAndTargetValue (repeats.get() * 0.01f);
}

void TapeEcho::process (float* left, float* right, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    delaySamples.setTargetValue ((float) (tempo_sync::timeMs (time, sync, getTempo()) * 0.001 * sampleRate));
    wet.setTargetValue (mix.get() * 0.01f);
    feedbackGain.setTargetValue (repeats.get() * 0.01f);

    // Older tape loses more treble on every pass.
    const auto lowPassCoefficient = onePoleCoefficient (sampleRate, 6000.0 * std::exp2 (-age.get() * 0.2));
    const auto wowAmount = wow.get() * 0.1f;

    for (int i = 0; i < numSamples; ++i)
    {
        // A slow drift and a fast flutter in the tape speed.
        const auto wobble = 1.0f + wowAmount * (0.004f * Lfo::sine (wowLfo.advance (0.6, sampleRate))
                                                + 0.0008f * Lfo::sine (flutterLfo.advance (6.5, sampleRate)));
        const auto echo = line.read (juce::jmax (1.0f, delaySamples.getNextValue() * wobble));

        auto filtered = lowPassB.lowPass (lowPassA.lowPass (echo, lowPassCoefficient), lowPassCoefficient);
        filtered = highPass.highPass (filtered, highPassCoefficient);

        const auto input = 0.5f * (left[i] + right[i]);

        // The record head saturates, which also stops runaway feedback growing forever.
        line.write (std::tanh (input + feedbackGain.getNextValue() * filtered * 1.05f));

        const auto amount = wet.getNextValue();
        left[i]  += amount * echo;
        right[i] += amount * echo;
    }
}

//==============================================================================
void AnalogDelay::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    line.setMaxSamples ((int) std::ceil ((time.maxValue + 2.0) * 0.001 * sampleRate) + 8);
    delaySamples.reset (sampleRate, 0.3);
    wet.reset (sampleRate, smoothingSeconds);
    feedbackGain.reset (sampleRate, smoothingSeconds);
    reset();
}

void AnalogDelay::reset()
{
    line.clear();
    lowPassA.reset();
    lowPassB.reset();
    lfo.reset();
    delaySamples.setCurrentAndTargetValue ((float) (tempo_sync::timeMs (time, sync, getTempo()) * 0.001 * sampleRate));
    wet.setCurrentAndTargetValue (mix.get() * 0.01f);
    feedbackGain.setCurrentAndTargetValue (feedback.get() * 0.01f);
}

void AnalogDelay::process (float* left, float* right, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    delaySamples.setTargetValue ((float) (tempo_sync::timeMs (time, sync, getTempo()) * 0.001 * sampleRate));
    wet.setTargetValue (mix.get() * 0.01f);
    feedbackGain.setTargetValue (feedback.get() * 0.01f);

    // A bucket-brigade chip clocks slower for longer delays, so it gets darker.
    const auto cutoff = 5000.0 * std::pow (20.0 / tempo_sync::timeMs (time, sync, getTempo()), 0.3);
    const auto coefficient = onePoleCoefficient (sampleRate, cutoff);
    const auto swing = modulation.get() * 0.04f * (float) (0.001 * sampleRate);   // up to 0.4 ms

    for (int i = 0; i < numSamples; ++i)
    {
        const auto d = delaySamples.getNextValue() + swing * (1.0f + Lfo::sine (lfo.advance (0.7, sampleRate)));
        const auto echo = lowPassB.lowPass (lowPassA.lowPass (line.read (juce::jmax (1.0f, d)), coefficient), coefficient);

        const auto input = 0.5f * (left[i] + right[i]);
        line.write (std::tanh (input + feedbackGain.getNextValue() * echo));

        const auto amount = wet.getNextValue();
        left[i]  += amount * echo;
        right[i] += amount * echo;
    }
}

//==============================================================================
int ReverseDelay::currentSliceLength() const noexcept
{
    return juce::jmax (2, (int) (tempo_sync::timeMs (time, sync, getTempo()) * 0.001 * sampleRate));
}

void ReverseDelay::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    buffer.assign ((size_t) std::ceil (2.0 * time.maxValue * 0.001 * sampleRate) + 16, 0.0f);
    wet.reset (sampleRate, smoothingSeconds);
    feedbackGain.reset (sampleRate, smoothingSeconds);
    reset();
}

void ReverseDelay::reset()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writeIndex = 0;
    position = 0;
    lastOut = 0.0f;
    sliceLength = currentSliceLength();
    wet.setCurrentAndTargetValue (mix.get() * 0.01f);
    feedbackGain.setCurrentAndTargetValue (feedback.get() * 0.01f);
}

void ReverseDelay::process (float* left, float* right, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    wet.setTargetValue (mix.get() * 0.01f);
    feedbackGain.setTargetValue (feedback.get() * 0.01f);

    const auto size = (int) buffer.size();

    const auto readHead = [this, size] (int offset)
    {
        // Each head's delay grows by two samples per sample, so it walks
        // backwards through what was recorded.
        const auto delay = 2 * offset + 1;
        auto index = writeIndex - delay;

        while (index < 0)
            index += size;

        const auto fade = std::sin (pi * (float) offset / (float) sliceLength);
        return buffer[(size_t) index] * fade * fade;
    };

    for (int i = 0; i < numSamples; ++i)
    {
        // Two heads half a slice apart cross-fade, so the jumps are never heard.
        const auto other = (position + sliceLength / 2) % sliceLength;
        const auto out = readHead (position) + readHead (other);

        const auto input = 0.5f * (left[i] + right[i]);
        buffer[(size_t) writeIndex] = input + feedbackGain.getNextValue() * lastOut;
        writeIndex = (writeIndex + 1) % size;
        lastOut = out;

        if (++position >= sliceLength)
        {
            position = 0;

            // Only change the slice length between slices.
            sliceLength = currentSliceLength();
        }

        const auto amount = wet.getNextValue();
        left[i]  = left[i]  * (1.0f - 0.5f * amount) + out * amount;
        right[i] = right[i] * (1.0f - 0.5f * amount) + out * amount;
    }
}
