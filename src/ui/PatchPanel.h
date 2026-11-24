#pragma once

#include "../engine/effects/PatchPedal.h"
#include "BlockEditorParts.h"
#include "TonePanels.h"

#include <optional>

/** The Patch pedal's panel: a canvas where blocks sit wherever you put them,
    joined by wires, and below it the controls of the selected block.

    Drag a block to move it. Drag from a block's output (the dot on its right)
    to another block's input (the dot on its left) to wire them; drag a wire's
    end off an input to unplug it. A modulator's orange dot wires it to the
    block it moves. Right-click a block to remove it, or the empty canvas to
    add one there.
*/
class PatchPanel final : public TonePanel,
                         private juce::Timer
{
public:
    PatchPanel (PatchPedal&, std::function<void()> onRemovePedal);
    ~PatchPanel() override;

    void resized() override;
    void refreshPower() override   { power.refresh(); }

private:
    /** The canvas of blocks and wires. */
    class Canvas final : public juce::Component,
                         public juce::SettableTooltipClient
    {
    public:
        explicit Canvas (PatchPanel&);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;

        /** Where a block of the patch sits now. */
        juce::Rectangle<float> getNodeBounds (int node) const;

    private:
        /** What is under the mouse. */
        struct Hit
        {
            enum Kind { nothing, body, nodeInput, nodeOutput, modOutput, inputPill, outputPill } kind = nothing;
            int node = -1;
            int input = 0;   // for nodeInput: 0, or 1 for a Mix's second input
        };

        struct Wiring
        {
            bool fromModulator = false;
            int source = PatchPedal::noSource;   // the node (or the input) the wire comes from
            juce::Point<float> end;
        };

        struct Moving
        {
            int node;
            juce::Point<float> grabOffset;
            bool moved = false;
        };

        Hit findHit (juce::Point<float>) const;
        juce::Rectangle<float> getInputPill() const;
        juce::Rectangle<float> getOutputPill() const;
        juce::Point<float> getOutputPort (int source) const;            // source may be the input
        juce::Point<float> getInputPort (int node, int input) const;
        juce::Point<float> getModPort (int node) const;
        juce::Point<float> getOutPillPort() const;
        int numInputs (int node) const;
        bool hasOutput (int node) const;
        void finishWire (const Wiring&, const Hit& target);
        void paintNode (juce::Graphics&, int node) const;
        static void drawWire (juce::Graphics&, juce::Point<float> from, juce::Point<float> to, juce::Colour, bool dashed);

        PatchPanel& owner;
        std::optional<Wiring> wiring;
        std::optional<Moving> moving;
        Hit hover;
    };

    void refreshNodes();
    void timerCallback() override;
    void selectNode (int node);
    void showAddMenu (juce::Point<float> where);
    void showNodeMenu (int node);
    void removeNode (int node);
    void refreshGraph();
    void refreshTargetChoices();

    PatchPedal& pedal;
    ParameterToggle power;
    juce::TextButton removePedalButton { "Remove" };
    juce::TextButton addButton { "Add block" };
    Canvas canvas { *this };
    ParameterKnob level;
    juce::Label description;

    ResponseGraph graph { "How the whole patch changes the level across the frequency range, from 20 Hz on the left "
                          "to 20 kHz on the right, measured with the modulators held still. The middle line is no "
                          "change; the dotted lines are 12 dB up and down." };
    juce::String lastResponseKey;

    juce::ComboBox targetBox;
    juce::Label targetCaption;

    std::vector<int> nodes;   // in use, by index
    int selectedNode = -1;

    juce::OwnedArray<ParameterKnob> knobs;
    juce::OwnedArray<BlockWidthKnob> widthKnobs;
    juce::OwnedArray<ParameterChoice> choices;
    juce::OwnedArray<ParameterToggle> switches;
};
