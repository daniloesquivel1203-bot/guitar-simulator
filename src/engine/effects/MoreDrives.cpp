#include "MoreDrives.h"

#include <cmath>

using namespace pedal_dsp;

namespace
{
float dbToGain (float db) noexcept   { return juce::Decibels::decibelsToGain (db); }

float toneKnobToHz (float knob, float lowHz, float highHz) noexcept
{
    return lowHz * std::pow (highHz / lowHz, knob / 10.0f);
}
}

//==============================================================================
void TrebleBooster::shape (float* samples, int numSamples) noexcept
{
    // Range sweeps the corner from 300 Hz to about 2.4 kHz.
    const auto corner = 300.0f * std::exp2 (range.get() * 0.3f);
    const auto coefficient = onePoleCoefficient (osRate, corner);
    const auto gain = dbToGain (6.0f + boost.get() * 2.6f);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto x = samples[i];

        // Mostly treble, with a little of the full signal let through.
        const auto driven = (highPass.highPass (x, coefficient) + 0.12f * x) * gain;

        // A germanium transistor clips one side of the wave sooner than the other.
        const auto clipped = driven > 0.0f ? std::tanh (driven) : 0.8f * std::tanh (driven * 1.25f);
        samples[i] = 0.5f * dc.process (clipped);
    }
}

//==============================================================================
void BluesDrive::prepareShaper (double oversampledRate)
{
    highPassCoefficient = onePoleCoefficient (oversampledRate, 100.0);
    dc.prepare (oversampledRate);
}

void BluesDrive::shape (float* samples, int numSamples) noexcept
{
    const auto drive = dbToGain (gain.get() * 3.0f);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto clean = samples[i];
        const auto driven = highPass.highPass (clean, highPassCoefficient) * drive * 2.0f + 0.15f;

        // A gentle curve that rounds peaks off gradually, with a slight lean
        // to one side for a touch of even harmonics.
        const auto clipped = driven / (1.0f + std::abs (driven)) - 0.15f / 1.15f;
        samples[i] = 0.25f * clean + 0.6f * dc.process (clipped);
    }
}

float BluesDrive::getToneHz() const noexcept
{
    return toneKnobToHz (tone.get(), 1500.0f, 10000.0f);
}

//==============================================================================
void VintageFuzz::prepareShaper (double oversampledRate)
{
    highPassCoefficient = onePoleCoefficient (oversampledRate, 40.0);
    dc.prepare (oversampledRate);
}

void VintageFuzz::shape (float* samples, int numSamples) noexcept
{
    const auto drive = dbToGain (20.0f + fuzz.get() * 4.0f);
    const auto starve = 1.0f - bias.get() * 0.1f;
    const auto offset = 0.5f * starve;
    const auto gateWidth = 0.4f * starve;

    const auto transfer = [gateWidth] (float x)
    {
        // A starved transistor needs a strong signal before it conducts at all.
        x = x > 0.0f ? juce::jmax (0.0f, x - gateWidth) : juce::jmin (0.0f, x + gateWidth);
        return x > 0.0f ? std::tanh (x) : 0.75f * std::tanh (x * 1.4f);
    };

    // What the circuit puts out with no input, taken off so silence stays silent.
    const auto atRest = transfer (offset);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto x = highPass.highPass (samples[i], highPassCoefficient) * drive + offset;
        samples[i] = 0.45f * dc.process (transfer (x) - atRest);
    }
}

float VintageFuzz::getToneHz() const noexcept
{
    return toneKnobToHz (tone.get(), 800.0f, 8000.0f);
}

//==============================================================================
void SustainFuzz::prepareShaper (double oversampledRate)
{
    inputHighPass = onePoleCoefficient (oversampledRate, 80.0);
    interstageLowPass = onePoleCoefficient (oversampledRate, 5000.0);

    const auto baseRate = oversampledRate / 4.0;
    toneLow = onePoleCoefficient (baseRate, 450.0);
    toneHigh = onePoleCoefficient (baseRate, 1100.0);
    toneBlend.reset (baseRate, 0.03);
}

void SustainFuzz::resetShaper()
{
    highPass.reset();
    interstage.reset();
    lowBand.reset();
    highBand.reset();
    toneBlend.setCurrentAndTargetValue (tone.get() * 0.1f);
}

void SustainFuzz::shape (float* samples, int numSamples) noexcept
{
    const auto firstGain = dbToGain (12.0f + sustain.get() * 2.8f);
    const auto secondGain = dbToGain (14.0f);

    for (int i = 0; i < numSamples; ++i)
    {
        // Two clipping stages in a row, with the fizz filtered off between them.
        auto x = std::tanh (highPass.highPass (samples[i], inputHighPass) * firstGain);
        x = interstage.lowPass (x, interstageLowPass);
        samples[i] = 0.4f * std::tanh (x * secondGain);
    }
}

