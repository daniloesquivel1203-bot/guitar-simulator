#include "Reverbs.h"

#include <cmath>

using namespace pedal_dsp;

namespace
{
constexpr double smoothingSeconds = 0.05;

// Line lengths in milliseconds. They share no common factors, so their
// echoes never line up into a pitched ring.
constexpr float baseLengthsMs[] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.7f, 73.1f };
constexpr float diffuserLengthsMs[] = { 4.7f, 3.6f, 12.7f, 9.3f };
constexpr float wobbleRates[] = { 0.31f, 0.37f, 0.41f, 0.47f, 0.53f, 0.59f, 0.67f, 0.71f };

/** Tone knob to the treble damping frequency. */
float toneToHz (float knob, double sampleRate) noexcept
{
    return juce::jmin ((float) (sampleRate * 0.45), 1500.0f * std::exp2 (knob * 0.45f));
}
}

//==============================================================================
void FeedbackDelayNetwork::prepare (double newSampleRate, float maxSizeScale)
{
    sampleRate = newSampleRate;

    for (size_t i = 0; i < (size_t) numLines; ++i)
    {
        lines[i].setMaxSamples ((int) std::ceil ((baseLengthsMs[i] * maxSizeScale + 6.0f) * 0.001 * sampleRate) + 64);   // room for the wobble
        wobble[i].reset ((double) i / numLines);
    }

    for (size_t i = 0; i < diffusers.size(); ++i)
        diffusers[i].setSize ((int) (diffuserLengthsMs[i] * 0.001 * sampleRate));

    settings.sizeScale = juce::jmin (settings.sizeScale, maxSizeScale);
    setSettings (settings);
    reset();
}

void FeedbackDelayNetwork::reset()
{
    for (auto& line : lines)
        line.clear();

    for (auto& diffuser : diffusers)
        diffuser.clear();

    damping.fill (0.0f);
}

void FeedbackDelayNetwork::setSettings (const Settings& newSettings) noexcept
{
    settings = newSettings;
    dampingCoefficient = onePoleCoefficient (sampleRate, settings.dampingHz);

    for (size_t i = 0; i < (size_t) numLines; ++i)
    {
        lengths[i] = baseLengthsMs[i] * settings.sizeScale * 0.001f * (float) sampleRate;

        // Each pass round a line loses the share of 60 dB that its length is
        // of the decay time.
        gains[i] = std::pow (10.0f, -3.0f * lengths[i] / ((float) sampleRate * juce::jmax (0.05f, settings.decaySeconds)));
    }
}

void FeedbackDelayNetwork::process (float input, float& left, float& right) noexcept
{
    for (auto& diffuser : diffusers)
        input = diffuser.process (input, settings.diffusion);

    std::array<float, numLines> outs {};

    for (size_t i = 0; i < (size_t) numLines; ++i)
    {
        const auto mod = settings.modulationSamples * Lfo::sine (wobble[i].advance (wobbleRates[i], sampleRate));
        const auto tap = lines[i].read (lengths[i] + mod + 2.0f);
        damping[i] += dampingCoefficient * (tap - damping[i]);
        outs[i] = damping[i] * gains[i];
    }

    left = 0.35f * (outs[0] - outs[2] + outs[4] - outs[6] + outs[1] - outs[5]);
    right = 0.35f * (outs[1] - outs[3] + outs[5] - outs[7] + outs[2] - outs[6]);

    // An 8x8 Hadamard matrix spreads every line into every other line while
    // keeping the total energy the same.
    for (int span = 1; span < numLines; span *= 2)
        for (int start = 0; start < numLines; start += 2 * span)
            for (int k = start; k < start + span; ++k)
            {
                const auto a = outs[(size_t) k], b = outs[(size_t) (k + span)];
                outs[(size_t) k] = a + b;
                outs[(size_t) (k + span)] = a - b;
            }

    const auto scale = 1.0f / std::sqrt ((float) numLines);

    for (size_t i = 0; i < (size_t) numLines; ++i)
        lines[i].write (outs[i] * scale + input * 0.5f);
}

