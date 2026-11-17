#pragma once

#include "Parameter.h"

#include <juce_dsp/juce_dsp.h>

#include <array>

/** A guitar amplifier model with twelve voicings.

    Signal flow: input shaping, then one to three tube-style gain stages run at
    eight times the sample rate to keep distortion free of aliasing, then the
    bass, mid and treble tone controls, a power amp stage, presence and master
    volume. Each voicing sets the gain structure, the filtering around the
    gain stages, where the tone controls act, and how hard the power amp is
    pushed. The voicings describe styles of amp, not specific products.

    Mono in, mono out, processed in place.
*/
class AmpModel
{
public:
    // New voicings go at the end so saved presets keep their sound.
    enum Voicing
    {
        clean = 0, crunch, lead,
        brightClean, jazzClean, chime, tweed, britishVintage, hotRodBritish,
        smoothOverdrive, modernScooped, tightModern,
        numVoicings
    };

    static juce::StringArray getVoicingNames()
    {
        return { "Clean", "Crunch", "Lead",
                 "Bright Clean", "Jazz Clean", "Chime", "Tweed", "British Vintage", "Hot-Rod British",
                 "Smooth Overdrive", "Modern Scooped", "Tight Modern" };
    }

    Parameter enabled  { "amp.enabled",  "On",       0.0f, 1.0f,  1.0f, 1.0f };
    Parameter voicing  { "amp.voicing",  "Voicing",  0.0f, (float) (numVoicings - 1), 0.0f, 1.0f };
    Parameter gain     { "amp.gain",     "Gain",     0.0f, 10.0f, 4.0f };
    Parameter bass     { "amp.bass",     "Bass",     0.0f, 10.0f, 5.0f };
    Parameter mid      { "amp.mid",      "Mid",      0.0f, 10.0f, 5.0f };
    Parameter treble   { "amp.treble",   "Treble",   0.0f, 10.0f, 5.0f };
    Parameter presence { "amp.presence", "Presence", 0.0f, 10.0f, 5.0f };
    Parameter master   { "amp.master",   "Master",   0.0f, 10.0f, 5.0f };

    std::array<Parameter*, 8> getParameters() noexcept
    {
        return { &enabled, &voicing, &gain, &bass, &mid, &treble, &presence, &master };
    }

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** numSamples must not exceed the block size given to prepare(). */
    void process (float* samples, int numSamples) noexcept;

private:
    using Filter = juce::dsp::IIR::Filter<float>;
    using Coefficients = juce::dsp::IIR::Coefficients<float>;

    static constexpr int maxStages = 3;
    static constexpr size_t oversamplingStages = 3;   // 3 stages of 2x = 8x

    void updateVoicing (int newVoicing);
    void updateTone();

    double baseRate = 48000.0;
    double oversampledRate = 192000.0;

    juce::dsp::Oversampling<float> oversampling { 1, oversamplingStages,
                                                  juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                                                  true, false };

    Filter inputHighPass, inputVoice;
    std::array<Filter, maxStages> stageHighPass, stageLowPass;
    Filter bassFilter, midFilter, trebleFilter, presenceFilter, outputDcBlock;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> stageGain { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> masterGain { 0.0f };

    int currentVoicing = -1;
    float lastBass = -1.0f, lastMid = -1.0f, lastTreble = -1.0f, lastPresence = -1.0f;
    bool wasEnabled = true;
};
