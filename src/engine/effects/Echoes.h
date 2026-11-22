#pragma once

#include "DspHelpers.h"
#include "Effect.h"
#include "TempoSync.h"

/** A tape echo: each repeat gets darker and more saturated, and the tape's
    uneven speed makes the echoes waver. Turn Repeats right up for runaway
    feedback that never quite explodes. */
class TapeEcho final : public StereoEffect
{
public:
    TapeEcho() : StereoEffect ("Tape Echo", "tape.enabled") {}

    Parameter time    { "tape.time",    "Time",    50.0f, 1000.0f, 350.0f, 1.0f, " ms", true };
    Parameter repeats { "tape.repeats", "Repeats", 0.0f,  100.0f,  40.0f,  1.0f, " %" };
    Parameter mix     { "tape.mix",     "Mix",     0.0f,  100.0f,  35.0f,  1.0f, " %" };
    Parameter wow     { "tape.wow",     "Wow",     0.0f,  10.0f,   3.0f };
    Parameter age     { "tape.age",     "Age",     0.0f,  10.0f,   4.0f };
    Parameter sync    { "tape.sync",    "Sync",    tempo_sync::choiceNames() };

    std::vector<Parameter*> getControls() override   { return { &time, &repeats, &mix, &wow, &age, &sync }; }
    Parameter* getTapTimeParameter() noexcept override     { return &time; }
    Parameter* getTempoSyncParameter() noexcept override   { return &sync; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* left, float* right, int numSamples) noexcept override;

private:
    double sampleRate = 48000.0;
    FractionalDelay line;
    pedal_dsp::OnePole lowPassA, lowPassB, highPass;
    pedal_dsp::Lfo wowLfo, flutterLfo;
    float highPassCoefficient = 0.0f;
    juce::SmoothedValue<float> delaySamples { 1000.0f }, wet { 0.35f }, feedbackGain { 0.4f };
};

/** A bucket-brigade style delay: short, warm and murky, with a slight
    chorus on the repeats. Longer times get darker, as on the real chips. */
class AnalogDelay final : public StereoEffect
{
public:
    AnalogDelay() : StereoEffect ("Analog Delay", "analogdelay.enabled") {}

    Parameter time       { "analogdelay.time",       "Time",       20.0f, 600.0f, 300.0f, 1.0f, " ms", true };
    Parameter feedback   { "analogdelay.feedback",   "Feedback",   0.0f,  100.0f, 45.0f,  1.0f, " %" };
    Parameter mix        { "analogdelay.mix",        "Mix",        0.0f,  100.0f, 35.0f,  1.0f, " %" };
    Parameter modulation { "analogdelay.modulation", "Modulation", 0.0f,  10.0f,  3.0f };
    Parameter sync       { "analogdelay.sync",       "Sync",       tempo_sync::choiceNames() };

    std::vector<Parameter*> getControls() override   { return { &time, &feedback, &mix, &modulation, &sync }; }
    Parameter* getTapTimeParameter() noexcept override     { return &time; }
    Parameter* getTempoSyncParameter() noexcept override   { return &sync; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* left, float* right, int numSamples) noexcept override;

private:
    double sampleRate = 48000.0;
    FractionalDelay line;
    pedal_dsp::OnePole lowPassA, lowPassB;
    pedal_dsp::Lfo lfo;
    juce::SmoothedValue<float> delaySamples { 1000.0f }, wet { 0.35f }, feedbackGain { 0.45f };
};

/** Plays each slice of what you just played backwards, so notes swell in
    instead of starting with the pick. Time is the length of each slice. */
class ReverseDelay final : public StereoEffect
{
public:
    ReverseDelay() : StereoEffect ("Reverse Delay", "reverse.enabled") {}

    Parameter time     { "reverse.time",     "Time",     150.0f, 1500.0f, 500.0f, 1.0f, " ms", true };
    Parameter feedback { "reverse.feedback", "Feedback", 0.0f,   80.0f,   30.0f,  1.0f, " %" };
    Parameter mix      { "reverse.mix",      "Mix",      0.0f,   100.0f,  50.0f,  1.0f, " %" };
    Parameter sync     { "reverse.sync",     "Sync",     tempo_sync::choiceNames() };

    std::vector<Parameter*> getControls() override   { return { &time, &feedback, &mix, &sync }; }
    Parameter* getTapTimeParameter() noexcept override     { return &time; }
    Parameter* getTempoSyncParameter() noexcept override   { return &sync; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* left, float* right, int numSamples) noexcept override;

private:
    int currentSliceLength() const noexcept;

    double sampleRate = 48000.0;
    std::vector<float> buffer;
    int writeIndex = 0;
    int sliceLength = 24000, position = 0;
    float lastOut = 0.0f;
    juce::SmoothedValue<float> wet { 0.5f }, feedbackGain { 0.3f };
};
