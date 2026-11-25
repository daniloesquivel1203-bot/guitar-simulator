#pragma once

#include "Effect.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <functional>
#include <vector>

/** Small DSP building blocks that a Custom pedal chains together. */
namespace custom_blocks
{
enum class BlockType
{
    empty = 0,
    lowPass,
    highPass,
    bandPass,
    notch,
    peak,
    lowShelf,
    highShelf,
    gain,
    clipper,
    lfo,
    envelope,
    delay,
    allPass,
    reducer,
    split,
    pathB,
    mix,
    multiply,    // added later: new types go at the end, so saved type numbers keep their meaning
    rectifier,
    ducker,
    wavefolder,
    noise,
    numTypes
};

/** True for blocks with a second input (the lower dot on the Patch canvas):
    Mix, Multiply and Ducker. */
inline bool hasSecondInput (BlockType type) noexcept
{
    return type == BlockType::mix || type == BlockType::multiply || type == BlockType::ducker;
}

/** True for blocks only the Patch pedal offers, since they need its wiring. */
inline bool isPatchOnly (BlockType type) noexcept   { return type == BlockType::multiply || type == BlockType::ducker; }

/** True for the LFO and Envelope blocks, which make no sound themselves but
    move another block's main knob. */
inline bool isModulator (BlockType type) noexcept   { return type == BlockType::lfo || type == BlockType::envelope; }

/** True for the Split, Path B and Mix markers, which route the signal into
    two parallel paths and back together. */
inline bool isRouting (BlockType type) noexcept
{
    return type == BlockType::split || type == BlockType::pathB || type == BlockType::mix;
}

/** The full name, as the add menu lists it. */
const char* getTypeName (BlockType) noexcept;

/** A short label for the block's tile, such as "LPF". */
const char* getShortName (BlockType) noexcept;

/** One sentence on what the block does to the sound. */
const char* getDescription (BlockType) noexcept;

/** A two-integrator state-variable filter in the "trapezoidal" form, which
    stays stable and click-free while its cutoff moves. Mixing its three
    internal signals gives every filter shape a block needs: low-pass,
    high-pass, band-pass, notch, peak and both shelves. */
struct SvfStage
{
    float ic1 = 0.0f, ic2 = 0.0f;
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    float m0 = 1.0f, m1 = 0.0f, m2 = 0.0f;

    void reset() noexcept   { ic1 = ic2 = 0.0f; }

    /** Sets the shape. `gainDb` matters only for the peak and shelf shapes. */
    void setCoefficients (BlockType, float hz, float q, float gainDb, double sampleRate) noexcept;

    float process (float x) noexcept
    {
        const auto v3 = x - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return m0 * x + m1 * v1 + m2 * v2;
    }
};

enum Shape { soft = 0, hard, diode, asymmetric };
enum Wave { sine = 0, triangle, square, rampUp, rampDown };

constexpr float maxDelayMs = 2000.0f;

/** True for the filter and EQ shapes the state-variable filter makes. */
bool isFilter (BlockType) noexcept;

/** What a modulator moves on a block of this type: the cutoff or
    frequency of a filter, the level of a gain block, the drive of a
    clipper. Empty for blocks that cannot be moved. */
const char* getModulatedKnobName (BlockType) noexcept;

/** Sets up one or two filter stages for a filter block's knobs. */
void configureFilter (BlockType, float hz, float q, float gainDb, bool steep, double sampleRate,
                      std::array<SvfStage, 2>& stages, int& numStages) noexcept;

//==============================================================================
/** The knobs of one block. Every block has the same fixed set, whatever its
    type, so a pedal's parameter list never changes. */
struct BlockParams
{
    /** `idFor (field)` gives each parameter's id (it must outlive the block);
        `targetNames` are the choices for what a modulator moves, "None" first. */
    BlockParams (const std::function<const char* (int field)>& idFor, const std::vector<const char*>& targetNames);

    static constexpr int numFields = 21;
    static const char* const fieldNames[numFields];   // "type", "freq", ... as used in ids

    Parameter type, frequency, resonance, gainDb, driveDb, shape, slope, invert;

    // Modulator (LFO and Envelope) controls. Target is 0 for none, or a block number from 1.
    Parameter rate, depth, target, wave, sensitivity, attack, release;

    // Delay, all-pass, reducer and Mix controls.
    Parameter time, feedback, mix, stages, bits, downsample;

    std::vector<Parameter*> getParameters()
    {
        return { &type, &frequency, &resonance, &gainDb, &driveDb, &shape, &slope, &invert,
                 &rate, &depth, &target, &wave, &sensitivity, &attack, &release,
                 &time, &feedback, &mix, &stages, &bits, &downsample };
    }

    /** The block this modulator moves, or -1. */
    int getTargetSlot() const noexcept   { return target.getIndex() - 1; }

    BlockType getType() const noexcept   { return (BlockType) type.getIndex(); }

    /** Puts every knob back to its default, then sets sensible starting
        points for a new block of this type. */
    void setFresh (BlockType);
};

/** A knob a block of some type uses, with the caption the panel shows.
    A "Width" control is the resonance parameter (Q), which the panel shows
    as a width in Hz around the block's frequency. */
struct Control
{
    Parameter* parameter;
    const char* caption;
};

std::vector<Control> getControlsFor (BlockParams&, BlockType);

//==============================================================================
/** The audio-thread state of one block, and the code that runs it. Used by
    both the Custom and the Patch pedal. */
class BlockRunner
{
public:
    /** Message thread: sizes the delay line. */
    void prepare (double sampleRate);
    void reset() noexcept;

    /** Moves the smoothed knobs towards the parameters, or jumps there. */
    void sync (const BlockParams&, bool snap) noexcept;

    /** The type (and slope) it now runs as. */
    void setShape (BlockType, bool steep) noexcept;
    BlockType getType() const noexcept   { return type; }

    /** The smoothed Mix knob, which a Mix block uses as its blend. */
    juce::SmoothedValue<float>& getBlend() noexcept   { return mix; }

    /** Runs a sound-making block in place. `mod` is the modulation of its
        main knob, -1 to 1 per sample, or nullptr. `side` is a Multiply or
        Ducker block's second input, or nullptr when nothing is wired there. */
    void process (const BlockParams&, float* samples, int numSamples, const float* mod, const float* side = nullptr) noexcept;

    /** Runs an LFO or Envelope block, adding its movement into `out` (which
        may be nullptr when it moves nothing). `input` is what an envelope follows. */
    void modulate (const BlockParams&, const float* input, float* out, int numSamples) noexcept;

private:
    void updateFilter (float frequencyRatio = 1.0f) noexcept;

    std::array<SvfStage, 2> stages;
    int numStages = 1;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> frequency { 1000.0f }, resonance { 0.707f };
    juce::SmoothedValue<float> gainDb, driveDb, depth;
    float dcX1 = 0.0f, dcY1 = 0.0f;
    double lfoPhase = 0.0;
    float envelope = 0.0f;
    juce::SmoothedValue<float> mix, feedback;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> time { 300.0f };
    std::vector<float> delayLine;   // sized in prepare()
    int writeIndex = 0;
    std::array<float, 8> allPassState {};
    float heldSample = 0.0f;
    double carrierPhase = 0.0;
    uint32_t noiseState = 0x9e3779b9u;
    float noiseTone = 0.0f;
    int holdCounter = 0;

    BlockType type = BlockType::empty;
    bool steep = false;
    double rate = 48000.0;
    float dcCoefficient = 0.998f;
};
}
