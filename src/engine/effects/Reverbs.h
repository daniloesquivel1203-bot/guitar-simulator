#pragma once

#include "DspHelpers.h"
#include "Effect.h"

#include <array>

/** The engine behind the hall, plate and shimmer reverbs: eight delay lines
    that feed into each other through a mixing matrix (a "feedback delay
    network"), with treble loss in every line and slow wobble to keep the
    tail from ringing metallically. */
class FeedbackDelayNetwork
{
public:
    struct Settings
    {
        float sizeScale = 1.0f;          // multiplies every line length
        float decaySeconds = 2.0f;       // time for the tail to fall by 60 dB
        float dampingHz = 6000.0f;       // treble above this dies away faster
        float modulationSamples = 0.0f;  // how far the line lengths wobble
        float diffusion = 0.7f;          // how quickly echoes smear into a wash
    };

    void prepare (double sampleRate, float maxSizeScale);
    void reset();

    /** Call once per block, before process(). */
    void setSettings (const Settings&) noexcept;

    /** Mono in, stereo wet signal out. */
    void process (float input, float& left, float& right) noexcept;

private:
    static constexpr int numLines = 8;

    double sampleRate = 48000.0;
    Settings settings;
    std::array<FractionalDelay, numLines> lines;
    std::array<float, numLines> lengths {}, gains {}, damping {};
    std::array<pedal_dsp::Lfo, numLines> wobble;
    std::array<pedal_dsp::DiffusionAllPass, 4> diffusers;
    float dampingCoefficient = 0.5f;
};

/** The big, smooth, long reverb of a concert hall. */
class HallReverb final : public StereoEffect
{
public:
    HallReverb() : StereoEffect ("Hall Reverb", "hall.enabled") {}

    Parameter decay      { "hall.decay",      "Decay",      0.5f, 12.0f,  3.0f,  0.01f, " s", true };
    Parameter preDelay   { "hall.predelay",   "Pre-delay",  0.0f, 200.0f, 25.0f, 1.0f,  " ms" };
    Parameter tone       { "hall.tone",       "Tone",       0.0f, 10.0f,  6.0f };
    Parameter modulation { "hall.modulation", "Modulation", 0.0f, 10.0f,  4.0f };
    Parameter mix        { "hall.mix",        "Mix",        0.0f, 100.0f, 30.0f, 1.0f, " %" };

    std::vector<Parameter*> getControls() override   { return { &decay, &preDelay, &tone, &modulation, &mix }; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* left, float* right, int numSamples) noexcept override;

private:
    double sampleRate = 48000.0;
    FeedbackDelayNetwork network;
    FractionalDelay preDelayLine;
    juce::SmoothedValue<float> wet { 0.3f }, preDelaySamples { 0.0f };
};

/** The bright, dense, instantly thick sound of a studio plate reverb. */
class PlateReverb final : public StereoEffect
{
public:
    PlateReverb() : StereoEffect ("Plate Reverb", "plate.enabled") {}

    Parameter decay    { "plate.decay",    "Decay",     0.3f, 6.0f,   1.8f,  0.01f, " s", true };
    Parameter preDelay { "plate.predelay", "Pre-delay", 0.0f, 100.0f, 0.0f,  1.0f,  " ms" };
    Parameter tone     { "plate.tone",     "Tone",      0.0f, 10.0f,  7.0f };
    Parameter mix      { "plate.mix",      "Mix",       0.0f, 100.0f, 25.0f, 1.0f, " %" };

    std::vector<Parameter*> getControls() override   { return { &decay, &preDelay, &tone, &mix }; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* left, float* right, int numSamples) noexcept override;

private:
    double sampleRate = 48000.0;
    FeedbackDelayNetwork network;
    FractionalDelay preDelayLine;
    pedal_dsp::OnePole lowCut;
    float lowCutCoefficient = 0.0f;
    juce::SmoothedValue<float> wet { 0.25f }, preDelaySamples { 0.0f };
};

/** A huge reverb whose tail keeps climbing an octave, like a choir or an
    organ appearing behind the guitar. Lovely under slow chords. */
class ShimmerReverb final : public StereoEffect
{
public:
    ShimmerReverb() : StereoEffect ("Shimmer Reverb", "shimmer.enabled") {}

    Parameter decay   { "shimmer.decay",   "Decay",   1.0f, 15.0f,  6.0f,  0.01f, " s", true };
    Parameter shimmer { "shimmer.amount",  "Shimmer", 0.0f, 100.0f, 50.0f, 1.0f,  " %" };
    Parameter tone    { "shimmer.tone",    "Tone",    0.0f, 10.0f,  6.0f };
    Parameter mix     { "shimmer.mix",     "Mix",     0.0f, 100.0f, 35.0f, 1.0f, " %" };

    std::vector<Parameter*> getControls() override   { return { &decay, &shimmer, &tone, &mix }; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* left, float* right, int numSamples) noexcept override;

private:
    double sampleRate = 48000.0;
    FeedbackDelayNetwork network;
    pedal_dsp::GrainPitchShifter octaveUp;
    pedal_dsp::OnePole shimmerLowCut;
    float lowCutCoefficient = 0.0f, lastWet = 0.0f;
    juce::SmoothedValue<float> wet { 0.35f }, shimmerGain { 0.5f };
};

/** The boingy, splashy spring tank built into many guitar amps. Dwell is
    how hard the springs are hit; turned up, every pick attack "drips". */
class SpringReverb final : public StereoEffect
{
public:
    SpringReverb() : StereoEffect ("Spring Reverb", "spring.enabled") {}

    Parameter dwell { "spring.dwell", "Dwell", 0.0f, 10.0f,  5.0f };
    Parameter decay { "spring.decay", "Decay", 0.0f, 10.0f,  5.0f };
    Parameter tone  { "spring.tone",  "Tone",  0.0f, 10.0f,  5.0f };
    Parameter mix   { "spring.mix",   "Mix",   0.0f, 100.0f, 30.0f, 1.0f, " %" };

    std::vector<Parameter*> getControls() override   { return { &dwell, &decay, &tone, &mix }; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* left, float* right, int numSamples) noexcept override;

private:
    static constexpr int numStages = 14;

    struct Spring
    {
        FractionalDelay line;
        std::array<pedal_dsp::FirstOrderAllPass, numStages> chirp;
        pedal_dsp::OnePole loopDamping, outLow, outHigh;
        float lengthSamples = 1000.0f;
        float last = 0.0f;

        void reset();
    };

    double sampleRate = 48000.0;
    std::array<Spring, 2> springs;
    float loopCoefficient = 0.0f, outHighCoefficient = 0.0f;
    juce::SmoothedValue<float> wet { 0.3f };
};
