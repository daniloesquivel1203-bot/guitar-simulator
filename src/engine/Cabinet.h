#pragma once

#include "Parameter.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <array>

/** A speaker cabinet simulator based on impulse responses.

    A raw amp signal is harsh and fizzy. A guitar speaker removes most of the
    treble and adds its own resonances, and an impulse response (IR) captures
    that. Three built-in cabinets are generated mathematically, so no audio
    files ship with the app, and any IR file (WAV or AIFF) can be loaded.

    Mono in, mono out, processed in place. Loading and selecting happen on the
    UI thread and are swapped in without clicks.
*/
class Cabinet
{
public:
    Parameter enabled { "cab.enabled", "On",       0.0f,    1.0f,     1.0f,    1.0f };
    Parameter lowCut  { "cab.lowCut",  "Low cut",  20.0f,   300.0f,   60.0f,   1.0f,   " Hz" };
    Parameter highCut { "cab.highCut", "High cut", 2000.0f, 16000.0f, 8000.0f, 100.0f, " Hz" };

    std::array<Parameter*, 3> getParameters() noexcept   { return { &enabled, &lowCut, &highCut }; }

    Cabinet();

    static juce::StringArray getBuiltInNames();

    /** Creates the impulse response of a built-in cabinet at the given sample rate. */
    static juce::AudioBuffer<float> makeBuiltInImpulse (int index, double sampleRate);

    void selectBuiltIn (int index);

    /** Loads an impulse response file. Returns an error message, or an empty
        string on success. On failure the current cabinet stays in use. */
    juce::String loadFile (const juce::File& file);

    /** -1 when a file is loaded. */
    int getBuiltInIndex() const noexcept        { return builtInIndex; }
    juce::File getLoadedFile() const            { return loadedFile; }
    juce::String getCurrentName() const;

    /** A string that identifies the current cabinet, for saving in settings. */
    juce::String getSourceDescription() const;
    void restoreFromDescription (const juce::String& description);

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** numSamples must not exceed the block size given to prepare(). */
    void process (float* samples, int numSamples) noexcept;

private:
    using Filter = juce::dsp::IIR::Filter<float>;
    using Coefficients = juce::dsp::IIR::Coefficients<float>;

    static constexpr double builtInSampleRate = 48000.0;
    static constexpr double maxImpulseSeconds = 0.5;

    void updateFilters();

    juce::dsp::Convolution convolution;
    Filter lowCutFilter, highCutFilter;

    double sampleRate = 48000.0;
    float lastLowCut = -1.0f, lastHighCut = -1.0f;

    int builtInIndex = 0;
    juce::File loadedFile;
};
