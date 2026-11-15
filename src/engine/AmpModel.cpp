#include "AmpModel.h"

#include <cmath>

namespace
{
struct VoicingSpec
{
    int numStages;           // tube-style gain stages in series
    float minGainDb;         // total drive with the gain knob at 0
    float maxGainDb;         // total drive with the gain knob at 10
    float inputHighPassHz;   // bass removed before distortion, keeps high gain tight
    float voiceHz;           // pre-distortion mid emphasis
    float voiceDb;
    float stageHighPassHz;   // between stages
    float stageLowPassHz;
    float bias;              // asymmetry, which adds even harmonics
    float level;             // makeup so the voicings are similar in loudness
};

constexpr VoicingSpec voicings[AmpModel::numVoicings] =
{
    // stages  min    max    inHP    voice          stHP    stLP     bias   level
    { 1,       -6.0f, 24.0f, 60.0f,  1200.0f, 0.0f, 20.0f,  12000.0f, 0.10f, 0.80f },   // clean
    { 2,       6.0f,  42.0f, 90.0f,  800.0f,  4.0f, 110.0f, 7500.0f,  0.15f, 0.55f },   // crunch
    { 3,       14.0f, 60.0f, 130.0f, 900.0f,  6.0f, 170.0f, 6500.0f,  0.12f, 0.45f },   // lead
};

constexpr float toneRangeDb = 12.0f;
constexpr float presenceRangeDb = 8.0f;
constexpr float powerAmpDrive = 1.4f;
constexpr float powerAmpBias = 0.05f;
constexpr double gainSmoothingSeconds = 0.03;

/** A smooth, slightly asymmetric saturation curve. Silence in gives silence out. */
inline float tubeStage (float x, float bias, float biasOffset) noexcept
{
    return std::tanh (x + bias) - biasOffset;
}

inline float knobToDb (float knob, float rangeDb) noexcept
{
    return (knob - 5.0f) / 5.0f * rangeDb;
}

inline float masterToGain (float knob) noexcept
{
    // 7 is unity, 10 is +12 dB, and 0 is silent.
    return knob <= 0.0f ? 0.0f : juce::Decibels::decibelsToGain ((knob - 7.0f) * 4.0f);
}
}

void AmpModel::prepare (double sampleRate, int maxBlockSize)
{
    baseRate = sampleRate;
    oversampledRate = sampleRate * (double) (1 << oversamplingStages);

    oversampling.initProcessing ((size_t) juce::jmax (1, maxBlockSize));

    stageGain.reset (oversampledRate, gainSmoothingSeconds);
    masterGain.reset (baseRate, gainSmoothingSeconds);

    outputDcBlock.coefficients = Coefficients::makeHighPass (baseRate, 20.0f);

    currentVoicing = -1;
    lastBass = lastMid = lastTreble = lastPresence = -1.0f;
    updateVoicing (voicing.getIndex());
    updateTone();
    reset();
}

void AmpModel::reset()
{
    oversampling.reset();
    inputHighPass.reset();
    inputVoice.reset();

    for (auto& filter : stageHighPass)   filter.reset();
    for (auto& filter : stageLowPass)    filter.reset();

    bassFilter.reset();
    midFilter.reset();
    trebleFilter.reset();
    presenceFilter.reset();
    outputDcBlock.reset();

    const auto& spec = voicings[juce::jlimit (0, numVoicings - 1, currentVoicing)];
    const auto totalDb = juce::jmap (gain.get() / 10.0f, spec.minGainDb, spec.maxGainDb);
    stageGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (totalDb / (float) spec.numStages));
    masterGain.setCurrentAndTargetValue (masterToGain (master.get()));
}

void AmpModel::updateVoicing (int newVoicing)
{
    newVoicing = juce::jlimit (0, numVoicings - 1, newVoicing);

    if (newVoicing == currentVoicing)
        return;

    currentVoicing = newVoicing;
    const auto& spec = voicings[currentVoicing];

    inputHighPass.coefficients = Coefficients::makeHighPass (baseRate, spec.inputHighPassHz);
    inputVoice.coefficients = Coefficients::makePeakFilter (baseRate, spec.voiceHz, 0.7f,
                                                            juce::Decibels::decibelsToGain (spec.voiceDb));

    const auto lowPassHz = juce::jmin (spec.stageLowPassHz, (float) (baseRate * 0.45));

    for (int stage = 0; stage < maxStages; ++stage)
    {
        stageHighPass[(size_t) stage].coefficients = Coefficients::makeFirstOrderHighPass (oversampledRate, spec.stageHighPassHz);
        stageLowPass[(size_t) stage].coefficients = Coefficients::makeFirstOrderLowPass (oversampledRate, lowPassHz);
        stageHighPass[(size_t) stage].reset();
        stageLowPass[(size_t) stage].reset();
    }
}

