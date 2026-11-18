#include "MoreDynamics.h"

#include <cmath>

using namespace pedal_dsp;

//==============================================================================
void OptoCompressor::prepare (double sampleRate, int)
{
    rate = sampleRate;
    detectorAttack = timeCoefficient (rate, 2.0);
    detectorRelease = timeCoefficient (rate, 60.0);
    gainAttack = timeCoefficient (rate, 10.0);
    memoryCoefficient = timeCoefficient (rate, 2000.0);
    makeup.reset (rate, 0.03);
    wet.reset (rate, 0.03);
    reset();
}

void OptoCompressor::reset()
{
    envelope = reductionDb = memory = 0.0f;
    makeup.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gain.get()));
    wet.setCurrentAndTargetValue (mix.get() * 0.01f);
}

void OptoCompressor::process (float* samples, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    makeup.setTargetValue (juce::Decibels::decibelsToGain (gain.get()));
    wet.setTargetValue (mix.get() * 0.01f);

    // The knob lowers the threshold; the ratio stays a gentle 3:1.
    const auto thresholdDb = -8.0f - reduction.get() * 3.2f;
    constexpr float slope = 1.0f - 1.0f / 3.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto x = samples[i];
        const auto level = std::abs (x);
        envelope += (level > envelope ? detectorAttack : detectorRelease) * (level - envelope);

        const auto overDb = juce::Decibels::gainToDecibels (envelope, -120.0f) - thresholdDb;
        const auto targetDb = overDb > 0.0f ? overDb * slope : 0.0f;

        if (targetDb > reductionDb)
        {
            reductionDb += gainAttack * (targetDb - reductionDb);
        }
        else
        {
            // Like the light cell in an optical unit, recovery slows down the
            // more it has been squeezed.
            const auto releaseMs = 60.0f + 1500.0f * memory;
            reductionDb += timeCoefficient (rate, releaseMs) * (targetDb - reductionDb);
        }

        memory += memoryCoefficient * (juce::jmin (1.0f, reductionDb / 10.0f) - memory);

        const auto compressed = x * juce::Decibels::decibelsToGain (-reductionDb) * makeup.getNextValue();
        const auto amount = wet.getNextValue();
        samples[i] = x + amount * (compressed - x);
    }
}

//==============================================================================
void CleanBoost::prepare (double sampleRate, int)
{
    rate = sampleRate;
    level.reset (rate, 0.03);
    lastBass = lastTreble = -100.0f;
    update();
    reset();
}

void CleanBoost::reset()
{
    bassFilter.reset();
    trebleFilter.reset();
    level.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gain.get()));
}

void CleanBoost::update() noexcept
{
    if (! juce::exactlyEqual (bass.get(), lastBass))
    {
        lastBass = bass.get();
        bassFilter.coefficients = Coefficients::makeLowShelf (rate, 150.0f, 0.6f, juce::Decibels::decibelsToGain (lastBass));
    }

    if (! juce::exactlyEqual (treble.get(), lastTreble))
    {
        lastTreble = treble.get();
        trebleFilter.coefficients = Coefficients::makeHighShelf (rate, juce::jmin (3000.0f, (float) (rate * 0.4)), 0.6f,
                                                                 juce::Decibels::decibelsToGain (lastTreble));
    }
}

void CleanBoost::process (float* samples, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    update();
    level.setTargetValue (juce::Decibels::decibelsToGain (gain.get()));

    for (int i = 0; i < numSamples; ++i)
        samples[i] = trebleFilter.processSample (bassFilter.processSample (samples[i])) * level.getNextValue();
}

//==============================================================================
void AutoSwell::prepare (double sampleRate, int)
{
    rate = sampleRate;
    fastAttack = timeCoefficient (rate, 0.5);
    fastRelease = timeCoefficient (rate, 15.0);
    slowAttack = timeCoefficient (rate, 40.0);
    slowRelease = timeCoefficient (rate, 250.0);
    reset();
}

void AutoSwell::reset()
{
    fastEnvelope = slowEnvelope = 0.0f;
    ramp = 1.0f;
    holdOff = 0;
}

void AutoSwell::process (float* samples, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    const auto thresholdGain = juce::Decibels::decibelsToGain (threshold.get());
    const auto rampStep = 1.0f / (time.get() * 0.001f * (float) rate);
    const auto holdOffSamples = (int) (0.08 * rate);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto x = samples[i];
        const auto level = std::abs (x);
        fastEnvelope += (level > fastEnvelope ? fastAttack : fastRelease) * (level - fastEnvelope);
        slowEnvelope += (level > slowEnvelope ? slowAttack : slowRelease) * (level - slowEnvelope);

        if (holdOff > 0)
            --holdOff;

        // A new pick shows up as the fast level jumping well above the slow one.
        if (holdOff == 0 && fastEnvelope > thresholdGain && fastEnvelope > 2.0f * slowEnvelope + 1.0e-6f)
        {
            ramp = 0.0f;
            holdOff = holdOffSamples;
        }

        ramp = juce::jmin (1.0f, ramp + rampStep);

        // Squaring the ramp makes the fade-in sound even to the ear.
        samples[i] = x * ramp * ramp;
    }
}
