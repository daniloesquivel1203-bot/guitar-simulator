#pragma once

#include "BlockDsp.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <memory>
#include <vector>

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

    using Slot = custom_blocks::BlockParams;


    /** What a modulator moves on a block of this type: the cutoff or
        frequency of a filter, the level of a gain block, the drive of a
        clipper. Empty for blocks that cannot be moved. */
    static const char* getModulatedKnobName (BlockType type) noexcept   { return custom_blocks::getModulatedKnobName (type); }

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
    using Control = custom_blocks::Control;

    static std::vector<Control> getControlsFor (Slot& slot, BlockType type)   { return custom_blocks::getControlsFor (slot, type); }

    using Order = std::array<int, maxBlocks>;

    /** A permutation of all slots to and from one whole number. */
    static Order decodeOrder (int code) noexcept;
    static int encodeOrder (const Order&) noexcept;

    /** Message thread. The level change in dB, at each frequency, of the
        filter, gain and all-pass blocks and the two paths, as their knobs are
        set now (ignoring modulators, delays, the reducer and distortion). `hasFilters` is false when there
        are none to draw. */
    std::vector<float> getResponseDb (const std::vector<float>& frequencies, bool& hasFilters);


    /** The order the pedal's knobs describe now. */
    Order getOrder() const noexcept;

    /** The blocks a slot of this type can modulate the delay time of, etc. */
    static constexpr float maxDelayMs = custom_blocks::maxDelayMs;

private:
    struct Structure
    {
        Order order {};
        std::array<BlockType, maxBlocks> types {};
        std::array<int, maxBlocks> slopes {};

        bool operator== (const Structure&) const = default;
    };


    Structure readStructure() const noexcept;
    void applyStructure (const Structure&) noexcept;
    void runModulators (const float* input, int numSamples) noexcept;

    void setOrder (const Order&);

    std::array<std::unique_ptr<Slot>, maxBlocks> slots;
    std::array<custom_blocks::BlockRunner, maxBlocks> runners;
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
};
