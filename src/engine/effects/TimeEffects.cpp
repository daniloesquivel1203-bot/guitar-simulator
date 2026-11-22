#include "TimeEffects.h"

#include <cmath>

namespace
{
constexpr double smoothingSeconds = 0.05;
}

//==============================================================================
void Chorus::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;

    const auto maxSamples = (int) std::ceil ((centreMs + maxDepthMs + 1.0f) * 0.001 * sampleRate);
    delayLeft.setMaxSamples (maxSamples);
    delayRight.setMaxSamples (maxSamples);

    wet.reset (sampleRate, smoothingSeconds);
    depthMs.reset (sampleRate, smoothingSeconds);
    reset();
}

void Chorus::reset()
{
    delayLeft.clear();
    delayRight.clear();
    phase = 0.0;
    wet.setCurrentAndTargetValue (mix.get() * 0.01f);
    depthMs.setCurrentAndTargetValue (depth.get() * 0.1f * maxDepthMs);
}

void Chorus::process (float* left, float* right, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    wet.setTargetValue (mix.get() * 0.01f);
    depthMs.setTargetValue (depth.get() * 0.1f * maxDepthMs);

    const auto phaseStep = juce::MathConstants<double>::twoPi * rate.get() / sampleRate;
    const auto msToSamples = (float) (0.001 * sampleRate);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto swing = depthMs.getNextValue();

        // The right side runs a quarter cycle behind the left for width.
        const auto delayL = (centreMs + swing * (float) std::sin (phase)) * msToSamples;
        const auto delayR = (centreMs + swing * (float) std::cos (phase)) * msToSamples;

        delayLeft.write (left[i]);
        delayRight.write (right[i]);

        const auto wetAmount = 0.5f * wet.getNextValue();
        left[i]  = left[i]  * (1.0f - wetAmount) + delayLeft.read (delayL + 1.0f) * wetAmount;
        right[i] = right[i] * (1.0f - wetAmount) + delayRight.read (delayR + 1.0f) * wetAmount;

        phase += phaseStep;

        if (phase >= juce::MathConstants<double>::twoPi)
            phase -= juce::MathConstants<double>::twoPi;
    }
}

//==============================================================================
void Delay::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;

    const auto maxSamples = (int) std::ceil (time.maxValue * 0.001 * sampleRate) + 8;
    lineA.setMaxSamples (maxSamples);
    lineB.setMaxSamples (maxSamples);

    // Each echo loses a little treble, like a tape or analogue delay.
    dampCoefficient = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 4500.0 / sampleRate));

    delaySamples.reset (sampleRate, 0.25);
    wet.reset (sampleRate, smoothingSeconds);
    feedbackGain.reset (sampleRate, smoothingSeconds);
    reset();
}

void Delay::reset()
{
    lineA.clear();
    lineB.clear();
    dampA = dampB = 0.0f;
    delaySamples.setCurrentAndTargetValue ((float) (tempo_sync::timeMs (time, sync, getTempo()) * 0.001 * sampleRate));
    wet.setCurrentAndTargetValue (mix.get() * 0.01f);
    feedbackGain.setCurrentAndTargetValue (feedback.get() * 0.01f);
}

void Delay::process (float* left, float* right, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    delaySamples.setTargetValue ((float) (tempo_sync::timeMs (time, sync, getTempo()) * 0.001 * sampleRate));
    wet.setTargetValue (mix.get() * 0.01f);
    feedbackGain.setTargetValue (feedback.get() * 0.01f);
    const bool bounce = pingPong.isOn();

    for (int i = 0; i < numSamples; ++i)
    {
        const auto delay = delaySamples.getNextValue();
        const auto amount = feedbackGain.getNextValue();
        const auto input = 0.5f * (left[i] + right[i]);

        const auto echoA = lineA.read (delay);
        const auto echoB = lineB.read (delay);

        dampA += dampCoefficient * (echoA - dampA);
        dampB += dampCoefficient * (echoB - dampB);

        if (bounce)
        {
            // A feeds B and B feeds A, so echoes alternate sides.
            lineA.write (input + dampB * amount);
            lineB.write (dampA);
        }
        else
        {
            lineA.write (input + dampA * amount);
            lineB.write (0.0f);
        }

        const auto wetAmount = wet.getNextValue();
        left[i]  += wetAmount * echoA;
        right[i] += wetAmount * (bounce ? echoB : echoA);
    }
}

//==============================================================================
void Reverb::prepare (double sampleRate, int)
{
    // Setting the sample rate after the parameters makes the reverb start at
    // its settings instead of gliding to them from wherever it was before.
    lastSize = lastDamping = lastMix = -1.0f;
    update();
    reverb.setSampleRate (sampleRate);
    reset();
}

void Reverb::reset()
{
    reverb.reset();
}

void Reverb::update()
{
    const auto newSize = size.get(), newDamping = damping.get(), newMix = mix.get();

    if (juce::exactlyEqual (newSize, lastSize) && juce::exactlyEqual (newDamping, lastDamping)
        && juce::exactlyEqual (newMix, lastMix))
        return;

    lastSize = newSize;
    lastDamping = newDamping;
    lastMix = newMix;

    juce::Reverb::Parameters parameters;
    parameters.roomSize = 0.3f + 0.068f * newSize;   // above about 0.98 the tail never dies
    parameters.damping = 0.1f * newDamping;
    parameters.wetLevel = 0.5f * newMix * 0.01f;
    parameters.dryLevel = 0.5f;                      // the reverb doubles this internally, giving unity
    parameters.width = 1.0f;
    parameters.freezeMode = 0.0f;
    reverb.setParameters (parameters);
}

void Reverb::process (float* left, float* right, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    update();
    reverb.processStereo (left, right, numSamples);
}