//==============================================================================
void HallReverb::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    network.prepare (sampleRate, 1.6f);
    preDelayLine.setMaxSamples ((int) std::ceil (preDelay.maxValue * 0.001 * sampleRate) + 8);
    wet.reset (sampleRate, smoothingSeconds);
    preDelaySamples.reset (sampleRate, 0.1);
    reset();
}

void HallReverb::reset()
{
    network.reset();
    preDelayLine.clear();
    wet.setCurrentAndTargetValue (mix.get() * 0.01f);
    preDelaySamples.setCurrentAndTargetValue ((float) (preDelay.get() * 0.001 * sampleRate));
}

void HallReverb::process (float* left, float* right, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    FeedbackDelayNetwork::Settings settings;
    settings.sizeScale = 1.6f;
    settings.decaySeconds = decay.get();
    settings.dampingHz = toneToHz (tone.get(), sampleRate);
    settings.modulationSamples = modulation.get() * 0.0004f * (float) sampleRate;   // up to 4 ms
    settings.diffusion = 0.7f;
    network.setSettings (settings);

    wet.setTargetValue (mix.get() * 0.01f);
    preDelaySamples.setTargetValue ((float) (preDelay.get() * 0.001 * sampleRate));

    for (int i = 0; i < numSamples; ++i)
    {
        preDelayLine.write (0.5f * (left[i] + right[i]));
        const auto input = preDelayLine.read (1.0f + preDelaySamples.getNextValue());

        float l = 0.0f, r = 0.0f;
        network.process (input, l, r);

        const auto amount = wet.getNextValue();
        left[i]  = left[i]  * (1.0f - 0.5f * amount) + l * amount;
        right[i] = right[i] * (1.0f - 0.5f * amount) + r * amount;
    }
}

//==============================================================================
void PlateReverb::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    network.prepare (sampleRate, 0.6f);
    preDelayLine.setMaxSamples ((int) std::ceil (preDelay.maxValue * 0.001 * sampleRate) + 8);
    lowCutCoefficient = onePoleCoefficient (sampleRate, 180.0);
    wet.reset (sampleRate, smoothingSeconds);
    preDelaySamples.reset (sampleRate, 0.1);
    reset();
}

void PlateReverb::reset()
{
    network.reset();
    preDelayLine.clear();
    lowCut.reset();
    wet.setCurrentAndTargetValue (mix.get() * 0.01f);
    preDelaySamples.setCurrentAndTargetValue ((float) (preDelay.get() * 0.001 * sampleRate));
}

void PlateReverb::process (float* left, float* right, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    // Short, closely spaced lines and strong diffusion give the plate its
    // instant density.
    FeedbackDelayNetwork::Settings settings;
    settings.sizeScale = 0.6f;
    settings.decaySeconds = decay.get();
    settings.dampingHz = toneToHz (tone.get(), sampleRate);
    settings.modulationSamples = 0.0003f * (float) sampleRate;
    settings.diffusion = 0.78f;
    network.setSettings (settings);

    wet.setTargetValue (mix.get() * 0.01f);
    preDelaySamples.setTargetValue ((float) (preDelay.get() * 0.001 * sampleRate));

    for (int i = 0; i < numSamples; ++i)
    {
        preDelayLine.write (0.5f * (left[i] + right[i]));
        const auto input = lowCut.highPass (preDelayLine.read (1.0f + preDelaySamples.getNextValue()), lowCutCoefficient);

        float l = 0.0f, r = 0.0f;
        network.process (input, l, r);

        const auto amount = wet.getNextValue();
        left[i]  = left[i]  * (1.0f - 0.5f * amount) + l * amount;
        right[i] = right[i] * (1.0f - 0.5f * amount) + r * amount;
    }
}

//==============================================================================
void ShimmerReverb::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    network.prepare (sampleRate, 1.4f);
    octaveUp.prepare (sampleRate, 50.0f);
    lowCutCoefficient = onePoleCoefficient (sampleRate, 300.0);
    wet.reset (sampleRate, smoothingSeconds);
    shimmerGain.reset (sampleRate, smoothingSeconds);
    reset();
}

