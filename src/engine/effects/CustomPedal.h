#pragma once

#include "Effect.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <memory>
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
    numTypes
};

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
}

//==============================================================================
/** A pedal you build yourself from up to eight blocks (filters, gain and a
    clipper) that the guitar passes through in order. LFO and Envelope blocks
    pass the sound unchanged and instead move another block's main knob, for
    filter sweeps, auto-wah and tremolo.

    Every block slot has a fixed set of knobs, so the pedal's parameter list
    never changes. That keeps presets, undo, A/B and the stereo twin working
    exactly as they do for the other pedals. Which slots are in use, and in
    what order, are parameters too. When either changes, the pedal blends to
    the dry signal for a few milliseconds, switches, and blends back, so
    editing while playing never clicks.

    The editing calls (addBlock, removeBlock, moveBlock) are for the message
    thread and only turn knobs, like the UI does.
*/
class CustomPedal final : public MonoEffect
{
public:
    using BlockType = custom_blocks::BlockType;

    static constexpr int maxBlocks = 10;

    CustomPedal();

    struct Slot
    {
        explicit Slot (int index);

        Parameter type, frequency, resonance, gainDb, driveDb, shape, slope, invert;

        // Modulator (LFO and Envelope) controls. Target is 0 for none, or a slot number from 1.
        Parameter rate, depth, target, wave, sensitivity, attack, release;

        // Delay, all-pass, reducer and Mix controls.
        Parameter time, feedback, mix, stages, bits, downsample;

        std::vector<Parameter*> getParameters()
        {
            return { &type, &frequency, &resonance, &gainDb, &driveDb, &shape, &slope, &invert,
                     &rate, &depth, &target, &wave, &sensitivity, &attack, &release,
                     &time, &feedback, &mix, &stages, &bits, &downsample };
        }

        /** The slot this modulator moves, or -1. */
        int getTargetSlot() const noexcept   { return target.getIndex() - 1; }

        BlockType getType() const noexcept   { return (BlockType) type.getIndex(); }
    };

    enum Shape { soft = 0, hard, diode, asymmetric };
    enum Wave { sine = 0, triangle, square, rampUp, rampDown };

    /** What a modulator moves on a block of this type: the cutoff or
        frequency of a filter, the level of a gain block, the drive of a
        clipper. Empty for blocks that cannot be moved. */
    static const char* getModulatedKnobName (BlockType) noexcept;

    /** Points a modulator at another slot (or -1 for none). Refuses slots
        that are empty, other modulators, or itself. */
    bool setTarget (int modulatorSlot, int targetSlot);

    std::vector<Parameter*> getControls() override;
    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* samples, int numSamples) noexcept override;

    Slot& getSlot (int index) noexcept   { return *slots[(size_t) index]; }

    /** A name the player gave this pedal, shown on its tile. Empty for an
        unnamed one. Message thread only. */
    juce::String label;

    /** The pedal's overall output level. */
    Parameter level { "custom.level", "Level", -24.0f, 12.0f, 0.0f, 0.1f, " dB" };

    /** Which slot is first, second and so on, packed into one number.
        `order` is the 8-block form from the first test builds, still read
        when `order2` is zero; new edits write `order2`. */
    Parameter order { "custom.order", "Order", 0.0f, 40319.0f, 0.0f, 1.0f };
    Parameter order2 { "custom.order2", "Order", 0.0f, 3628799.0f, 0.0f, 1.0f };

    //==============================================================================
    /** The slots in use, in the order the signal passes through them. */
    std::vector<int> getBlocksInOrder() const;

    /** Puts a new block, with fresh settings, after the last one. Returns its
        slot, or -1 when all eight are in use. */
    int addBlock (BlockType);

    /** Empties a slot. For a Split, Path B or Mix marker, empties all the
        markers of that branch, leaving its blocks in one line. */
    void removeBlock (int slot);

    /** Adds a Split, an empty lower path and the Mix that joins them, after
        the last block. Returns the Split's slot, or -1 without room for three. */
    int addSplit();

    /** The Split, Path B and Mix markers of the branch a marker belongs to,
        or just the slot itself. */
    std::vector<int> getSplitGroup (int slot) const;

    /** Moves the block at position `from` (counted among the blocks in use)
        to position `to`, counted after it has left its old place. */
    void moveBlock (int from, int to);

    /** The knobs a block of this type uses, with the caption each shows.
        A "Width" control is the resonance parameter (Q), which the panel
        shows as a width in Hz around the block's frequency. */
    struct Control
    {
        Parameter* parameter;
        const char* caption;
    };

    static std::vector<Control> getControlsFor (Slot&, BlockType);

    using Order = std::array<int, maxBlocks>;

    /** A permutation of all slots to and from one whole number. */
    static Order decodeOrder (int code) noexcept;
    static int encodeOrder (const Order&) noexcept;

    /** Message thread. The level change in dB, at each frequency, of the
        filter, gain and all-pass blocks and the two paths, as their knobs are
        set now (ignoring modulators, delays, the reducer and distortion). `hasFilters` is false when there
        are none to draw. */
    std::vector<float> getResponseDb (const std::vector<float>& frequencies, bool& hasFilters);

    static void configureFilter (BlockType, float hz, float q, float gainDb, bool steep, double sampleRate,
                                 std::array<custom_blocks::SvfStage, 2>& stages, int& numStages) noexcept;

    /** The order the pedal's knobs describe now. */
    Order getOrder() const noexcept;

    /** The blocks a slot of this type can modulate the delay time of, etc. */
    static constexpr float maxDelayMs = 2000.0f;

private:
    struct Structure
    {
        Order order {};
        std::array<BlockType, maxBlocks> types {};
        std::array<int, maxBlocks> slopes {};

        bool operator== (const Structure&) const = default;
    };

    /** The audio-thread state of one slot. */
    struct Runner
    {
        std::array<custom_blocks::SvfStage, 2> stages;
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
        int holdCounter = 0;

        void reset() noexcept;
    };

    Structure readStructure() const noexcept;
    void applyStructure (const Structure&) noexcept;
    void updateTargets (int slot, bool snap) noexcept;
    void updateFilter (int slot, float frequencyRatio = 1.0f) noexcept;
    void runSlot (int slot, float* samples, int numSamples, const float* modulation) noexcept;
    void runModulators (const float* input, int numSamples) noexcept;

    void setOrder (const Order&);

    std::array<std::unique_ptr<Slot>, maxBlocks> slots;
    std::array<Runner, maxBlocks> runners;
    Structure applied;
    std::vector<int> activeSlots;   // reserved in the constructor, so the audio thread never allocates

    // Per target slot: how far modulators push its main knob, -1 to 1 per sample.
    std::vector<float> modulation;
    std::array<bool, maxBlocks> isModulated {};
    int bufferSize = 1;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetMix { 1.0f };
    juce::SmoothedValue<float> outputGain { 1.0f };
    std::vector<float> dry, splitInput, pathA;
    double rate = 48000.0;
    float dcCoefficient = 0.998f;
};
