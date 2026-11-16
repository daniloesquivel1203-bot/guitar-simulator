#pragma once

#include "AmpModel.h"
#include "Cabinet.h"
#include "SafetyLimiter.h"
#include "effects/Drives.h"
#include "effects/Dynamics.h"
#include "effects/Equaliser.h"
#include "effects/TimeEffects.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <vector>

/** The audio path from one mono input to a stereo output.

    This class has no knowledge of audio devices, so it can be tested offline.
    The order is: input trim, noise gate, compressor, overdrive, distortion,
    amp, cabinet, EQ, then in stereo chorus, delay, reverb, output level and
    safety limiter.

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

    AmpModel& getAmp() noexcept        { return amp; }
    Cabinet& getCabinet() noexcept     { return cabinet; }

    NoiseGate& getNoiseGate() noexcept     { return noiseGate; }
    Compressor& getCompressor() noexcept   { return compressor; }
    Overdrive& getOverdrive() noexcept     { return overdrive; }
    Distortion& getDistortion() noexcept   { return distortion; }
    Equaliser& getEqualiser() noexcept     { return equaliser; }
    Chorus& getChorus() noexcept           { return chorus; }
    Delay& getDelay() noexcept             { return delay; }
    Reverb& getReverb() noexcept           { return reverb; }

    /** The pedals before the amp and after the cabinet, in signal order. */
    std::vector<MonoEffect*> getPreEffects()       { return { &noiseGate, &compressor, &overdrive, &distortion }; }
    std::vector<MonoEffect*> getPostMonoEffects()  { return { &equaliser }; }
    std::vector<StereoEffect*> getStereoEffects()  { return { &chorus, &delay, &reverb }; }

    /** Every parameter in the chain, for saving and restoring. */
    std::vector<Parameter*> getAllParameters();

    void setInputGainDb (float db) noexcept;
    void setOutputGainDb (float db) noexcept;
    void setMuted (bool shouldBeMuted) noexcept   { muted.store (shouldBeMuted); }

    /** A second, separate mute used by the tuner, so closing the tuner does not
        undo a mute the user set themselves. */
    void setTunerMuted (bool shouldBeMuted) noexcept   { tunerMuted.store (shouldBeMuted); }

    float getInputGainDb() const noexcept    { return inputGainDb.load(); }
    float getOutputGainDb() const noexcept   { return outputGainDb.load(); }
    bool isMuted() const noexcept            { return muted.load(); }

    /** Each reader returns the highest value seen since it was last called. */
    float readInputPeak() noexcept    { return inputPeak.exchange (0.0f); }
    float readOutputPeak() noexcept   { return outputPeak.exchange (0.0f); }
    bool readInputClipped() noexcept  { return inputClipped.exchange (false); }

private:
    void processChunk (const float* input, float* left, float* right, int numSamples) noexcept;

    NoiseGate noiseGate;
    Compressor compressor;
    Overdrive overdrive;
    Distortion distortion;
    AmpModel amp;
    Cabinet cabinet;
    Equaliser equaliser;
    Chorus chorus;
    Delay delay;
    Reverb reverb;
    SafetyLimiter limiter;

    std::vector<float> scratch = std::vector<float> (512);
    std::vector<float> scratchRight = std::vector<float> (512);

    std::atomic<float> inputGainDb { 0.0f };
    std::atomic<float> outputGainDb { 0.0f };
    std::atomic<bool> muted { false };
    std::atomic<bool> tunerMuted { false };

    std::atomic<float> inputPeak { 0.0f };
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<bool> inputClipped { false };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> inputGain { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputGain { 1.0f };
};
