#pragma once

#include "Effect.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

/** A short delay line that can be read at a fractional, moving position. */
class FractionalDelay
{
public:
    void setMaxSamples (int maxSamples)
    {
        buffer.assign ((size_t) juce::nextPowerOfTwo (maxSamples + 4), 0.0f);
        mask = (int) buffer.size() - 1;
        clear();
    }

    void clear() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
    }

    void write (float sample) noexcept
    {
        buffer[(size_t) writeIndex] = sample;
        writeIndex = (writeIndex + 1) & mask;
    }

    /** Reads the sample written `delaySamples` writes ago, before the next write(). */
    float read (float delaySamples) const noexcept
    {
        const auto position = (float) writeIndex - delaySamples;
        const auto whole = (int) std::floor (position);
        const auto fraction = position - (float) whole;
        const auto a = buffer[(size_t) (whole & mask)];
        const auto b = buffer[(size_t) ((whole + 1) & mask)];
        return a + (b - a) * fraction;
    }

private:
    std::vector<float> buffer = std::vector<float> (4);
    int mask = 3;
    int writeIndex = 0;
};

/** Thickens the sound by mixing in copies whose pitch drifts slowly up and
    down. The left and right copies drift out of step, which makes it wide. */
class Chorus final : public StereoEffect
{
public:
    Chorus() : StereoEffect ("Chorus", "chorus.enabled") {}

    Parameter rate  { "chorus.rate",  "Rate",  0.1f, 5.0f,   0.8f,  0.01f, " Hz", true };
    Parameter depth { "chorus.depth", "Depth", 0.0f, 10.0f,  5.0f };
    Parameter mix   { "chorus.mix",   "Mix",   0.0f, 100.0f, 50.0f, 1.0f, " %" };

    std::vector<Parameter*> getControls() override   { return { &rate, &depth, &mix }; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* left, float* right, int numSamples) noexcept override;

private:
    static constexpr float centreMs = 8.0f;
    static constexpr float maxDepthMs = 4.0f;

    double sampleRate = 48000.0;
    FractionalDelay delayLeft, delayRight;
    double phase = 0.0;
    juce::SmoothedValue<float> wet { 0.0f }, depthMs { 0.0f };
};

/** Repeats what you play as echoes. In ping-pong mode the echoes bounce
    between the left and right ears. */
class Delay final : public StereoEffect
{
public:
    Delay() : StereoEffect ("Delay", "delay.enabled") {}

    Parameter time     { "delay.time",     "Time",      20.0f, 1500.0f, 350.0f, 1.0f, " ms", true };
    Parameter feedback { "delay.feedback", "Feedback",  0.0f,  90.0f,   35.0f,  1.0f, " %" };
    Parameter mix      { "delay.mix",      "Mix",       0.0f,  100.0f,  30.0f,  1.0f, " %" };
    Parameter pingPong { "delay.pingPong", "Ping-pong", 0.0f,  1.0f,    0.0f,   1.0f };

    std::vector<Parameter*> getControls() override   { return { &time, &feedback, &mix, &pingPong }; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* left, float* right, int numSamples) noexcept override;

private:
    double sampleRate = 48000.0;
    FractionalDelay lineA, lineB;
    float dampA = 0.0f, dampB = 0.0f, dampCoefficient = 0.5f;
    juce::SmoothedValue<float> delaySamples { 1000.0f }, wet { 0.0f }, feedbackGain { 0.0f };
};

/** Adds the sound of a room around the guitar. */
class Reverb final : public StereoEffect
{
public:
    Reverb() : StereoEffect ("Reverb", "reverb.enabled") {}

    Parameter size    { "reverb.size",    "Size",    0.0f, 10.0f,  5.0f };
    Parameter damping { "reverb.damping", "Damping", 0.0f, 10.0f,  5.0f };
    Parameter mix     { "reverb.mix",     "Mix",     0.0f, 100.0f, 25.0f, 1.0f, " %" };

    std::vector<Parameter*> getControls() override   { return { &size, &damping, &mix }; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* left, float* right, int numSamples) noexcept override;

private:
    void update();

    juce::Reverb reverb;
    float lastSize = -1.0f, lastDamping = -1.0f, lastMix = -1.0f;
};
