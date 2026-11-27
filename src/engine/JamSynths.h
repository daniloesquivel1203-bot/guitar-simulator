#pragma once

#include "JamArranger.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <vector>

/** The backing band's instruments, all made from simple synthesis so nothing
    has to be downloaded or loaded from disk.

    Every class here is used on the audio thread only, after prepare(). They
    never allocate or lock once prepared. Each render call adds to its output. */
namespace jam::synth
{
/** A cheap noise source. */
struct Noise
{
    std::uint32_t state = 22222;

    float next() noexcept
    {
        state = state * 1664525u + 1013904223u;
        return (float) (std::int32_t) state * (1.0f / 2147483648.0f);
    }
};

/** A sine from a table, which is much cheaper than std::sin for the organ and piano. */
float fastSin (double phase) noexcept;   // phase in cycles

float midiToHz (int note) noexcept;

//==============================================================================
class DrumKit
{
public:
    void prepare (double sampleRate);
    void trigger (Drum, float velocity) noexcept;
    void stopAll() noexcept;
    void render (float* left, float* right, int numSamples) noexcept;

private:
    struct Piece
    {
        float level = 0.0f;     // current loudness, decays every sample
        float decay = 0.0f;
        float tone = 0.0f;      // pitch-sweeping parts
        float pitch = 0.0f, pitchTarget = 0.0f, pitchDecay = 0.0f;
        double phase = 0.0;
        float noiseLevel = 0.0f, noiseDecay = 0.0f;
        float lowState = 0.0f, highState = 0.0f;
        bool active = false;
    };

    float metal (double* phases, const float* ratios, double baseHz) noexcept;

    double sampleRate = 48000.0;
    Noise noise;
    std::array<Piece, numDrums> pieces {};
    std::array<double, 6> hatPhases {}, ridePhases {}, crashPhases {};
    float hatHigh = 0.0f, rideHigh = 0.0f, crashHigh = 0.0f, snareHigh = 0.0f;
    float stickBand1 = 0.0f, stickBand2 = 0.0f;
};

//==============================================================================
class Bass
{
public:
    void prepare (double sampleRate);
    void setUpright (bool shouldBeUpright) noexcept   { upright = shouldBeUpright; }
    void noteOn (int note, float velocity, int lengthSamples) noexcept;
    void stopAll() noexcept;
    void render (float* out, int numSamples) noexcept;

private:
    struct Voice
    {
        bool active = false;
        double phase = 0.0, step = 0.0;
        float level = 0.0f, velocity = 0.0f;
        float env = 0.0f;
        float filterEnv = 0.0f;
        int samplesLeft = 0;
        bool releasing = false;
        float ic1 = 0.0f, ic2 = 0.0f;
    };

    double sampleRate = 48000.0;
    bool upright = false;
    std::array<Voice, 2> voices {};
    int nextVoice = 0;
    float attackStep = 0.0f, decayCoeff = 0.0f, releaseCoeff = 0.0f, filterCoeff = 0.0f;
};

//==============================================================================
/** Six plucked strings (Karplus-Strong). A chord is strummed across them. */
class Guitar
{
public:
    static constexpr int numStrings = 6;

    void prepare (double sampleRate);
    void setDrive (float amount) noexcept   { drive = amount; }

    /** Strums `notes` (lowest first) across the strings. */
    void play (const NoteEvent&, int lengthSamples) noexcept;
    void stopAll() noexcept;
    void render (float* out, int numSamples) noexcept;

private:
    struct String
    {
        std::vector<float> buffer;
        int writeIndex = 0;
        double delay = 100.0;
        float feedback = 0.99f;
        float brightness = 0.5f;
        float loopState = 0.0f;
        bool active = false;

        int pendingNote = -1;      // waits for its turn in the strum
        int pendingDelay = 0;
        float pendingVelocity = 0.0f;
        Articulation pendingArticulation = Articulation::ring;
        int samplesLeft = 0;       // until the note is damped
        int silentFor = 0;
    };

    void pluck (String&, int note, float velocity, Articulation) noexcept;
    void damp (String&, double seconds) noexcept;
    float feedbackFor (double hz, double seconds) const noexcept;

    double sampleRate = 48000.0;
    Noise noise;
    std::array<String, numStrings> strings;
    float drive = 0.0f;
    float preHigh = 0.0f, postLow1 = 0.0f, postLow2 = 0.0f, dcX = 0.0f, dcY = 0.0f;
};

//==============================================================================
/** An electric piano made with FM: one sine bending another. */
class Keys
{
public:
    void prepare (double sampleRate);
    void noteOn (int note, float velocity, int lengthSamples) noexcept;
    void stopAll() noexcept;
    void render (float* out, int numSamples) noexcept;

private:
    struct Voice
    {
        bool active = false;
        int note = 0;
        double phase = 0.0, step = 0.0, modPhase = 0.0, tinePhase = 0.0;
        float level = 0.0f, decay = 0.0f, modIndex = 0.0f, modDecay = 0.0f, tineLevel = 0.0f, tineDecay = 0.0f;
        int samplesLeft = 0;
        bool releasing = false;
        juce::uint32 age = 0;
    };

    double sampleRate = 48000.0;
    std::array<Voice, 16> voices {};
    juce::uint32 counter = 0;
    float releaseCoeff = 0.0f;
};

//==============================================================================
/** A drawbar organ with a slow swirl. */
class Organ
{
public:
    void prepare (double sampleRate);
    void setPercussive (bool shouldClick) noexcept   { percussive = shouldClick; }
    void noteOn (int note, float velocity, int lengthSamples) noexcept;
    void stopAll() noexcept;
    void render (float* out, int numSamples) noexcept;

private:
    struct Voice
    {
        bool active = false;
        int note = 0;
        double phase = 0.0, step = 0.0;
        float level = 0.0f, target = 0.0f, percussion = 0.0f;
        int samplesLeft = 0;
        juce::uint32 age = 0;
    };

    double sampleRate = 48000.0;
    bool percussive = false;
    std::array<Voice, 12> voices {};
    juce::uint32 counter = 0;
    float attackCoeff = 0.0f, releaseCoeff = 0.0f, percussionDecay = 0.0f;
    double swirlPhase = 0.0;
    float bodyState = 0.0f;
};
}