void ShimmerReverb::reset()
{
    network.reset();
    octaveUp.reset();
    shimmerLowCut.reset();
    lastWet = 0.0f;
    wet.setCurrentAndTargetValue (mix.get() * 0.01f);
    shimmerGain.setCurrentAndTargetValue (shimmer.get() * 0.01f);
}

void ShimmerReverb::process (float* left, float* right, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    FeedbackDelayNetwork::Settings settings;
    settings.sizeScale = 1.4f;
    settings.decaySeconds = decay.get();
    settings.dampingHz = toneToHz (tone.get(), sampleRate);
    settings.modulationSamples = 0.002f * (float) sampleRate;
    settings.diffusion = 0.72f;
    network.setSettings (settings);

    wet.setTargetValue (mix.get() * 0.01f);
    shimmerGain.setTargetValue (shimmer.get() * 0.01f);

    for (int i = 0; i < numSamples; ++i)
    {
        // Part of the tail is shifted up an octave and fed back in, so each
        // pass climbs higher. The soft clip keeps the loop from running away.
        const auto raised = octaveUp.process (shimmerLowCut.highPass (lastWet, lowCutCoefficient), 2.0f);
        const auto input = 0.5f * (left[i] + right[i]) + std::tanh (0.6f * shimmerGain.getNextValue() * raised);

        float l = 0.0f, r = 0.0f;
        network.process (input, l, r);
        lastWet = 0.5f * (l + r);

        const auto amount = wet.getNextValue();
        left[i]  = left[i]  * (1.0f - 0.5f * amount) + l * amount;
        right[i] = right[i] * (1.0f - 0.5f * amount) + r * amount;
    }
}

//==============================================================================
void SpringReverb::Spring::reset()
{
    line.clear();

    for (auto& stage : chirp)
        stage.reset();

    loopDamping.reset();
    outLow.reset();
    outHigh.reset();
    last = 0.0f;
}

void SpringReverb::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;

    // Two springs of slightly different lengths, one per ear.
    const float lengthsMs[] = { 37.0f, 43.0f };

    for (size_t s = 0; s < springs.size(); ++s)
    {
        springs[s].lengthSamples = lengthsMs[s] * 0.001f * (float) sampleRate;
        springs[s].line.setMaxSamples ((int) springs[s].lengthSamples + 8);
    }

    loopCoefficient = onePoleCoefficient (sampleRate, 4000.0);
    outHighCoefficient = onePoleCoefficient (sampleRate, 250.0);
    wet.reset (sampleRate, smoothingSeconds);
    reset();
}

void SpringReverb::reset()
{
    for (auto& spring : springs)
        spring.reset();

    wet.setCurrentAndTargetValue (mix.get() * 0.01f);
}

void SpringReverb::process (float* left, float* right, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    wet.setTargetValue (mix.get() * 0.01f);

    const auto dwellGain = juce::Decibels::decibelsToGain (dwell.get() * 2.4f - 6.0f);
    const auto feedback = 0.55f + decay.get() * 0.04f;
    const auto outLowCoefficient = onePoleCoefficient (sampleRate, 2000.0 * std::exp2 (tone.get() * 0.2));

    // A strong all-pass coefficient makes high frequencies arrive later than
    // low ones: the springy "chirp".
    constexpr float chirpCoefficient = -0.65f;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto input = std::tanh (0.5f * (left[i] + right[i]) * dwellGain);
        float outs[2];

        for (size_t s = 0; s < springs.size(); ++s)
        {
            auto& spring = springs[s];
            auto x = input + feedback * spring.loopDamping.lowPass (spring.last, loopCoefficient);

            for (auto& stage : spring.chirp)
                x = stage.process (x, chirpCoefficient);

            spring.line.write (x);
            spring.last = spring.line.read (spring.lengthSamples);

            const auto shaped = spring.outHigh.highPass (spring.outLow.lowPass (spring.last, outLowCoefficient), outHighCoefficient);
            outs[s] = 0.8f * shaped;
        }

        const auto amount = wet.getNextValue();
        left[i]  = left[i]  * (1.0f - 0.5f * amount) + outs[0] * amount;
        right[i] = right[i] * (1.0f - 0.5f * amount) + outs[1] * amount;
    }
}
