#pragma once

#include "Effect.h"

#include <juce_dsp/juce_dsp.h>

/** A three-band tone control placed after the cabinet for final shaping. */
class Equaliser final : public MonoEffect
{
public:
    Equaliser() : MonoEffect ("EQ", "eq.enabled") {}

    Parameter low     { "eq.low",     "Low",      -12.0f, 12.0f,   0.0f,   0.1f,  " dB" };
    Parameter mid     { "eq.mid",     "Mid",      -12.0f, 12.0f,   0.0f,   0.1f,  " dB" };
    Parameter midFreq { "eq.midFreq", "Mid freq", 200.0f, 5000.0f, 800.0f, 10.0f, " Hz", true };
    Parameter high    { "eq.high",    "High",     -12.0f, 12.0f,   0.0f,   0.1f,  " dB" };

    std::vector<Parameter*> getControls() override   { return { &low, &mid, &midFreq, &high }; }

    void prepare (double sampleRate, int) override
    {
        rate = sampleRate;
        lastLow = lastMid = lastMidFreq = lastHigh = -1000.0f;
        update();
        reset();
    }

    void reset() override
    {
        lowFilter.reset();
        midFilter.reset();
        highFilter.reset();
    }

    void process (float* samples, int numSamples) noexcept override
    {
        if (! shouldProcess())
            return;

        update();

        for (int i = 0; i < numSamples; ++i)
            samples[i] = highFilter.processSample (midFilter.processSample (lowFilter.processSample (samples[i])));
    }

private:
    using Filter = juce::dsp::IIR::Filter<float>;
    using Coefficients = juce::dsp::IIR::Coefficients<float>;

    void update()
    {
        const auto newLow = low.get(), newMid = mid.get(), newMidFreq = midFreq.get(), newHigh = high.get();

        if (! juce::exactlyEqual (newLow, lastLow))
        {
            lastLow = newLow;
            lowFilter.coefficients = Coefficients::makeLowShelf (rate, 120.0f, 0.7f, juce::Decibels::decibelsToGain (newLow));
        }

        if (! juce::exactlyEqual (newMid, lastMid) || ! juce::exactlyEqual (newMidFreq, lastMidFreq))
        {
            lastMid = newMid;
            lastMidFreq = newMidFreq;
            midFilter.coefficients = Coefficients::makePeakFilter (rate, newMidFreq, 0.9f, juce::Decibels::decibelsToGain (newMid));
        }

        if (! juce::exactlyEqual (newHigh, lastHigh))
        {
            lastHigh = newHigh;
            highFilter.coefficients = Coefficients::makeHighShelf (rate, juce::jmin (4000.0f, (float) (rate * 0.4)), 0.7f,
                                                                   juce::Decibels::decibelsToGain (newHigh));
        }
    }

    Filter lowFilter, midFilter, highFilter;
    double rate = 48000.0;
    float lastLow = -1000.0f, lastMid = -1000.0f, lastMidFreq = -1000.0f, lastHigh = -1000.0f;
};
