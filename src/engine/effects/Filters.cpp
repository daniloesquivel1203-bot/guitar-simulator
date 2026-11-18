#include "Filters.h"

#include <cmath>

using namespace pedal_dsp;

//==============================================================================
void EnvelopeFilter::prepare (double sampleRate, int)
{
    rate = sampleRate;
    attack = timeCoefficient (rate, 3.0);
    release = timeCoefficient (rate, 120.0);
    wet.reset (rate, 0.03);
    reset();
}

void EnvelopeFilter::reset()
{
    envelope = 0.0f;
    filter.reset();
    wet.setCurrentAndTargetValue (mix.get() * 0.01f);
}

void EnvelopeFilter::process (float* samples, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    wet.setTargetValue (mix.get() * 0.01f);

    const auto sensitivityGain = 4.0f * juce::Decibels::decibelsToGain (sensitivity.get() * 4.0f);
    const auto octaves = 1.0f + range.get() * 0.5f;
    const auto q = 0.7f + resonance.get() * 1.0f;
    const auto filterMode = mode.getIndex();
    const bool down = direction.getIndex() == 1;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto x = samples[i];
        const auto level = std::abs (x);
        envelope += (level > envelope ? attack : release) * (level - envelope);

        const auto opening = juce::jmin (1.0f, envelope * sensitivityGain);
        const auto cutoff = 200.0f * std::exp2 (octaves * (down ? 1.0f - opening : opening));
        const auto out = filter.process (x, cutoff, q, rate);

        // The low-pass's resonant peak sits on the loudest harmonics, so it
        // is turned down to keep the pedal about as loud as the dry sound.
        float filtered = 0.6f * out.lowPass;

        if (filterMode == 1)        filtered = 2.0f * out.bandPass / q;
        else if (filterMode == 2)   filtered = out.highPass;

        const auto amount = wet.getNextValue();
        samples[i] = x + amount * (filtered - x);
    }
}

//==============================================================================
void Wah::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    smoothedPosition.reset (sampleRate, 0.02);
    reset();
}

void Wah::reset()
{
    filter.reset();
    lfo.reset();
    smoothedPosition.setCurrentAndTargetValue (position.get() * 0.1f);
}

void Wah::process (float* samples, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    smoothedPosition.setTargetValue (position.get() * 0.1f);
    const bool automatic = mode.getIndex() == 1;
    const auto q = 2.0f + resonance.get();
    const auto lfoHz = rate.get();

    for (int i = 0; i < numSamples; ++i)
    {
        auto where = smoothedPosition.getNextValue();

        if (automatic)
            where = 0.5f + 0.5f * Lfo::sine (lfo.advance (lfoHz, sampleRate));

        // A real wah sweeps from about 350 Hz to 2.2 kHz.
        const auto cutoff = 350.0f * std::exp2 (where * 2.65f);
        const auto out = filter.process (samples[i], cutoff, q, sampleRate);

        // Mostly the resonant peak, with some of the low end kept so it does
        // not get thin.
        samples[i] = 2.1f * (out.bandPass / q + 0.3f * out.lowPass);
    }
}

//==============================================================================
namespace
{
struct Formant { float hz[3]; };

// The first three resonances of the vowels in "father", "bed", "see", "law", "boot".
constexpr Formant vowelTable[] = {
    { { 750.0f, 1150.0f, 2800.0f } },
    { { 550.0f, 1800.0f, 2500.0f } },
    { { 300.0f, 2200.0f, 2950.0f } },
    { { 500.0f, 850.0f,  2700.0f } },
    { { 330.0f, 700.0f,  2500.0f } },
};

constexpr float formantGains[] = { 1.0f, 0.6f, 0.3f };
}

void VowelFilter::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    wet.reset (sampleRate, 0.03);
    smoothedVowel.reset (sampleRate, 0.03);
    reset();
}

void VowelFilter::reset()
{
    for (auto& formant : formants)
        formant.reset();

    lfo.reset();
    wet.setCurrentAndTargetValue (mix.get() * 0.01f);
    smoothedVowel.setCurrentAndTargetValue (vowel.get());
}

void VowelFilter::process (float* samples, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    wet.setTargetValue (mix.get() * 0.01f);
    smoothedVowel.setTargetValue (vowel.get());

    const auto swing = depth.get() * 0.15f;
    const auto q = 4.0f + resonance.get() * 1.2f;
    const auto lfoHz = rate.get();

    for (int i = 0; i < numSamples; ++i)
    {
        const auto x = samples[i];
        auto where = smoothedVowel.getNextValue() + swing * Lfo::sine (lfo.advance (lfoHz, sampleRate));

        // Bounce off the ends instead of sticking there.
        if (where < 0.0f) where = -where;
        if (where > 4.0f) where = 8.0f - where;
        where = juce::jlimit (0.0f, 4.0f, where);

        const auto index = juce::jmin (3, (int) where);
        const auto fraction = where - (float) index;

        float sum = 0.0f;

        for (size_t f = 0; f < 3; ++f)
        {
            // Glide between vowels evenly by ratio, as the ear hears pitch.
            const auto a = vowelTable[index].hz[f];
            const auto b = vowelTable[index + 1].hz[f];
            const auto hz = a * std::pow (b / a, fraction);
            sum += formantGains[f] * formants[f].process (x, hz, q, sampleRate).bandPass / q;
        }

        const auto amount = wet.getNextValue();
        samples[i] = x + amount * (4.4f * sum - x);
    }
}

//==============================================================================
std::vector<Parameter*> GraphicEq::getControls()
{
    std::vector<Parameter*> controls;

    for (auto& band : bands)
        controls.push_back (&band);

    controls.push_back (&level);
    return controls;
}

void GraphicEq::prepare (double sampleRate, int)
{
    rate = sampleRate;
    lastGains.fill (-100.0f);
    output.reset (rate, 0.03);
    update();
    reset();
}

void GraphicEq::reset()
{
    for (auto& filter : filters)
        filter.reset();

    output.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (level.get()));
}

void GraphicEq::update() noexcept
{
    for (size_t i = 0; i < bands.size(); ++i)
    {
        const auto gain = bands[i].get();

        if (juce::exactlyEqual (gain, lastGains[i]))
            continue;

        lastGains[i] = gain;
        const auto hz = juce::jmin (100.0f * (float) (1 << i), (float) (rate * 0.4));
        filters[i].coefficients = Coefficients::makePeakFilter (rate, hz, 1.4f, juce::Decibels::decibelsToGain (gain));
    }
}

void GraphicEq::process (float* samples, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    update();
    output.setTargetValue (juce::Decibels::decibelsToGain (level.get()));

    for (int i = 0; i < numSamples; ++i)
    {
        auto x = samples[i];

        for (auto& filter : filters)
            x = filter.processSample (x);

        samples[i] = x * output.getNextValue();
    }
}
