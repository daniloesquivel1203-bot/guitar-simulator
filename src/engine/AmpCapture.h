#pragma once

#include "Parameter.h"
#include "RateConverter.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <memory>

namespace nam { class DSP; }

/** Plays a Neural Amp Modeler (NAM) capture of a real amp or pedal.

    A capture is a .nam file: a small neural network trained to behave like
    one piece of gear at one set of knob positions. Thousands are shared
    online for free. The network itself is run by NeuralAmpModelerCore, the
    same open source library as the official NAM plugin, so a capture sounds
    the way its author made it.

    A capture has no knobs of its own, so this block adds an input level (how
    hard the capture is driven), bass, mid and treble after it, and an output
    level. Captures that report their loudness are levelled to a common
    volume, so switching between them does not jump in volume. A capture made
    at a different sample rate from the audio device is converted on the fly.

    Mono in, mono out, processed in place. Loading happens on the UI thread
    and the new capture is swapped in without stopping the audio.
*/
class AmpCapture
{
public:
    /** The volume, in dB, that captures with a known loudness are levelled to. */
    static constexpr double targetLoudnessDb = -18.0;

    /** When on, and a capture is loaded, it replaces the built-in amp. */
    Parameter active { "capture.active", "Use capture", 0.0f, 1.0f, 0.0f, 1.0f };
    Parameter input  { "capture.input",  "Input",  -20.0f, 20.0f, 0.0f, 0.1f, " dB" };
    Parameter bass   { "capture.bass",   "Bass",     0.0f, 10.0f, 5.0f };
    Parameter mid    { "capture.mid",    "Mid",      0.0f, 10.0f, 5.0f };
    Parameter treble { "capture.treble", "Treble",   0.0f, 10.0f, 5.0f };
    Parameter output { "capture.output", "Output", -20.0f, 20.0f, 0.0f, 0.1f, " dB" };

    std::array<Parameter*, 6> getParameters() noexcept   { return { &active, &input, &bass, &mid, &treble, &output }; }

    /** What the app knows about the loaded capture. */
    struct Info
    {
        juce::String name;            // the file name without its extension
        juce::String gearType;        // "amp", "amp_cab", "pedal" and so on, if the file says
        double sampleRate = 0.0;      // the rate the capture was made at
        bool hasLoudness = false;     // whether it can be levelled

        /** True when the capture already includes a speaker cabinet, so the
            separate cabinet block would add a second one. */
        bool includesCabinet() const   { return gearType.contains ("cab"); }
    };

    AmpCapture();
    ~AmpCapture();

    /** Loads a .nam file. Returns an error message, or an empty string on
        success. On failure the current capture stays in use. Loading the file
        that is already loaded does nothing. */
    juce::String loadFile (const juce::File& file);

    /** Removes the capture, so the built-in amp is used. */
    void unload();

    bool isLoaded() const noexcept         { return loaded.load(); }
    juce::File getLoadedFile() const       { return loadedFile; }
    Info getInfo() const                   { return info; }

    /** True when process() is running the capture rather than leaving the
        signal for the built-in amp. */
    bool isInUse() const noexcept          { return loaded.load() && active.isOn(); }

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** numSamples must not exceed the block size given to prepare(). */
    void process (float* samples, int numSamples) noexcept;

private:
    struct Model;

    std::unique_ptr<Model> makeModel (std::unique_ptr<nam::DSP> dsp) const;
    void updateTone();

    using Filter = juce::dsp::IIR::Filter<float>;
    using Coefficients = juce::dsp::IIR::Coefficients<float>;

    // Only process() and the swaps in loadFile(), unload() and prepare() touch
    // the model. The audio thread never waits for the lock: if a swap is in
    // progress it outputs silence for that one block.
    juce::SpinLock modelLock;
    std::unique_ptr<Model> model;

    double sampleRate = 48000.0;
    int maxBlockSize = 512;

    Filter bassFilter, midFilter, trebleFilter;
    float lastBass = -1.0f, lastMid = -1.0f, lastTreble = -1.0f;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inputGain { 1.0f }, outputGain { 1.0f };

    // Fades in after a new capture is swapped in, so the change does not click.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> swapFade { 1.0f };
    std::atomic<bool> fadeInPending { false };

    std::atomic<bool> loaded { false };
    juce::File loadedFile;
    Info info;
};
