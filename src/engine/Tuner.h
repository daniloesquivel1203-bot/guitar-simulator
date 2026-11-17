#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <optional>
#include <vector>

/** Finds the pitch of a single guitar note.

    Uses the McLeod pitch method: it measures how well the waveform matches a
    shifted copy of itself at every possible period and picks the first strong
    match. That copes with guitar strings, whose harmonics are often louder
    than the fundamental and would fool a simple peak finder.
*/
class PitchDetector
{
public:
    static constexpr double minHz = 55.0;     // below the low B of a 7-string
    static constexpr double maxHz = 1400.0;   // above the 24th fret of the high E

    /** Returns the pitch in Hz, or nothing when there is no clear note. */
    static std::optional<double> detect (const float* samples, int numSamples, double sampleRate);
};

/** Turns the detector's raw readings into one steady pitch for the display.

    A raw reading wobbles by a few cents from one moment to the next, a plucked
    string starts sharp before it settles, and as a note dies away the detector
    can lock on to mains hum instead. The tracker skips the pick attack, stops
    reading once the note has faded, takes the middle value of the last few
    readings so a stray one is ignored, and then glides towards it.
*/
class PitchTracker
{
public:
    /** Call at a steady rate with the most recent audio, oldest sample first.
        Returns the settled pitch in Hz, or nothing when no note is sounding. */
    std::optional<double> update (const float* samples, int numSamples, double sampleRate);

    void reset();

private:
    static constexpr int historySize = 7;
    static constexpr int readingsNeeded = 3;
    static constexpr int attackUpdatesToSkip = 3;

    std::array<double, historySize> history {};   // recent readings, as log2 of Hz
    int historyCount = 0;
    int historyIndex = 0;
    int skipRemaining = 0;
    double previousLevel = 0.0;
    double notePeak = 0.0;
    double noiseFloor = 0.0;
    bool hasNoiseFloor = false;
    double smoothed = 0.0;      // log2 of Hz
    bool hasSmoothed = false;
};

/** A musical note and how far a pitch is from it. */
struct TunerReading
{
    juce::String note;     // for example "E"
    int octave = 0;        // scientific pitch notation, where A4 is 440 Hz
    double cents = 0.0;    // -50 to +50, negative is flat
    double hz = 0.0;

    static TunerReading fromHz (double hz);
};

/** A named set of six open-string notes, from the thickest string to the thinnest. */
struct Tuning
{
    juce::String name;
    std::array<int, 6> notes {};   // MIDI note numbers, where 40 is the low E of standard tuning

    /** Every tuning the tuner offers. Standard is first. */
    static const std::vector<Tuning>& getAll();

    /** "E2" style name for a MIDI note. */
    static juce::String noteName (int midiNote, bool withOctave = true);

    static double noteToHz (int midiNote);

    /** How far a pitch is from a target note, in cents. Negative is flat. */
    static double centsFrom (double hz, int midiNote);

    /** Index (0 = thickest) of the string whose note is closest to this pitch. */
    int closestString (double hz) const;
};

/** Carries recent input from the audio thread to the tuner display.

    The audio thread only copies samples into a ring buffer while the tuner is
    open, so the tuner costs nothing when it is closed.
*/
class TunerTap
{
public:
    static constexpr int windowSize = 8192;   // 170 ms at 48 kHz: about 14 cycles of a low E

    void setActive (bool shouldBeActive) noexcept   { active.store (shouldBeActive); }
    bool isActive() const noexcept                  { return active.load(); }

    /** Audio thread. */
    void push (const float* samples, int numSamples) noexcept
    {
        if (! active.load (std::memory_order_relaxed) || samples == nullptr)
            return;

        auto index = writeIndex.load (std::memory_order_relaxed);

        for (int i = 0; i < numSamples; ++i)
        {
            buffer[(size_t) index] = samples[i];
            index = (index + 1) & mask;
        }

        writeIndex.store (index, std::memory_order_release);
    }

    /** UI thread. Copies the most recent windowSize samples, oldest first. The
        ring is much longer than the window, so the audio thread cannot catch
        up with the region being copied. */
    void copyLatest (float* destination) const noexcept
    {
        const auto end = writeIndex.load (std::memory_order_acquire);

        for (int i = 0; i < windowSize; ++i)
            destination[i] = buffer[(size_t) ((end - windowSize + i) & mask)];
    }

private:
    static constexpr int ringSize = windowSize * 8;
    static constexpr int mask = ringSize - 1;

    std::vector<float> buffer = std::vector<float> ((size_t) ringSize, 0.0f);
    std::atomic<int> writeIndex { 0 };
    std::atomic<bool> active { false };
};
