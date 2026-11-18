#include "Modulation.h"

#include <cmath>

using namespace pedal_dsp;

namespace
{
constexpr double smoothingSeconds = 0.05;

/** Wraps a phase into 0..1. */
double wrap (double phase) noexcept   { return phase - std::floor (phase); }
}

//==============================================================================
void Flanger::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    const auto maxSamples = (int) std::ceil (delay.maxValue * 2.0 * 0.001 * sampleRate) + 8;
    lineLeft.setMaxSamples (maxSamples);
    lineRight.setMaxSamples (maxSamples);
    centreMs.reset (sampleRate, 0.1);
    wet.reset (sampleRate, smoothingSeconds);
    feedbackGain.reset (sampleRate, smoothingSeconds);
    reset();
}

void Flanger::reset()
{
    lineLeft.clear();
    lineRight.clear();
    lfo.reset();
    centreMs.setCurrentAndTargetValue (delay.get());
    wet.setCurrentAndTargetValue (mix.get() * 0.01f);
    feedbackGain.setCurrentAndTargetValue (feedback.get() * 0.01f);
}

void Flanger::process (float* left, float* right, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    centreMs.setTargetValue (delay.get());
    wet.setTargetValue (mix.get() * 0.01f);
    feedbackGain.setTargetValue (feedback.get() * 0.01f);

    const auto swing = depth.get() * 0.09f;   // up to 90% of the centre delay either way
    const auto msToSamples = (float) (0.001 * sampleRate);
    const auto lfoHz = rate.get();

    for (int i = 0; i < numSamples; ++i)
    {
        const auto phase = lfo.advance (lfoHz, sampleRate);
        const auto centre = centreMs.getNextValue() * msToSamples;

        // The right side sweeps a quarter cycle behind the left.
        const auto delayL = 1.0f + centre * (1.0f + swing * Lfo::triangle (phase));
        const auto delayR = 1.0f + centre * (1.0f + swing * Lfo::triangle (phase + 0.25));

        const auto tapL = lineLeft.read (delayL);
        const auto tapR = lineRight.read (delayR);
        const auto fb = feedbackGain.getNextValue();

        lineLeft.write (left[i] + fb * tapL);
        lineRight.write (right[i] + fb * tapR);

        // At full mix the dry and delayed sound are equal, for the deepest notches.
        const auto amount = 0.5f * wet.getNextValue();
        left[i]  = left[i]  * (1.0f - amount) + tapL * amount;
        right[i] = right[i] * (1.0f - amount) + tapR * amount;
    }
}

//==============================================================================
void Phaser::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    wet.reset (sampleRate, smoothingSeconds);
    reset();
}

void Phaser::reset()
{
    for (auto& stage : stagesLeft)   stage.reset();
    for (auto& stage : stagesRight)  stage.reset();

    lastLeft = lastRight = 0.0f;
    lfo.reset();
    wet.setCurrentAndTargetValue (mix.get() * 0.01f);
}

void Phaser::process (float* left, float* right, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    wet.setTargetValue (mix.get() * 0.01f);

    static constexpr int stageCounts[] = { 4, 6, 8, 12 };
    const auto count = stageCounts[juce::jlimit (0, 3, stages.getIndex())];
    const auto span = 1.0f + depth.get() * 0.35f;   // octaves the sweep covers
    const auto fb = feedback.get() * 0.01f;
    const auto lfoHz = rate.get();

    for (int i = 0; i < numSamples; ++i)
    {
        const auto phase = lfo.advance (lfoHz, sampleRate);
        const auto sweepL = 0.5f + 0.5f * Lfo::sine (phase);
        const auto sweepR = 0.5f + 0.5f * Lfo::sine (phase + 0.25);
        const auto coefficientL = FirstOrderAllPass::coefficientFor (200.0f * std::exp2 (span * sweepL), sampleRate);
        const auto coefficientR = FirstOrderAllPass::coefficientFor (200.0f * std::exp2 (span * sweepR), sampleRate);

        auto l = left[i] + fb * lastLeft;
        auto r = right[i] + fb * lastRight;

        for (int s = 0; s < count; ++s)
        {
            l = stagesLeft[(size_t) s].process (l, coefficientL);
            r = stagesRight[(size_t) s].process (r, coefficientR);
        }

        lastLeft = l;
        lastRight = r;

        const auto amount = 0.5f * wet.getNextValue();
        left[i]  = left[i]  * (1.0f - amount) + l * amount;
        right[i] = right[i] * (1.0f - amount) + r * amount;
    }
}

