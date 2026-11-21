#include "Drives.h"

#include <cmath>

namespace
{
constexpr double smoothingSeconds = 0.03;

/** Level knob: 5 is unity before the pedal's output trim, 10 is +15 dB, 0 is silent. */
float levelKnobToGain (float knob) noexcept
{
    return knob <= 0.0f ? 0.0f : juce::Decibels::decibelsToGain ((knob - 5.0f) * 3.0f);
}

/** Tone knob to a low-pass cutoff, spread evenly by ratio. */
float toneKnobToHz (float knob, float lowHz, float highHz) noexcept
{
    return lowHz * std::pow (highHz / lowHz, knob / 10.0f);
}

/** A clipper that is linear for small values and flat beyond +/-1.5. */
inline float hardClip (float x) noexcept
{
    x = juce::jlimit (-1.5f, 1.5f, x);
    return x - (4.0f / 27.0f) * x * x * x;
}
}

//==============================================================================
void DrivePedal::prepare (double sampleRate, int maxBlockSize)
{
    rate = sampleRate;
    oversampling.initProcessing ((size_t) juce::jmax (1, maxBlockSize));
    level.reset (rate, smoothingSeconds);
    loading.reset (rate, 0.05);
    lastToneHz = -1.0f;
    trebleCoefficient = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 1800.0 / rate));
    bassCoefficient = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 160.0 / rate));
    prepareShaper (rate * 4.0);
    reset();
}

void DrivePedal::reset()
{
    oversampling.reset();
    toneFilter.reset();
    level.setCurrentAndTargetValue (levelKnobToGain (getLevelKnob()) * juce::Decibels::decibelsToGain (getOutputTrimDb()));
    loading.setCurrentAndTargetValue (isFedByGuitar() ? 1.0f : 0.0f);
    trebleState = bassState = 0.0f;
    resetShaper();
}

void DrivePedal::applyPickupLoading (float* samples, int numSamples) noexcept
{
    loading.setTargetValue (isFedByGuitar() ? 1.0f : 0.0f);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto x = samples[i];
        trebleState += trebleCoefficient * (x - trebleState);
        bassState += bassCoefficient * (x - bassState);

        const auto lows = trebleState;      // everything below about 1.8 kHz
        const auto highs = x - trebleState;
        const auto deepBass = bassState;

        // Loaded pickups: the treble is rounded off and some level is lost, so
        // the fuzz is darker, rounder and cleans up more when you pick softly.
        const auto loaded = 0.8f * lows + 0.2f * highs;

        // Behind a buffer: full treble with a slight lift and a little less
        // deep bass, which is why a fuzz there sounds thin and spitty.
        const auto buffered = x + 0.4f * highs - 0.25f * deepBass;

        const auto amount = loading.getNextValue();
        samples[i] = buffered + amount * (loaded - buffered);
    }
}

void DrivePedal::process (float* samples, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    const auto toneHz = juce::jmin (getToneHz(), (float) (rate * 0.45));
    const bool useTone = toneHz > 0.0f;

    if (useTone && ! juce::exactlyEqual (toneHz, lastToneHz))
    {
        lastToneHz = toneHz;
        toneFilter.coefficients = Coefficients::makeLowPass (rate, toneHz);
    }

    level.setTargetValue (levelKnobToGain (getLevelKnob()) * juce::Decibels::decibelsToGain (getOutputTrimDb()));

    if (loadsThePickups())
        applyPickupLoading (samples, numSamples);

    float* channel[] = { samples };
    juce::dsp::AudioBlock<float> block (channel, 1, (size_t) numSamples);
    auto oversampled = oversampling.processSamplesUp (block);
    shape (oversampled.getChannelPointer (0), (int) oversampled.getNumSamples());
    oversampling.processSamplesDown (block);
    postShape (samples, numSamples);

    if (useTone)
        for (int i = 0; i < numSamples; ++i)
            samples[i] = toneFilter.processSample (samples[i]);

    for (int i = 0; i < numSamples; ++i)
        samples[i] *= level.getNextValue();
}

//==============================================================================
void Overdrive::prepareShaper (double oversampledRate)
{
    highPass.coefficients = Coefficients::makeFirstOrderHighPass (oversampledRate, 720.0f);
    driveGain.reset (oversampledRate, smoothingSeconds);
}

void Overdrive::resetShaper()
{
    highPass.reset();
    driveGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (6.0f + drive.get() * 3.4f));
}

void Overdrive::shape (float* samples, int numSamples) noexcept
{
    // Drive runs from +6 dB to +40 dB.
    driveGain.setTargetValue (juce::Decibels::decibelsToGain (6.0f + drive.get() * 3.4f));

    for (int i = 0; i < numSamples; ++i)
    {
        const auto clean = samples[i];
        const auto clipped = std::tanh (highPass.processSample (clean) * driveGain.getNextValue());

        // Clean plus clipped, as in the original circuit, halved to keep the level in check.
        samples[i] = 0.5f * (clean + clipped);
    }
}

float Overdrive::getToneHz() const noexcept
{
    return toneKnobToHz (tone.get(), 700.0f, 6000.0f);
}

//==============================================================================
void Distortion::prepareShaper (double oversampledRate)
{
    highPass.coefficients = Coefficients::makeFirstOrderHighPass (oversampledRate, 80.0f);
    lowPass.coefficients = Coefficients::makeFirstOrderLowPass (oversampledRate, 9000.0f);
    driveGain.reset (oversampledRate, smoothingSeconds);
}

void Distortion::resetShaper()
{
    highPass.reset();
    lowPass.reset();
    driveGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (10.0f + gain.get() * 4.5f));
}

void Distortion::shape (float* samples, int numSamples) noexcept
{
    // Gain runs from +10 dB to +55 dB.
    driveGain.setTargetValue (juce::Decibels::decibelsToGain (10.0f + gain.get() * 4.5f));

    for (int i = 0; i < numSamples; ++i)
    {
        const auto driven = highPass.processSample (samples[i]) * driveGain.getNextValue();
        samples[i] = 0.3f * lowPass.processSample (hardClip (driven));
    }
}

float Distortion::getToneHz() const noexcept
{
    return toneKnobToHz (tone.get(), 1000.0f, 9000.0f);
}
