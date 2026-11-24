#pragma once

#include "BlockDsp.h"

#include <array>
#include <memory>
#include <vector>

//==============================================================================
/** A pedal you wire yourself: up to sixteen blocks placed anywhere on a
    canvas, joined by wires from the guitar's input, between blocks, and to
    the output. One output can feed any number of blocks, a Mix block joins
    two, and a wire may lead back to an earlier block to make a feedback loop.

    Like the Custom pedal, every node has a fixed set of knobs, and the wires
    are knobs too (each input names the node it listens to), so presets,
    undo, A/B and the stereo twin work as for any other pedal. When the wiring
    or a block's type changes, the pedal blends to the dry signal for a few
    milliseconds, switches, and blends back.

    The sound runs in steps of 32 samples. A wire into a block that runs
    earlier in the step (a loop) carries the previous step's sound, so every
    loop has the same short delay of 32 samples whatever the audio buffer
    size, and is softly limited to full scale so it can never run away.
*/
class PatchPedal final : public MonoEffect
{
public:
    using BlockType = custom_blocks::BlockType;

    static constexpr int maxNodes = 16;
    static constexpr int stepSize = 32;

    /** Where a wire comes from: nothing, the pedal's input, or a node (0 and up). */
    static constexpr int noSource = -2;
    static constexpr int inputSource = -1;

    PatchPedal();

    struct Node
    {
        explicit Node (int index);

        custom_blocks::BlockParams block;

        /** The node's inputs, as source choices: 0 none, 1 the input, 2 + n node n.
            Only a Mix block uses its second input. */
        Parameter inA, inB;

        /** Where the node sits on the canvas, from 0 to 1 across and down. */
        Parameter x, y;

        std::vector<Parameter*> getParameters();

        BlockType getType() const noexcept   { return block.getType(); }
        int getSource (int input) const noexcept   { return (input == 0 ? inA : inB).getIndex() - 2; }
    };

    Node& getNode (int index) noexcept   { return *nodes[(size_t) index]; }

    /** The overall output level. */
    Parameter level { "patch.level", "Level", -24.0f, 12.0f, 0.0f, 0.1f, " dB" };

    /** What the output listens to, as a source choice (1, the input, at first). */
    Parameter output;

    //==============================================================================
    // Editing, for the message thread. These only turn knobs, like the UI does.

    /** The nodes in use, by index. */
    std::vector<int> getNodesInUse() const;

    /** Adds a block at (x, y). A sound block is wired in just before the
        output; a modulator listens to the input and moves the block feeding
        the output, when it can. Returns the node, or -1 when all are in use. */
    int addNode (BlockType, float x, float y);

    /** Removes a node. Whatever listened to it listens to the node's own
        first input instead, so a chain closes up around the gap. */
    void removeNode (int node);

    /** Wires `source` (noSource to disconnect) into a node's input 0 or 1.
        Refuses modulators as sources (they move knobs, not sound). */
    bool connect (int source, int node, int input);
    bool connectOutput (int source);
    int getOutputSource() const noexcept   { return output.getIndex() - 2; }

    /** Points a modulator at a node (or -1 for none). Refuses nodes it
        cannot move. */
    bool setTarget (int modulator, int target);

    /** True when a node's input is a loop: its source runs after it. */
    bool isLoopWire (int node, int input) const;

    /** Message thread. The level change in dB at each frequency of the whole
        patch as its knobs are set now, measured with a quiet impulse (with
        modulators held still). `hasBlocks` is false for an empty patch. */
    std::vector<float> getResponseDb (const std::vector<float>& frequencies, bool& hasBlocks);

    //==============================================================================
    std::vector<Parameter*> getControls() override;
    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* samples, int numSamples) noexcept override;

private:
    struct Structure
    {
        std::array<BlockType, maxNodes> types {};
        std::array<int, maxNodes> slopes {}, sourceA {}, sourceB {}, targets {};
        int out = inputSource;

        bool operator== (const Structure&) const = default;
    };

    Structure readStructure() const noexcept;
    void applyStructure (const Structure&) noexcept;
    void runStep (const float* input, float* output, int numSamples) noexcept;
    const float* sourceBuffer (int source, int forNode, float* scratch) noexcept;

    /** The order nodes run in: inputs first where possible; a loop is broken
        at its lowest-numbered node. */
    static int computeOrder (const Structure&, std::array<int, maxNodes>& order) noexcept;

    std::array<std::unique_ptr<Node>, maxNodes> nodes;
    std::array<custom_blocks::BlockRunner, maxNodes> runners;
    Structure applied;
    std::array<int, maxNodes> order {};
    std::array<int, maxNodes> position {};   // where each node is in `order`
    std::array<uint32_t, maxNodes> modulatedBy {};   // per node, a bit for each modulator moving it
    int numInOrder = 0;

    std::array<std::array<float, stepSize>, maxNodes> outputs {};
    std::array<float, stepSize> inputStep {}, silence {}, modulation {}, loopScratch {}, loopScratchB {};

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetMix { 1.0f };
    juce::SmoothedValue<float> outputGain { 1.0f };
    std::vector<float> dry;
};
