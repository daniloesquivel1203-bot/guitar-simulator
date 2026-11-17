#pragma once

#include "DspHelpers.h"
#include "Effect.h"

/** Adds notes one and two octaves below, and one above, what you play. The
    lower octaves make a guitar sound like guitar and bass together. */
class Octaver final : public MonoEffect
{
public:
    Octaver() : MonoEffect ("Octaver", "octave.enabled") {}

    Parameter dry   { "octave.dry",   "Dry",        0.0f, 100.0f, 100.0f, 1.0f, " %" };
    Parameter down  { "octave.down",  "Octave down", 0.0f, 100.0f, 70.0f, 1.0f, " %" };
    Parameter down2 { "octave.down2", "Two down",   0.0f, 100.0f, 0.0f,   1.0f, " %" };
    Parameter up    { "octave.up",    "Octave up",  0.0f, 100.0f, 0.0f,   1.0f, " %" };

    std::vector<Parameter*> getControls() override   { return { &dry, &down, &down2, &up }; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* samples, int numSamples) noexcept override;

private:
    float trackingCoefficient = 0.0f, smoothingCoefficient = 0.0f;
    pedal_dsp::OnePole tracking, smoothDown, smoothDown2;
    pedal_dsp::GrainPitchShifter shiftDown, shiftDown2, shiftUp;
    juce::SmoothedValue<float> dryGain { 1.0f }, downGain { 0.0f }, down2Gain { 0.0f }, upGain { 0.0f };
};

/** Moves everything you play up or down by a set interval, mixed with the
    original for instant harmonies, or alone for a transposed guitar. */
class PitchShifter final : public MonoEffect
{
public:
    PitchShifter() : MonoEffect ("Pitch Shifter", "pitch.enabled") {}

    Parameter shift { "pitch.shift", "Interval", -12.0f, 12.0f,  7.0f,  1.0f, " st" };
    Parameter fine  { "pitch.fine",  "Fine",     -50.0f, 50.0f,  0.0f,  1.0f, " ct" };
    Parameter mix   { "pitch.mix",   "Mix",      0.0f,   100.0f, 50.0f, 1.0f, " %" };

    std::vector<Parameter*> getControls() override   { return { &shift, &fine, &mix }; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* samples, int numSamples) noexcept override;

private:
    pedal_dsp::GrainPitchShifter shifter;
    juce::SmoothedValue<float> wet { 0.5f }, ratio { 1.0f };
};

/** Two slightly detuned, slightly late copies, one in each ear: sounds like
    the part was recorded twice. Thicker and steadier than a chorus. */
class Detune final : public StereoEffect
{
public:
    Detune() : StereoEffect ("Detune", "detune.enabled") {}

    Parameter cents { "detune.cents", "Detune", 0.0f, 50.0f,  12.0f, 0.1f, " ct" };
    Parameter delay { "detune.delay", "Delay",  0.0f, 40.0f,  15.0f, 0.1f, " ms" };
    Parameter mix   { "detune.mix",   "Mix",    0.0f, 100.0f, 50.0f, 1.0f, " %" };

    std::vector<Parameter*> getControls() override   { return { &cents, &delay, &mix }; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* left, float* right, int numSamples) noexcept override;

private:
    double sampleRate = 48000.0;
    pedal_dsp::GrainPitchShifter shiftLeft, shiftRight;
    FractionalDelay lineLeft, lineRight;
    juce::SmoothedValue<float> wet { 0.5f }, delaySamples { 0.0f };
};
