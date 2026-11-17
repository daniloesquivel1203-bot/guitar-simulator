#pragma once

#include "DspHelpers.h"
#include "Effect.h"

#include <juce_dsp/juce_dsp.h>

#include <array>

/** A resonant filter that opens as you pick harder and closes as the note
    fades: the funky "quack" of an auto-wah. */
class EnvelopeFilter final : public MonoEffect
{
public:
    EnvelopeFilter() : MonoEffect ("Envelope Filter", "envfilter.enabled") {}

    Parameter sensitivity { "envfilter.sensitivity", "Sensitivity", 0.0f, 10.0f, 6.0f };
    Parameter range       { "envfilter.range",       "Range",       0.0f, 10.0f, 6.0f };
    Parameter resonance   { "envfilter.resonance",   "Resonance",   0.0f, 10.0f, 5.0f };
    Parameter mix         { "envfilter.mix",         "Mix",         0.0f, 100.0f, 100.0f, 1.0f, " %" };
    Parameter mode        { "envfilter.mode",        "Filter",      { "Low-pass", "Band-pass", "High-pass" } };
    Parameter direction   { "envfilter.direction",   "Sweep",       { "Up", "Down" } };

    std::vector<Parameter*> getControls() override
    {
        return { &sensitivity, &range, &resonance, &mix, &mode, &direction };
    }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* samples, int numSamples) noexcept override;

private:
    double rate = 48000.0;
    float envelope = 0.0f, attack = 0.0f, release = 0.0f;
    pedal_dsp::StateVariableFilter filter;
    juce::SmoothedValue<float> wet { 1.0f };
};

/** A wah pedal. Position is where the rocker sits, from heel down (dark) to
    toe down (bright). Leave it in one place for a "cocked" wah, or let the
    automatic mode rock it for you. */
class Wah final : public MonoEffect
{
public:
    Wah() : MonoEffect ("Wah", "wah.enabled") {}

    Parameter position  { "wah.position",  "Position",  0.0f, 10.0f, 5.0f, 0.01f };
    Parameter resonance { "wah.resonance", "Resonance", 0.0f, 10.0f, 5.0f };
    Parameter rate      { "wah.rate",      "Auto rate", 0.1f, 8.0f,  1.5f, 0.01f, " Hz", true };
    Parameter mode      { "wah.mode",      "Mode",      { "Manual", "Auto" } };

    std::vector<Parameter*> getControls() override   { return { &position, &resonance, &rate, &mode }; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* samples, int numSamples) noexcept override;

private:
    double sampleRate = 48000.0;
    pedal_dsp::StateVariableFilter filter;
    pedal_dsp::Lfo lfo;
    juce::SmoothedValue<float> smoothedPosition { 0.5f };
};

/** Makes the guitar "talk" by imitating the resonances of a mouth shaping
    vowels. The LFO sweeps between nearby vowels. */
class VowelFilter final : public MonoEffect
{
public:
    VowelFilter() : MonoEffect ("Vowel Filter", "vowel.enabled") {}

    Parameter vowel     { "vowel.vowel",     "Vowel",     0.0f,  4.0f,   1.0f,  0.01f };
    Parameter rate      { "vowel.rate",      "Rate",      0.05f, 5.0f,   0.5f,  0.01f, " Hz", true };
    Parameter depth     { "vowel.depth",     "Depth",     0.0f,  10.0f,  5.0f };
    Parameter resonance { "vowel.resonance", "Resonance", 0.0f,  10.0f,  5.0f };
    Parameter mix       { "vowel.mix",       "Mix",       0.0f,  100.0f, 100.0f, 1.0f, " %" };

    std::vector<Parameter*> getControls() override   { return { &vowel, &rate, &depth, &resonance, &mix }; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* samples, int numSamples) noexcept override;

private:
    double sampleRate = 48000.0;
    std::array<pedal_dsp::StateVariableFilter, 3> formants;
    pedal_dsp::Lfo lfo;
    juce::SmoothedValue<float> wet { 1.0f }, smoothedVowel { 1.0f };
};

/** Seven sliders' worth of EQ, an octave apart, plus an output level. Good
    for shaping a lead boost or fixing a muddy or harsh tone. */
class GraphicEq final : public MonoEffect
{
public:
    GraphicEq() : MonoEffect ("Graphic EQ", "geq.enabled") {}

    std::array<Parameter, 7> bands {{
        { "geq.100",  "100",  -15.0f, 15.0f, 0.0f, 0.1f, " dB" },
        { "geq.200",  "200",  -15.0f, 15.0f, 0.0f, 0.1f, " dB" },
        { "geq.400",  "400",  -15.0f, 15.0f, 0.0f, 0.1f, " dB" },
        { "geq.800",  "800",  -15.0f, 15.0f, 0.0f, 0.1f, " dB" },
        { "geq.1600", "1.6k", -15.0f, 15.0f, 0.0f, 0.1f, " dB" },
        { "geq.3200", "3.2k", -15.0f, 15.0f, 0.0f, 0.1f, " dB" },
        { "geq.6400", "6.4k", -15.0f, 15.0f, 0.0f, 0.1f, " dB" },
    }};

    Parameter level { "geq.level", "Level", -15.0f, 15.0f, 0.0f, 0.1f, " dB" };

    std::vector<Parameter*> getControls() override;

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* samples, int numSamples) noexcept override;

private:
    using Filter = juce::dsp::IIR::Filter<float>;
    using Coefficients = juce::dsp::IIR::Coefficients<float>;

    void update() noexcept;

    double rate = 48000.0;
    std::array<Filter, 7> filters;
    std::array<float, 7> lastGains {};
    juce::SmoothedValue<float> output { 1.0f };
};