//==============================================================================
void Vibe::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    reset();
}

void Vibe::reset()
{
    for (auto& stage : stages)
        stage.reset();

    lfo.reset();
}

void Vibe::process (float* samples, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    // The photocells all differ, so each stage sits at its own frequency.
    static constexpr float spread[] = { 1.0f, 1.7f, 2.9f, 4.6f };

    const auto span = 0.5f + intensity.get() * 0.25f;
    const bool chorus = mode.getIndex() == 0;
    const auto lfoHz = speed.get();

    for (int i = 0; i < numSamples; ++i)
    {
        // A bulb heats quickly and cools slowly, so the pulse is lopsided.
        const auto bulb = std::pow (0.5f + 0.5f * Lfo::sine (lfo.advance (lfoHz, sampleRate)), 1.8f);
        const auto base = 110.0f * std::exp2 (span * bulb);

        auto y = samples[i];

        for (size_t s = 0; s < stages.size(); ++s)
            y = stages[s].process (y, FirstOrderAllPass::coefficientFor (base * spread[s], sampleRate));

        samples[i] = chorus ? 0.5f * (samples[i] + y) * 1.3f : y;
    }
}

//==============================================================================
void Vibrato::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    line.setMaxSamples ((int) std::ceil (0.012 * sampleRate));
    swingMs.reset (sampleRate, smoothingSeconds);
    reset();
}

void Vibrato::reset()
{
    line.clear();
    lfo.reset();
    swingMs.setCurrentAndTargetValue (depth.get() * 0.3f);
}

void Vibrato::process (float* samples, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    swingMs.setTargetValue (depth.get() * 0.3f);
    const auto msToSamples = (float) (0.001 * sampleRate);
    const auto lfoHz = rate.get();

    for (int i = 0; i < numSamples; ++i)
    {
        const auto wobble = Lfo::sine (lfo.advance (lfoHz, sampleRate));
        const auto delay = (4.0f + swingMs.getNextValue() * wobble) * msToSamples;

        line.write (samples[i]);
        samples[i] = line.read (1.0f + delay);
    }
}

//==============================================================================
void Tremolo::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    crossover = onePoleCoefficient (sampleRate, 700.0);
    amount.reset (sampleRate, smoothingSeconds);
    reset();
}

void Tremolo::reset()
{
    lowLeftA.reset();
    lowLeftB.reset();
    lowRightA.reset();
    lowRightB.reset();
    lfo.reset();
    amount.setCurrentAndTargetValue (depth.get() * 0.01f);
}

void Tremolo::process (float* left, float* right, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    amount.setTargetValue (depth.get() * 0.01f);
    const auto waveform = shape.getIndex();
    const auto how = mode.getIndex();
    const auto lfoHz = rate.get();

    for (int i = 0; i < numSamples; ++i)
    {
        const auto phase = lfo.advance (lfoHz, sampleRate);
        const auto wave = waveform == 0 ? Lfo::sine (phase)
                        : waveform == 1 ? Lfo::triangle (phase)
                                        : Lfo::softSquare (phase);

        const auto d = amount.getNextValue();
        const auto gainA = 1.0f - d * (0.5f + 0.5f * wave);
        const auto gainB = 1.0f - d * (0.5f - 0.5f * wave);

        if (how == 0)
        {
            left[i] *= gainA;
            right[i] *= gainA;
        }
        else if (how == 1)
        {
            // Bass and treble take turns, which sounds like a gentle phaser.
            const auto lowL = lowLeftB.lowPass (lowLeftA.lowPass (left[i], crossover), crossover);
            const auto lowR = lowRightB.lowPass (lowRightA.lowPass (right[i], crossover), crossover);
            left[i]  = lowL * gainA + (left[i] - lowL) * gainB;
            right[i] = lowR * gainA + (right[i] - lowR) * gainB;
        }
        else
        {
            left[i] *= gainA;
            right[i] *= gainB;
        }
    }
}

//==============================================================================
void RotarySpeaker::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    crossover = onePoleCoefficient (sampleRate, 800.0);

    // The light horn changes speed in under a second; the heavy drum takes a few.
    hornAccel = timeCoefficient (sampleRate, 600.0);
    drumAccel = timeCoefficient (sampleRate, 2500.0);

    hornLine.setMaxSamples ((int) std::ceil (0.004 * sampleRate));
    drumLine.setMaxSamples ((int) std::ceil (0.004 * sampleRate));
    wet.reset (sampleRate, smoothingSeconds);
    reset();
}

