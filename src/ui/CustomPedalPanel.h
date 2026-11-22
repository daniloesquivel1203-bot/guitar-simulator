#pragma once

#include "../engine/effects/CustomPedal.h"
#include "TonePanels.h"

#include <optional>

/** The Custom pedal's panel: a row of the pedal's blocks in signal order,
    with a + to add one, and below it the controls of the selected block.

    Click a block to show its controls. Drag a block to move it. Right-click
    a block, or use the Remove block button, to take it out.
*/
class CustomPedalPanel final : public TonePanel,
                               private juce::Timer
{
public:
    CustomPedalPanel (CustomPedal&, std::function<void()> onRemovePedal);

    void resized() override;
    void refreshPower() override   { power.refresh(); }

private:
    /** The row of block tiles. */
    class BlockRow final : public juce::Component,
                           public juce::SettableTooltipClient
    {
    public:
        explicit BlockRow (CustomPedalPanel&);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        struct Drag
        {
            int position;
            int grabOffset;
            int mouseX;
            bool moved;
        };

        int numTiles() const;                           // the blocks, plus a + tile while there is room
        bool isAddTile (int tile) const;
        juce::Rectangle<int> getTileBounds (int tile) const;
        int findTileAt (juce::Point<int>) const;
        int findDropPosition (int x) const;
        void paintTile (juce::Graphics&, int tile, juce::Rectangle<float>) const;

        CustomPedalPanel& owner;
        std::optional<Drag> drag;
    };

    void refreshBlocks();
    void selectBlock (int slot);
    void showAddMenu (juce::Rectangle<int> screenArea);
    void showBlockMenu (int slot);
    void removeBlock (int slot);
    void timerCallback() override;   // keeps the band readout in step with the knobs

    CustomPedal& pedal;
    ParameterToggle power;
    juce::TextButton removePedalButton { "Remove" };
    BlockRow blockRow { *this };
    ParameterKnob level;
    juce::Label hint, description, bandReadout;
    juce::TextButton removeBlockButton { "Remove block" };

    std::vector<int> blocks;   // slots in signal order
    int selectedSlot = -1;

    juce::OwnedArray<ParameterKnob> knobs;
    juce::OwnedArray<ParameterChoice> choices;
    juce::OwnedArray<ParameterToggle> switches;
};
