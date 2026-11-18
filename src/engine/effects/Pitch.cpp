#include "Pitch.h"

#include <cmath>

using namespace pedal_dsp;

namespace
{
constexpr double smoothingSeconds = 0.05;
}

//==============================================================================
void Octaver::prepare (double sampleRate, int)
{
    // Longer windows suit lower notes.
    shiftDown.prepare (sampleRate, 60.0f);
    shiftDown2.prepare (sampleRate, 90.0f);
    shiftUp.prepare (sampleRate, 30.0f);

    trackingCoefficient = onePoleCoefficient (sampleRate, 1200.0);
    smoothingCoefficient = onePoleCoefficient (sampleRate, 1500.0);

    for (auto* gain : { &dryGain, &downGain, &down2Gain, &upGain })
        gain->reset (sampleRate, smoothingSeconds);

    reset();
}

void Octaver::reset()
{
    tracking.reset();
    smoothDown.reset();
    smoothDown2.reset();
    shiftDown.reset();
    shiftDown2.reset();
    shiftUp.reset();
    dryGain.setCurrentAndTargetValue (dry.get() * 0.01f);
    downGain.setCurrentAndTargetValue (down.get() * 0.01f);
    down2Gain.setCurrentAndTargetValue (down2.get() * 0.01f);
    upGain.setCurrentAndTargetValue (up.get() * 0.01f);
}

void Octaver::process (float* samples, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    dryGain.setTargetValue (dry.get() * 0.01f);
    downGain.setTargetValue (down.get() * 0.01f);
    down2Gain.setTargetValue (down2.get() * 0.01f);
    upGain.setTargetValue (up.get() * 0.01f);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto x = samples[i];

        // The low voices work from a darker copy, which tracks more cleanly.
        const auto dark = tracking.lowPass (x, trackingCoefficient);
        const auto lower = smoothDown.lowPass (shiftDown.process (dark, 0.5f), smoothingCoefficient);
        const auto lowest = smoothDown2.lowPass (shiftDown2.process (dark, 0.25f), smoothingCoefficient);
        const auto higher = shiftUp.process (x, 2.0f);

        samples[i] = x * dryGain.getNextValue()
                   + 1.3f * lower * downGain.getNextValue()
                   + 1.3f * lowest * down2Gain.getNextValue()
                   + higher * upGain.getNextValue();
    }
}

//==============================================================================
void PitchShifter::prepare (double sampleRate, int)
{
    shifter.prepare (sampleRate, 40.0f);
    wet.reset (sampleRate, smoothingSeconds);
    ratio.reset (sampleRate, smoothingSeconds);
    reset();
}

void PitchShifter::reset()
{
    shifter.reset();
    wet.setCurrentAndTargetValue (mix.get() * 0.01f);
    ratio.setCurrentAndTargetValue (semitonesToRatio (shift.get() + fine.get() * 0.01f));
}

void PitchShifter::process (float* samples, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    wet.setTargetValue (mix.get() * 0.01f);
    ratio.setTargetValue (semitonesToRatio (shift.get() + fine.get() * 0.01f));

    for (int i = 0; i < numSamples; ++i)
    {
        const auto x = samples[i];
        const auto shifted = shifter.process (x, ratio.getNextValue());
        const auto amount = wet.getNextValue();
        samples[i] = x * (1.0f - amount) + shifted * amount;
    }
}

//==============================================================================
void Detune::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    shiftLeft.prepare (sampleRate, 25.0f);
    shiftRight.prepare (sampleRate, 25.0f);

    const auto maxSamples = (int) std::ceil (delay.maxValue * 0.001 * sampleRate) + 8;
    lineLeft.setMaxSamples (maxSamples);
    lineRight.setMaxSamples (maxSamples);

    wet.reset (sampleRate, smoothingSeconds);
    delaySamples.reset (sampleRate, 0.1);
    reset();
}

void Detune::reset()
{
    shiftLeft.reset();
    shiftRight.reset();
    lineLeft.clear();
    lineRight.clear();
    wet.setCurrentAndTargetValue (mix.get() * 0.01f);
    delaySamples.setCurrentAndTargetValue ((float) (delay.get() * 0.001 * sampleRate));
}

void Detune::process (float* left, float* right, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    wet.setTargetValue (mix.get() * 0.01f);
    delaySamples.setTargetValue ((float) (delay.get() * 0.001 * sampleRate));

    // One copy goes sharp in the left ear, the other flat in the right.
    const auto sharp = semitonesToRatio (cents.get() * 0.01f);
    const auto flat = 1.0f / sharp;

    for (int i = 0; i < numSamples; ++i)
    {
        lineLeft.write (shiftLeft.process (left[i], sharp));
        lineRight.write (shiftRight.process (right[i], flat));

        const auto d = 1.0f + delaySamples.getNextValue();
        const auto amount = wet.getNextValue();
        left[i]  = left[i]  * (1.0f - 0.5f * amount) + lineLeft.read (d) * amount * 0.5f;
        right[i] = right[i] * (1.0f - 0.5f * amount) + lineRight.read (d) * amount * 0.5f;
    }
}
