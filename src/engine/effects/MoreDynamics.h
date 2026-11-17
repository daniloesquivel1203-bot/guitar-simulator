#pragma once

#include "DspHelpers.h"
#include "Effect.h"

#include <juce_dsp/juce_dsp.h>

/** A slow, smooth leveling compressor in the style of the optical studio
    units. The release gets slower the longer it has been compressing, so it
    evens out a whole phrase rather than pumping on each note. */
class OptoCompressor final : public MonoEffect
{
public:
    OptoCompressor() : MonoEffect ("Opto Compressor", "opto.enabled") {}

    Parameter reduction { "opto.reduction", "Squeeze", 0.0f, 10.0f,  4.0f };
    Parameter gain      { "opto.gain",      "Gain",    0.0f, 20.0f,  4.0f,   0.1f, " dB" };
    Parameter mix       { "opto.mix",       "Mix",     0.0f, 100.0f, 100.0f, 1.0f, " %" };

    std::vector<Parameter*> getControls() override   { return { &reduction, &gain, &mix }; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* samples, int numSamples) noexcept override;

private:
    double rate = 48000.0;
    float envelope = 0.0f, reductionDb = 0.0f, memory = 0.0f;
    float detectorAttack = 0.0f, detectorRelease = 0.0f, gainAttack = 0.0f, memoryCoefficient = 0.0f;
    juce::SmoothedValue<float> makeup { 1.0f }, wet { 1.0f };
};

/** A clean volume boost with gentle bass and treble controls. In front of
    the amp it pushes it into breakup; after it, it is a solo volume lift. */
class CleanBoost final : public MonoEffect
{
public:
    CleanBoost() : MonoEffect ("Clean Boost", "boost.enabled") {}

    Parameter gain   { "boost.gain",   "Boost",  0.0f,   20.0f, 6.0f, 0.1f, " dB" };
    Parameter bass   { "boost.bass",   "Bass",   -10.0f, 10.0f, 0.0f, 0.1f, " dB" };
    Parameter treble { "boost.treble", "Treble", -10.0f, 10.0f, 0.0f, 0.1f, " dB" };

    std::vector<Parameter*> getControls() override   { return { &gain, &bass, &treble }; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* samples, int numSamples) noexcept override;

private:
    using Filter = juce::dsp::IIR::Filter<float>;
    using Coefficients = juce::dsp::IIR::Coefficients<float>;

    void update() noexcept;

    double rate = 48000.0;
    Filter bassFilter, trebleFilter;
    float lastBass = -100.0f, lastTreble = -100.0f;
    juce::SmoothedValue<float> level { 1.0f };
};

/** Fades every new note in from silence, removing the pick attack, for
    violin-like swells without touching the volume knob. */
class AutoSwell final : public MonoEffect
{
public:
    AutoSwell() : MonoEffect ("Auto Swell", "swell.enabled") {}

    Parameter threshold { "swell.threshold", "Sensitivity", -70.0f, -20.0f, -45.0f, 0.5f, " dB" };
    Parameter time      { "swell.time",      "Swell",       30.0f,  2000.0f, 400.0f, 1.0f, " ms", true };

    std::vector<Parameter*> getControls() override   { return { &threshold, &time }; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* samples, int numSamples) noexcept override;

private:
    double rate = 48000.0;
    float fastEnvelope = 0.0f, slowEnvelope = 0.0f, ramp = 1.0f;
    float fastAttack = 0.0f, fastRelease = 0.0f, slowAttack = 0.0f, slowRelease = 0.0f;
    int holdOff = 0;
};
