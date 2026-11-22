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
    numTypes
};

/** The full name, as the add menu lists it. */
const char* getTypeName (BlockType) noexcept;

/** A short label for the block's tile, such as "LPF". */
const char* getShortName (BlockType) noexcept;

/** One sentence on what the block does to the sound. */
const char* getDescription (BlockType) noexcept;

/** The stretch of frequencies a band-pass, notch or peak block acts on: its
    -3 dB points, or for a peak the points at half its boost or cut. */
struct Band
{
    float lowHz, highHz;

    float getWidth() const noexcept   { return highHz - lowHz; }
};

/** The band around `centreHz` at a Narrowness of `q`, stopping at 20 kHz. */
Band getBand (float centreHz, float q) noexcept;

/** The band in words, on two lines: its width, then where it starts and ends. */
juce::String describeBand (float centreHz, float q);

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
    clipper) that the guitar passes through in order.

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

    static constexpr int maxBlocks = 8;

    CustomPedal();

    struct Slot
    {
        explicit Slot (int index);

        Parameter type, frequency, resonance, gainDb, driveDb, shape, slope, invert;

        std::vector<Parameter*> getParameters()
        {
            return { &type, &frequency, &resonance, &gainDb, &driveDb, &shape, &slope, &invert };
        }

        BlockType getType() const noexcept   { return (BlockType) type.getIndex(); }
    };

    enum Shape { soft = 0, hard, diode, asymmetric };

    std::vector<Parameter*> getControls() override;
    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* samples, int numSamples) noexcept override;

    Slot& getSlot (int index) noexcept   { return *slots[(size_t) index]; }

    /** The pedal's overall output level. */
    Parameter level { "custom.level", "Level", -24.0f, 12.0f, 0.0f, 0.1f, " dB" };

    /** Which slot is first, second and so on, packed into one number. */
    Parameter order { "custom.order", "Order", 0.0f, 40319.0f, 0.0f, 1.0f };

    //==============================================================================
    /** The slots in use, in the order the signal passes through them. */
    std::vector<int> getBlocksInOrder() const;

    /** Puts a new block, with fresh settings, after the last one. Returns its
        slot, or -1 when all eight are in use. */
    int addBlock (BlockType);

    /** Empties a slot. */
    void removeBlock (int slot);

    /** Moves the block at position `from` (counted among the blocks in use)
        to position `to`, counted after it has left its old place. */
    void moveBlock (int from, int to);

    /** The knobs a block of this type uses, with the caption each shows. */
    struct Control
    {
        Parameter* parameter;
        const char* caption;
    };

    static std::vector<Control> getControlsFor (Slot&, BlockType);

    using Order = std::array<int, maxBlocks>;

    /** A permutation of 0..7 to and from one whole number. */
    static Order decodeOrder (int code) noexcept;
    static int encodeOrder (const Order&) noexcept;

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
        juce::SmoothedValue<float> gainDb, driveDb;
        float dcX1 = 0.0f, dcY1 = 0.0f;

        void reset() noexcept;
    };

    Structure readStructure() const noexcept;
    void applyStructure (const Structure&) noexcept;
    void updateTargets (int slot, bool snap) noexcept;
    void updateFilter (int slot) noexcept;
    void runSlot (int slot, float* samples, int numSamples) noexcept;

    void setOrder (const Order&);

    std::array<std::unique_ptr<Slot>, maxBlocks> slots;
    std::array<Runner, maxBlocks> runners;
    Structure applied;
    std::vector<int> activeSlots;   // reserved in the constructor, so the audio thread never allocates

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetMix { 1.0f };
    juce::SmoothedValue<float> outputGain { 1.0f };
    std::vector<float> dry;
    double rate = 48000.0;
    float dcCoefficient = 0.998f;
};
