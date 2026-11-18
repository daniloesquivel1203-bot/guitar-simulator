#pragma once

#include "DspHelpers.h"
#include "Effect.h"

#include <array>

/** The swooshing "jet plane" sweep: a very short delay that moves, mixed
    with the dry sound. Feedback makes it sharper and more metallic. */
class Flanger final : public StereoEffect
{
public:
    Flanger() : StereoEffect ("Flanger", "flanger.enabled") {}

    Parameter rate     { "flanger.rate",     "Rate",     0.05f, 5.0f,   0.3f,  0.01f, " Hz", true };
    Parameter depth    { "flanger.depth",    "Depth",    0.0f,  10.0f,  7.0f };
    Parameter delay    { "flanger.delay",    "Manual",   0.5f,  8.0f,   2.5f,  0.01f, " ms", true };
    Parameter feedback { "flanger.feedback", "Feedback", -95.0f, 95.0f, 50.0f, 1.0f,  " %" };
    Parameter mix      { "flanger.mix",      "Mix",      0.0f,  100.0f, 100.0f, 1.0f, " %" };

    std::vector<Parameter*> getControls() override   { return { &rate, &depth, &delay, &feedback, &mix }; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* left, float* right, int numSamples) noexcept override;

private:
    double sampleRate = 48000.0;
    FractionalDelay lineLeft, lineRight;
    pedal_dsp::Lfo lfo;
    juce::SmoothedValue<float> centreMs { 2.5f }, wet { 1.0f }, feedbackGain { 0.5f };
};

/** A sweeping, liquid whoosh made by chaining all-pass filters and mixing
    them with the dry sound. More stages give more notches and a deeper sweep. */
class Phaser final : public StereoEffect
{
public:
    Phaser() : StereoEffect ("Phaser", "phaser.enabled") {}

    Parameter rate     { "phaser.rate",     "Rate",     0.05f, 8.0f,   0.5f,  0.01f, " Hz", true };
    Parameter depth    { "phaser.depth",    "Depth",    0.0f,  10.0f,  7.0f };
    Parameter feedback { "phaser.feedback", "Feedback", 0.0f,  90.0f,  40.0f, 1.0f, " %" };
    Parameter mix      { "phaser.mix",      "Mix",      0.0f,  100.0f, 100.0f, 1.0f, " %" };
    Parameter stages   { "phaser.stages",   "Stages",   { "4 stages", "6 stages", "8 stages", "12 stages" } };

    std::vector<Parameter*> getControls() override   { return { &rate, &depth, &feedback, &mix, &stages }; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* left, float* right, int numSamples) noexcept override;

private:
    static constexpr int maxStages = 12;

    double sampleRate = 48000.0;
    std::array<pedal_dsp::FirstOrderAllPass, maxStages> stagesLeft, stagesRight;
    float lastLeft = 0.0f, lastRight = 0.0f;
    pedal_dsp::Lfo lfo;
    juce::SmoothedValue<float> wet { 1.0f };
};

/** A throbbing, watery sweep from four uneven filter stages driven by a
    lopsided pulse, like the old light-bulb-and-photocell units. Chorus mode
    mixes in the dry sound; Vibrato mode is the wobbling pitch alone. */
class Vibe final : public MonoEffect
{
public:
    Vibe() : MonoEffect ("Vibe", "vibe.enabled") {}

    Parameter speed     { "vibe.speed",     "Speed",     0.5f, 10.0f, 3.0f, 0.01f, " Hz", true };
    Parameter intensity { "vibe.intensity", "Intensity", 0.0f, 10.0f, 7.0f };
    Parameter mode      { "vibe.mode",      "Mode",      { "Chorus", "Vibrato" } };

    std::vector<Parameter*> getControls() override   { return { &speed, &intensity, &mode }; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* samples, int numSamples) noexcept override;

private:
    double sampleRate = 48000.0;
    std::array<pedal_dsp::FirstOrderAllPass, 4> stages;
    pedal_dsp::Lfo lfo;
};

/** Pitch wobble alone, with no dry signal, like a whammy bar shaken in time. */
class Vibrato final : public MonoEffect
{
public:
    Vibrato() : MonoEffect ("Vibrato", "vibrato.enabled") {}

