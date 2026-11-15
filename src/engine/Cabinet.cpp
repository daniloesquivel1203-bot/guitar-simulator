#include "Cabinet.h"

namespace
{
struct Band
{
    enum Type { highPass, lowPass, peak };

    Type type;
    float hz;
    float q;
    float db;   // peaks only
};

struct CabinetSpec
{
    const char* name;
    std::initializer_list<Band> bands;
};

// Each cabinet is described by the frequency response of its speakers: a bass
// roll-off, a low resonance, a scooped lower midrange, an upper-mid presence
// peak, and a steep treble roll-off.
const CabinetSpec& getSpec (int index)
{
    static const CabinetSpec specs[] =
    {
        { "1x12 Open Back",
          { { Band::highPass, 75.0f, 0.8f, 0.0f },   { Band::peak, 105.0f, 1.4f, 3.0f },
            { Band::peak, 420.0f, 1.0f, -2.0f },     { Band::peak, 2300.0f, 1.2f, 4.5f },
            { Band::peak, 3800.0f, 2.5f, 2.0f },     { Band::lowPass, 5600.0f, 0.9f, 0.0f },
            { Band::lowPass, 6800.0f, 0.7f, 0.0f } } },

        { "2x12 British",
          { { Band::highPass, 85.0f, 0.9f, 0.0f },   { Band::peak, 125.0f, 1.5f, 4.0f },
            { Band::peak, 500.0f, 0.9f, -3.0f },     { Band::peak, 1500.0f, 2.0f, -1.5f },
            { Band::peak, 2800.0f, 1.5f, 5.5f },     { Band::lowPass, 5000.0f, 1.0f, 0.0f },
            { Band::lowPass, 6200.0f, 0.7f, 0.0f } } },

        { "4x12 Closed Back",
          { { Band::highPass, 95.0f, 1.1f, 0.0f },   { Band::peak, 115.0f, 1.8f, 5.0f },
            { Band::peak, 450.0f, 0.8f, -4.5f },     { Band::peak, 1300.0f, 2.2f, -2.0f },
            { Band::peak, 3400.0f, 1.8f, 5.0f },     { Band::lowPass, 4600.0f, 1.1f, 0.0f },
            { Band::lowPass, 5600.0f, 0.7f, 0.0f } } },
    };

    return specs[juce::jlimit (0, (int) std::size (specs) - 1, index)];
}

constexpr int numBuiltIns = 3;
constexpr int builtInImpulseLength = 4096;
const char* const builtInPrefix = "builtin:";
const char* const filePrefix = "file:";
}

Cabinet::Cabinet()
{
    selectBuiltIn (0);
}

juce::StringArray Cabinet::getBuiltInNames()
{
    juce::StringArray names;

    for (int i = 0; i < numBuiltIns; ++i)
        names.add (getSpec (i).name);

    return names;
}

juce::AudioBuffer<float> Cabinet::makeBuiltInImpulse (int index, double rate)
{
    const auto length = juce::roundToInt (builtInImpulseLength * rate / builtInSampleRate);
    juce::AudioBuffer<float> impulse (1, length);
    impulse.clear();
    impulse.setSample (0, 0, 1.0f);

    auto* data = impulse.getWritePointer (0);

    for (const auto& band : getSpec (index).bands)
    {
        const auto hz = juce::jmin (band.hz, (float) (rate * 0.45));
        Filter filter;

        switch (band.type)
        {
            case Band::highPass:  filter.coefficients = Coefficients::makeHighPass (rate, hz, band.q); break;
            case Band::lowPass:   filter.coefficients = Coefficients::makeLowPass (rate, hz, band.q); break;
            case Band::peak:      filter.coefficients = Coefficients::makePeakFilter (rate, hz, band.q,
                                                                                      juce::Decibels::decibelsToGain (band.db)); break;
        }

        for (int i = 0; i < length; ++i)
            data[i] = filter.processSample (data[i]);
    }

    // Fade the tail so the response ends at exactly zero.
    const auto fadeLength = length / 8;
    impulse.applyGainRamp (length - fadeLength, fadeLength, 1.0f, 0.0f);

    return impulse;
}

