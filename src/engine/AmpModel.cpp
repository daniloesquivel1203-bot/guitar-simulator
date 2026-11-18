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

    // The tone stack. Real amps place their bass, mid and treble controls at
    // different frequencies, and many have a built-in mid scoop or push.
    float bassHz;
    float midHz;
    float midDb;             // fixed mid offset with the mid knob at 5
    float trebleHz;
    float powerDrive;        // how hard the power amp is pushed: more sag and squash
};

constexpr VoicingSpec voicings[AmpModel::numVoicings] =
{
    // stages  min     max    inHP    voice          stHP    stLP      bias   level    bass    mid     midDb  treble   power
    { 1,       -6.0f,  24.0f, 60.0f,  1200.0f, 0.0f, 20.0f,  12000.0f, 0.10f, 0.80f,   120.0f, 650.0f,  0.0f, 3200.0f, 1.4f },   // clean
    { 2,       6.0f,   42.0f, 90.0f,  800.0f,  4.0f, 110.0f, 7500.0f,  0.15f, 0.55f,   120.0f, 650.0f,  0.0f, 3200.0f, 1.4f },   // crunch
    { 3,       14.0f,  60.0f, 130.0f, 900.0f,  6.0f, 170.0f, 6500.0f,  0.12f, 0.45f,   120.0f, 650.0f,  0.0f, 3200.0f, 1.4f },   // lead

    // American clean: big headroom, bright cap sparkle, scooped mids.
    { 1,       -10.0f, 18.0f, 70.0f,  2500.0f, 3.0f, 20.0f,  14000.0f, 0.06f, 1.60f,   100.0f, 450.0f, -4.0f, 4000.0f, 1.1f },   // bright clean
    // Dark and round, almost no breakup.
    { 1,       -12.0f, 14.0f, 50.0f,  400.0f,  1.5f, 20.0f,  5000.0f,  0.05f, 1.70f,   140.0f, 700.0f,  0.0f, 2500.0f, 1.0f },   // jazz clean
    // Small British class A combo: jangly upper mids, strong even harmonics, early power amp breakup.
    { 2,       0.0f,   34.0f, 80.0f,  2200.0f, 4.0f, 60.0f,  8000.0f,  0.25f, 0.65f,   100.0f, 1000.0f, 1.0f, 3000.0f, 1.6f },   // chime
    // Small vintage combo: loose bass, fat mids, saggy and compressed.
    { 2,       2.0f,   36.0f, 50.0f,  700.0f,  3.0f, 40.0f,  5500.0f,  0.20f, 0.60f,   140.0f, 800.0f,  3.0f, 2600.0f, 2.0f },   // tweed
    // 1960s high power British head: raw upper-mid crunch.
    { 2,       4.0f,   40.0f, 90.0f,  1600.0f, 5.0f, 90.0f,  8000.0f,  0.15f, 0.55f,   110.0f, 900.0f,  2.0f, 3200.0f, 1.8f },   // british vintage
    // Modded 1980s British: an extra stage and a mid push for rock leads.
    { 3,       10.0f,  52.0f, 110.0f, 1100.0f, 6.0f, 140.0f, 7000.0f,  0.12f, 0.48f,   110.0f, 800.0f,  3.0f, 3200.0f, 1.6f },   // hot-rod british
    // Boutique singing lead: rounded top, vocal mids, long sustain.
    { 3,       8.0f,   50.0f, 100.0f, 1000.0f, 5.0f, 120.0f, 5000.0f,  0.18f, 0.50f,   120.0f, 1000.0f, 2.0f, 2800.0f, 1.6f },   // smooth overdrive
    // American high gain: tight lows, scooped mids for metal.
    { 3,       16.0f,  62.0f, 120.0f, 900.0f,  3.0f, 180.0f, 7000.0f,  0.10f, 0.60f,   90.0f,  500.0f, -5.0f, 3500.0f, 1.3f },   // modern scooped
    // Very tight bass and biting upper mids for drop tunings.
    { 3,       18.0f,  64.0f, 180.0f, 1500.0f, 6.0f, 250.0f, 7500.0f,  0.10f, 0.62f,   90.0f,  700.0f, -1.0f, 3800.0f, 1.2f },   // tight modern
};

constexpr float toneRangeDb = 12.0f;
constexpr float presenceRangeDb = 8.0f;
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

    // The tone stack frequencies belong to the voicing.
    lastBass = lastMid = lastTreble = -1.0f;
}

void AmpModel::updateTone()
{
    const auto& spec = voicings[juce::jlimit (0, numVoicings - 1, currentVoicing)];
    const auto newBass = bass.get(), newMid = mid.get(), newTreble = treble.get(), newPresence = presence.get();

    if (! juce::exactlyEqual (newBass, lastBass))
    {
        lastBass = newBass;
        bassFilter.coefficients = Coefficients::makeLowShelf (baseRate, spec.bassHz, 0.7f,
                                                              juce::Decibels::decibelsToGain (knobToDb (newBass, toneRangeDb)));
    }

    if (! juce::exactlyEqual (newMid, lastMid))
    {
        lastMid = newMid;
        midFilter.coefficients = Coefficients::makePeakFilter (baseRate, spec.midHz, 0.7f,
                                                               juce::Decibels::decibelsToGain (knobToDb (newMid, toneRangeDb) + spec.midDb));
    }

    if (! juce::exactlyEqual (newTreble, lastTreble))
    {
        lastTreble = newTreble;
        trebleFilter.coefficients = Coefficients::makeHighShelf (baseRate, juce::jmin (spec.trebleHz, (float) (baseRate * 0.4)), 0.7f,
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
    const auto powerDrive = spec.powerDrive;
    const auto powerMakeup = 1.0f / powerDrive;

    for (int i = 0; i < numSamples; ++i)
    {
        auto x = samples[i];
        x = trebleFilter.processSample (midFilter.processSample (bassFilter.processSample (x)));
        x = tubeStage (x * powerDrive, powerAmpBias, powerOffset) * powerMakeup;
        x = outputDcBlock.processSample (presenceFilter.processSample (x));
        samples[i] = x * masterGain.getNextValue();
    }
}
