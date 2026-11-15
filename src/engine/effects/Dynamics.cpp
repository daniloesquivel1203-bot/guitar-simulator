#include "Dynamics.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

namespace
{
/** Coefficient for a one-pole smoother that covers most of a change in `ms`. */
float smoothingCoefficient (double ms, double sampleRate) noexcept
{
    return (float) std::exp (-1.0 / (juce::jmax (0.01, ms) * 0.001 * sampleRate));
}
}

//==============================================================================
void NoiseGate::prepare (double sampleRate, int)
{
    rate = sampleRate;
    detectorRelease = smoothingCoefficient (20.0, rate);
    openCoefficient = smoothingCoefficient (1.0, rate);
    reset();
}

void NoiseGate::reset()
{
    envelope = 0.0f;
    gain = 0.0f;
    open = false;
}

void NoiseGate::process (float* samples, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    const auto openLevel = juce::Decibels::decibelsToGain (threshold.get());
    const auto closeLevel = juce::Decibels::decibelsToGain (threshold.get() - hysteresisDb);
    const auto closeCoefficient = smoothingCoefficient (release.get(), rate);

    for (int i = 0; i < numSamples; ++i)
    {
        envelope = juce::jmax (std::abs (samples[i]), envelope * detectorRelease);

        if (envelope > openLevel)
            open = true;
        else if (envelope < closeLevel)
            open = false;

        // Open fast so the start of a note is not cut off, and close gently.
        const auto target = open ? 1.0f : 0.0f;
        const auto coefficient = target > gain ? openCoefficient : closeCoefficient;
        gain = target + (gain - target) * coefficient;

        samples[i] *= gain;
    }

    // Stop tiny values lingering forever once the gate has closed.
    if (gain < 1.0e-6f)
        gain = 0.0f;
}

//==============================================================================
void Compressor::prepare (double sampleRate, int)
{
    rate = sampleRate;
    makeupGain.reset (rate, 0.03);
    reset();
}

void Compressor::reset()
{
    reductionDb = 0.0f;
    makeupGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (makeup.get()));
}

void Compressor::process (float* samples, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    const auto thresholdDb = threshold.get();
    const auto slope = 1.0f - 1.0f / juce::jmax (1.0f, ratio.get());
    const auto attackCoefficient = smoothingCoefficient (attack.get(), rate);
    const auto releaseCoefficient = smoothingCoefficient (release.get(), rate);
    makeupGain.setTargetValue (juce::Decibels::decibelsToGain (makeup.get()));

    for (int i = 0; i < numSamples; ++i)
    {
        const auto levelDb = juce::Decibels::gainToDecibels (std::abs (samples[i]), -120.0f);
        const auto over = levelDb - thresholdDb;

        // A soft knee eases into compression around the threshold.
        float targetDb = 0.0f;

        if (over >= kneeDb * 0.5f)
            targetDb = over * slope;
        else if (over > -kneeDb * 0.5f)
            targetDb = slope * juce::square (over + kneeDb * 0.5f) / (2.0f * kneeDb);

        const auto coefficient = targetDb > reductionDb ? attackCoefficient : releaseCoefficient;
        reductionDb = targetDb + (reductionDb - targetDb) * coefficient;

        samples[i] *= juce::Decibels::decibelsToGain (-reductionDb) * makeupGain.getNextValue();
    }
}