void AmpModel::updateTone()
{
    const auto newBass = bass.get(), newMid = mid.get(), newTreble = treble.get(), newPresence = presence.get();

    if (! juce::exactlyEqual (newBass, lastBass))
    {
        lastBass = newBass;
        bassFilter.coefficients = Coefficients::makeLowShelf (baseRate, 120.0f, 0.7f,
                                                              juce::Decibels::decibelsToGain (knobToDb (newBass, toneRangeDb)));
    }

    if (! juce::exactlyEqual (newMid, lastMid))
    {
        lastMid = newMid;
        midFilter.coefficients = Coefficients::makePeakFilter (baseRate, 650.0f, 0.7f,
                                                               juce::Decibels::decibelsToGain (knobToDb (newMid, toneRangeDb)));
    }

    if (! juce::exactlyEqual (newTreble, lastTreble))
    {
        lastTreble = newTreble;
        trebleFilter.coefficients = Coefficients::makeHighShelf (baseRate, 3200.0f, 0.7f,
                                                                 juce::Decibels::decibelsToGain (knobToDb (newTreble, toneRangeDb)));
    }

    if (! juce::exactlyEqual (newPresence, lastPresence))
    {
        lastPresence = newPresence;
        presenceFilter.coefficients = Coefficients::makeHighShelf (baseRate, juce::jmin (5000.0f, (float) (baseRate * 0.4)), 0.7f,
                                                                   juce::Decibels::decibelsToGain (knobToDb (newPresence, presenceRangeDb)));
    }
}

void AmpModel::process (float* samples, int numSamples) noexcept
{
    const bool isEnabled = enabled.isOn();

    if (! isEnabled)
    {
        wasEnabled = false;
        return;
    }

    if (! wasEnabled)
    {
        // Start from a clean state so stale filter contents do not pop.
        wasEnabled = true;
        reset();
    }

    updateVoicing (voicing.getIndex());
    updateTone();

    const auto& spec = voicings[currentVoicing];
    const auto totalDb = juce::jmap (gain.get() / 10.0f, spec.minGainDb, spec.maxGainDb);
    stageGain.setTargetValue (juce::Decibels::decibelsToGain (totalDb / (float) spec.numStages));
    masterGain.setTargetValue (masterToGain (master.get()));

    // Shape the signal before distortion.
    for (int i = 0; i < numSamples; ++i)
        samples[i] = inputVoice.processSample (inputHighPass.processSample (samples[i]));

    // Run the gain stages at eight times the sample rate.
    float* channel[] = { samples };
    juce::dsp::AudioBlock<float> block (channel, 1, (size_t) numSamples);
    auto oversampled = oversampling.processSamplesUp (block);

    auto* data = oversampled.getChannelPointer (0);
    const auto numOversampled = (int) oversampled.getNumSamples();
    const auto biasOffset = std::tanh (spec.bias);

    for (int i = 0; i < numOversampled; ++i)
    {
        const auto drive = stageGain.getNextValue();
        auto x = data[i];

        for (int stage = 0; stage < spec.numStages; ++stage)
        {
            x = tubeStage (x * drive, spec.bias, biasOffset);
            x = stageHighPass[(size_t) stage].processSample (x);
            x = stageLowPass[(size_t) stage].processSample (x);
        }

        data[i] = x * spec.level;
    }

    oversampling.processSamplesDown (block);

    // Tone controls, power amp, presence and master.
    const auto powerOffset = std::tanh (powerAmpBias);
    const auto powerMakeup = 1.0f / powerAmpDrive;

    for (int i = 0; i < numSamples; ++i)
    {
        auto x = samples[i];
        x = trebleFilter.processSample (midFilter.processSample (bassFilter.processSample (x)));
        x = tubeStage (x * powerAmpDrive, powerAmpBias, powerOffset) * powerMakeup;
        x = outputDcBlock.processSample (presenceFilter.processSample (x));
        samples[i] = x * masterGain.getNextValue();
    }
}