    Parameter rate  { "vibrato.rate",  "Rate",  0.5f, 12.0f, 5.0f, 0.01f, " Hz", true };
    Parameter depth { "vibrato.depth", "Depth", 0.0f, 10.0f, 4.0f };

    std::vector<Parameter*> getControls() override   { return { &rate, &depth }; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* samples, int numSamples) noexcept override;

private:
    double sampleRate = 48000.0;
    FractionalDelay line;
    pedal_dsp::Lfo lfo;
    juce::SmoothedValue<float> swingMs { 0.0f };
};

/** Pulses the volume. Harmonic mode pulses the bass and treble in turn, as
    some 1960s amps did; Pan mode swings the sound between the ears. */
class Tremolo final : public StereoEffect
{
public:
    Tremolo() : StereoEffect ("Tremolo", "trem.enabled") {}

    Parameter rate  { "trem.rate",  "Rate",  0.5f, 15.0f,  5.0f,  0.01f, " Hz", true };
    Parameter depth { "trem.depth", "Depth", 0.0f, 100.0f, 60.0f, 1.0f,  " %" };
    Parameter shape { "trem.shape", "Shape", { "Sine", "Triangle", "Square" } };
    Parameter mode  { "trem.mode",  "Mode",  { "Classic", "Harmonic", "Pan" } };

    std::vector<Parameter*> getControls() override   { return { &rate, &depth, &shape, &mode }; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* left, float* right, int numSamples) noexcept override;

private:
    double sampleRate = 48000.0;
    float crossover = 0.0f;
    pedal_dsp::OnePole lowLeftA, lowLeftB, lowRightA, lowRightB;
    pedal_dsp::Lfo lfo;
    juce::SmoothedValue<float> amount { 0.6f };
};

/** A rotating speaker cabinet: a spinning treble horn and bass drum, heard
    through two microphones. Flip Fast to hear them speed up and slow down. */
class RotarySpeaker final : public StereoEffect
{
public:
    RotarySpeaker() : StereoEffect ("Rotary Speaker", "rotary.enabled") {}

    Parameter fast    { "rotary.fast",    "Fast",    0.0f, 1.0f,   0.0f,  1.0f };
    Parameter drive   { "rotary.drive",   "Drive",   0.0f, 10.0f,  2.0f };
    Parameter balance { "rotary.balance", "Horn",    0.0f, 10.0f,  5.0f };
    Parameter mix     { "rotary.mix",     "Mix",     0.0f, 100.0f, 100.0f, 1.0f, " %" };

    std::vector<Parameter*> getControls() override   { return { &drive, &balance, &mix, &fast }; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* left, float* right, int numSamples) noexcept override;

private:
    double sampleRate = 48000.0;
    float crossover = 0.0f, hornAccel = 0.0f, drumAccel = 0.0f;
    float hornHz = 0.8f, drumHz = 0.67f;
    double hornPhase = 0.0, drumPhase = 0.0;
    pedal_dsp::OnePole lowA, lowB;
    FractionalDelay hornLine, drumLine;
    juce::SmoothedValue<float> wet { 1.0f };
};

/** Multiplies the guitar by a steady tone, giving clangorous, bell-like and
    robotic sounds that ignore the key you play in. */
class RingModulator final : public MonoEffect
{
public:
    RingModulator() : MonoEffect ("Ring Modulator", "ringmod.enabled") {}

    Parameter frequency { "ringmod.frequency", "Frequency", 20.0f, 2000.0f, 440.0f, 1.0f, " Hz", true };
    Parameter sweep     { "ringmod.sweep",     "Sweep",     0.0f,  10.0f,   0.0f };
    Parameter rate      { "ringmod.rate",      "Rate",      0.1f,  10.0f,   1.0f,   0.01f, " Hz", true };
    Parameter mix       { "ringmod.mix",       "Mix",       0.0f,  100.0f,  100.0f, 1.0f,  " %" };

    std::vector<Parameter*> getControls() override   { return { &frequency, &sweep, &rate, &mix }; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* samples, int numSamples) noexcept override;

private:
    double sampleRate = 48000.0;
    double carrierPhase = 0.0;
    pedal_dsp::Lfo lfo;
    juce::SmoothedValue<float> wet { 1.0f }, carrierHz { 440.0f };
};