void RotarySpeaker::reset()
{
    lowA.reset();
    lowB.reset();
    hornLine.clear();
    drumLine.clear();
    hornPhase = drumPhase = 0.0;
    hornHz = fast.isOn() ? 6.8f : 0.8f;
    drumHz = fast.isOn() ? 5.8f : 0.67f;
    wet.setCurrentAndTargetValue (mix.get() * 0.01f);
}

void RotarySpeaker::process (float* left, float* right, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    wet.setTargetValue (mix.get() * 0.01f);

    const auto hornTarget = fast.isOn() ? 6.8f : 0.8f;
    const auto drumTarget = fast.isOn() ? 5.8f : 0.67f;
    const auto driveGain = juce::Decibels::decibelsToGain (drive.get() * 2.0f);
    const auto driveNorm = 1.0f / std::sqrt (driveGain);
    const auto hornLevel = juce::jmin (1.0f, balance.get() * 0.2f);
    const auto drumLevel = juce::jmin (1.0f, 2.0f - balance.get() * 0.2f);
    const auto msToSamples = (float) (0.001 * sampleRate);

    for (int i = 0; i < numSamples; ++i)
    {
        hornHz += hornAccel * (hornTarget - hornHz);
        drumHz += drumAccel * (drumTarget - drumHz);
        hornPhase = wrap (hornPhase + hornHz / sampleRate);
        drumPhase = wrap (drumPhase + drumHz / sampleRate);

        // The amplifier inside the cabinet, which is often driven a little.
        const auto mono = std::tanh (0.5f * (left[i] + right[i]) * driveGain) * driveNorm;

        const auto low = lowB.lowPass (lowA.lowPass (mono, crossover), crossover);
        const auto high = mono - low;

        hornLine.write (high);
        drumLine.write (low);

        // The microphones sit on opposite sides, so each hears the horn
        // coming towards it while the other hears it going away.
        const auto hornSin = Lfo::sine (hornPhase);
        const auto hornCos = Lfo::sine (hornPhase + 0.25);
        const auto hornL = hornLine.read (1.0f + (1.5f + 0.4f * hornSin) * msToSamples) * (1.0f - 0.35f * (0.5f + 0.5f * hornCos));
        const auto hornR = hornLine.read (1.0f + (1.5f - 0.4f * hornSin) * msToSamples) * (1.0f - 0.35f * (0.5f - 0.5f * hornCos));

        const auto drumSin = Lfo::sine (drumPhase);
        const auto drumCos = Lfo::sine (drumPhase + 0.25);
        const auto drumL = drumLine.read (1.0f + (1.0f + 0.15f * drumSin) * msToSamples) * (1.0f - 0.3f * (0.5f + 0.5f * drumCos));
        const auto drumR = drumLine.read (1.0f + (1.0f - 0.15f * drumSin) * msToSamples) * (1.0f - 0.3f * (0.5f - 0.5f * drumCos));

        const auto outL = 1.2f * (hornL * hornLevel + drumL * drumLevel);
        const auto outR = 1.2f * (hornR * hornLevel + drumR * drumLevel);

        const auto amount = wet.getNextValue();
        left[i]  += amount * (outL - left[i]);
        right[i] += amount * (outR - right[i]);
    }
}

//==============================================================================
void RingModulator::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    wet.reset (sampleRate, smoothingSeconds);
    carrierHz.reset (sampleRate, smoothingSeconds);
    reset();
}

void RingModulator::reset()
{
    carrierPhase = 0.0;
    lfo.reset();
    wet.setCurrentAndTargetValue (mix.get() * 0.01f);
    carrierHz.setCurrentAndTargetValue (frequency.get());
}

void RingModulator::process (float* samples, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    wet.setTargetValue (mix.get() * 0.01f);
    carrierHz.setTargetValue (frequency.get());
    const auto sweepOctaves = sweep.get() * 0.2f;
    const auto lfoHz = rate.get();

    for (int i = 0; i < numSamples; ++i)
    {
        const auto hz = carrierHz.getNextValue() * std::exp2 (sweepOctaves * Lfo::sine (lfo.advance (lfoHz, sampleRate)));
        carrierPhase = wrap (carrierPhase + hz / sampleRate);

        const auto x = samples[i];
        const auto amount = wet.getNextValue();
        samples[i] = x + amount * (x * Lfo::sine (carrierPhase) - x);
    }
}
