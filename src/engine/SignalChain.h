#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>

/** The audio path from one mono input to a stereo output.

    This class has no knowledge of audio devices, so it can be tested offline.
    For now it is a clean passthrough with an input trim, an output level and
    level metering. Pedals, the amp and the cabinet are inserted between the
    two gain stages in later milestones.

    process() runs on the audio thread and never allocates or locks. The
    setters and meter readers may be called from any thread.
*/
class SignalChain
{
public:
    static constexpr float minInputGainDb  = -24.0f;
    static constexpr float maxInputGainDb  =  24.0f;
    static constexpr float minOutputGainDb = -60.0f;
    static constexpr float maxOutputGainDb =   6.0f;

    /** A raw input sample at or above this magnitude counts as clipping. */
    static constexpr float clipThreshold = 0.999f;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Processes one block. `input` may be null, which is treated as silence.
        `left` and `right` may point to the same buffer. */
    void process (const float* input, float* left, float* right, int numSamples) noexcept;

    void setInputGainDb (float db) noexcept;
    void setOutputGainDb (float db) noexcept;
    void setMuted (bool shouldBeMuted) noexcept   { muted.store (shouldBeMuted); }

    float getInputGainDb() const noexcept    { return inputGainDb.load(); }
    float getOutputGainDb() const noexcept   { return outputGainDb.load(); }
    bool isMuted() const noexcept            { return muted.load(); }

    /** Each reader returns the highest value seen since it was last called. */
    float readInputPeak() noexcept    { return inputPeak.exchange (0.0f); }
    float readOutputPeak() noexcept   { return outputPeak.exchange (0.0f); }
    bool readInputClipped() noexcept  { return inputClipped.exchange (false); }

private:
    std::atomic<float> inputGainDb { 0.0f };
    std::atomic<float> outputGainDb { 0.0f };
    std::atomic<bool> muted { false };

    std::atomic<float> inputPeak { 0.0f };
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<bool> inputClipped { false };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> inputGain { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputGain { 1.0f };
};
