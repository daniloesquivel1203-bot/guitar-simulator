#pragma once

#include "Effect.h"

#include <juce_audio_basics/juce_audio_basics.h>

/** Silences the signal when you are not playing, to hide hum and hiss.

    The gate opens when the level rises above the threshold and closes when it
    falls a little below it. The gap stops it chattering on a decaying note.
*/
class NoiseGate final : public MonoEffect
{
public:
    NoiseGate() : MonoEffect ("Noise Gate", "gate.enabled") {}

    Parameter threshold { "gate.threshold", "Threshold", -80.0f, -20.0f, -55.0f, 0.5f, " dB" };
    Parameter release   { "gate.release",   "Release",   20.0f,  1000.0f, 150.0f, 1.0f, " ms", true };

    std::vector<Parameter*> getControls() override   { return { &threshold, &release }; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* samples, int numSamples) noexcept override;

private:
    static constexpr float hysteresisDb = 6.0f;

    double rate = 48000.0;
    float detectorRelease = 0.0f;
    float openCoefficient = 0.0f;
    float envelope = 0.0f;
    float gain = 0.0f;
    bool open = false;
};

/** Evens out the volume by turning down loud notes. With makeup gain this
    brings quiet notes up, adding sustain and making playing sound more even.
*/
class Compressor final : public MonoEffect
{
public:
    Compressor() : MonoEffect ("Compressor", "comp.enabled") {}

    Parameter threshold { "comp.threshold", "Threshold", -50.0f, 0.0f,    -28.0f, 0.5f, " dB" };
    Parameter ratio     { "comp.ratio",     "Ratio",     1.0f,   20.0f,   4.0f,   0.1f, ":1", true };
    Parameter attack    { "comp.attack",    "Attack",    1.0f,   100.0f,  10.0f,  0.1f, " ms", true };
    Parameter release   { "comp.release",   "Release",   20.0f,  1000.0f, 150.0f, 1.0f, " ms", true };
    Parameter makeup    { "comp.makeup",    "Makeup",    0.0f,   24.0f,   6.0f,   0.1f, " dB" };

    std::vector<Parameter*> getControls() override   { return { &threshold, &ratio, &attack, &release, &makeup }; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* samples, int numSamples) noexcept override;

private:
    static constexpr float kneeDb = 6.0f;

    double rate = 48000.0;
    float reductionDb = 0.0f;
    juce::SmoothedValue<float> makeupGain { 1.0f };
};