void SustainFuzz::postShape (float* samples, int numSamples) noexcept
{
    toneBlend.setTargetValue (tone.get() * 0.1f);

    for (int i = 0; i < numSamples; ++i)
    {
        // Blending a low-passed copy with a high-passed one leaves a dip in
        // the middle, deepest with the knob at noon.
        const auto blend = toneBlend.getNextValue();
        const auto low = lowBand.lowPass (samples[i], toneLow);
        const auto high = highBand.highPass (samples[i], toneHigh);
        samples[i] = 1.6f * ((1.0f - blend) * low + blend * high);
    }
}

//==============================================================================
void OctaveFuzz::prepareShaper (double oversampledRate)
{
    highPassCoefficient = onePoleCoefficient (oversampledRate, 60.0);
    dc.prepare (oversampledRate);
}

void OctaveFuzz::shape (float* samples, int numSamples) noexcept
{
    const auto drive = dbToGain (10.0f + fuzz.get() * 3.0f);
    const auto octaveAmount = octave.get() * 0.1f;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto first = std::tanh (highPass.highPass (samples[i], highPassCoefficient) * drive);

        // Flipping the negative half of the wave up doubles its frequency.
        const auto folded = octaveAmount * std::abs (first) * 2.0f + (1.0f - octaveAmount) * first;
        samples[i] = 0.4f * std::tanh (4.0f * dc.process (folded));
    }
}

float OctaveFuzz::getToneHz() const noexcept
{
    return toneKnobToHz (tone.get(), 1000.0f, 8000.0f);
}

//==============================================================================
void MetalDistortion::prepareShaper (double oversampledRate)
{
    baseRate = oversampledRate / 4.0;
    tightCoefficient = onePoleCoefficient (oversampledRate, 160.0);
    interstageCoefficient = onePoleCoefficient (oversampledRate, 6500.0);
    dc.prepare (oversampledRate);
    fizzCut.coefficients = Coefficients::makeLowPass (baseRate, juce::jmin (7500.0f, (float) (baseRate * 0.45)), 0.7f);
    lastLow = lastMid = lastHigh = -100.0f;
    updateEq();
}

void MetalDistortion::resetShaper()
{
    tight.reset();
    interstage.reset();
    dc.reset();
    lowShelf.reset();
    midPeak.reset();
    highShelf.reset();
    fizzCut.reset();
}

void MetalDistortion::shape (float* samples, int numSamples) noexcept
{
    const auto drive = dbToGain (20.0f + gain.get() * 4.5f);

    for (int i = 0; i < numSamples; ++i)
    {
        // Cutting the lows before clipping keeps palm mutes tight instead of flubby.
        auto x = tight.highPass (samples[i], tightCoefficient) * drive;
        x = fastTanh (x * 0.25f);
        x = interstage.lowPass (x, interstageCoefficient);
        x = fastTanh (x * 6.0f);
        samples[i] = 0.3f * dc.process (x);
    }
}

void MetalDistortion::updateEq() noexcept
{
    const auto newLow = low.get(), newMid = mid.get(), newHigh = high.get();

    if (! juce::exactlyEqual (newLow, lastLow))
    {
        lastLow = newLow;
        lowShelf.coefficients = Coefficients::makeLowShelf (baseRate, 110.0f, 0.7f, dbToGain (newLow));
    }

    if (! juce::exactlyEqual (newMid, lastMid))
    {
        lastMid = newMid;
        midPeak.coefficients = Coefficients::makePeakFilter (baseRate, 750.0f, 0.8f, dbToGain (newMid));
    }

    if (! juce::exactlyEqual (newHigh, lastHigh))
    {
        lastHigh = newHigh;
        highShelf.coefficients = Coefficients::makeHighShelf (baseRate, juce::jmin (3200.0f, (float) (baseRate * 0.4)),
                                                              0.7f, dbToGain (newHigh));
    }
}

void MetalDistortion::postShape (float* samples, int numSamples) noexcept
{
    updateEq();

    for (int i = 0; i < numSamples; ++i)
        samples[i] = fizzCut.processSample (highShelf.processSample (midPeak.processSample (lowShelf.processSample (samples[i]))));
}

//==============================================================================
void BitCrusher::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    wet.reset (sampleRate, 0.03);
    reset();
}

void BitCrusher::reset()
{
    holdPhase = 1.0;
    held = 0.0f;
    wet.setCurrentAndTargetValue (mix.get() * 0.01f);
}

void BitCrusher::process (float* samples, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    wet.setTargetValue (mix.get() * 0.01f);
    const auto steps = std::exp2 (bits.get() - 1.0f);
    const auto holdStep = juce::jmin (1.0, (double) rate.get() / sampleRate);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto x = samples[i];

        // Sample-and-hold: only take a new value at the lower sample rate.
        holdPhase += holdStep;

        if (holdPhase >= 1.0)
        {
            holdPhase -= 1.0;
            held = std::round (juce::jlimit (-1.0f, 1.0f, x) * steps) / steps;
        }

        const auto amount = wet.getNextValue();
        samples[i] = x + amount * (held - x);
    }
}