void Cabinet::selectBuiltIn (int index)
{
    builtInIndex = juce::jlimit (0, numBuiltIns - 1, index);
    loadedFile = juce::File();

    convolution.loadImpulseResponse (makeBuiltInImpulse (builtInIndex, builtInSampleRate), builtInSampleRate,
                                     juce::dsp::Convolution::Stereo::no,
                                     juce::dsp::Convolution::Trim::no,
                                     juce::dsp::Convolution::Normalise::yes);
}

juce::String Cabinet::loadFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return "The file \"" + file.getFileName() + "\" could not be found.";

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return "\"" + file.getFileName() + "\" is not an audio file this app can read. Use a WAV or AIFF impulse response.";

    // Cabinet responses are short. Cap the length so a long file cannot overload the CPU.
    const auto maxSamples = (juce::int64) (maxImpulseSeconds * reader->sampleRate);
    const auto samplesToUse = (size_t) juce::jmin (reader->lengthInSamples, maxSamples);

    convolution.loadImpulseResponse (file,
                                     juce::dsp::Convolution::Stereo::no,
                                     juce::dsp::Convolution::Trim::yes,
                                     samplesToUse,
                                     juce::dsp::Convolution::Normalise::yes);

    loadedFile = file;
    builtInIndex = -1;
    return {};
}

juce::String Cabinet::getCurrentName() const
{
    return builtInIndex >= 0 ? juce::String (getSpec (builtInIndex).name)
                             : loadedFile.getFileNameWithoutExtension();
}

juce::String Cabinet::getSourceDescription() const
{
    return builtInIndex >= 0 ? builtInPrefix + juce::String (builtInIndex)
                             : filePrefix + loadedFile.getFullPathName();
}

void Cabinet::restoreFromDescription (const juce::String& description)
{
    if (description.startsWith (filePrefix)
        && loadFile (juce::File (description.fromFirstOccurrenceOf (filePrefix, false, false))).isEmpty())
        return;

    // Fall back to a built-in cabinet if the saved file has gone missing.
    selectBuiltIn (description.startsWith (builtInPrefix)
                       ? description.fromFirstOccurrenceOf (builtInPrefix, false, false).getIntValue()
                       : 0);
}

void Cabinet::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    convolution.prepare ({ sampleRate, (juce::uint32) juce::jmax (1, maxBlockSize), 1 });

    lastLowCut = lastHighCut = -1.0f;
    updateFilters();
    reset();
}

void Cabinet::reset()
{
    convolution.reset();
    lowCutFilter.reset();
    highCutFilter.reset();
}

void Cabinet::updateFilters()
{
    const auto newLowCut = lowCut.get(), newHighCut = highCut.get();

    if (! juce::exactlyEqual (newLowCut, lastLowCut))
    {
        lastLowCut = newLowCut;
        lowCutFilter.coefficients = Coefficients::makeHighPass (sampleRate, newLowCut);
    }

    if (! juce::exactlyEqual (newHighCut, lastHighCut))
    {
        lastHighCut = newHighCut;
        highCutFilter.coefficients = Coefficients::makeLowPass (sampleRate, juce::jmin (newHighCut, (float) (sampleRate * 0.45)));
    }
}

void Cabinet::process (float* samples, int numSamples) noexcept
{
    if (! enabled.isOn())
        return;

    updateFilters();

    float* channel[] = { samples };
    juce::dsp::AudioBlock<float> block (channel, 1, (size_t) numSamples);
    convolution.process (juce::dsp::ProcessContextReplacing<float> (block));

    for (int i = 0; i < numSamples; ++i)
        samples[i] = highCutFilter.processSample (lowCutFilter.processSample (samples[i]));
}
