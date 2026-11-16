#pragma once

#include <juce_core/juce_core.h>

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

/** A musical note and how far a pitch is from it. */
struct TunerReading
{
    juce::String note;     // for example "E"
    int octave = 0;        // scientific pitch notation, where A4 is 440 Hz
    double cents = 0.0;    // -50 to +50, negative is flat
    double hz = 0.0;

    static TunerReading fromHz (double hz);
};

/** Carries recent input from the audio thread to the tuner display.

    The audio thread only copies samples into a ring buffer while the tuner is
    open, so the tuner costs nothing when it is closed.
*/
class TunerTap
{
public:
    static constexpr int windowSize = 4096;

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
