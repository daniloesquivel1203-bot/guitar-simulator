#pragma once

#include "../engine/CustomPedalLibrary.h"
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
    /** `onRenamed` runs after the pedal is saved under a new name. */
    CustomPedalPanel (CustomPedal&, std::function<void()> onRemovePedal, CustomPedalLibrary&,
                      std::function<void()> onRenamed);
    ~CustomPedalPanel() override;

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

    class WidthKnob;

    /** The combined frequency response of the filter and gain blocks. */
    class ResponseGraph final : public juce::Component,
                                public juce::SettableTooltipClient
    {
    public:
        ResponseGraph();
        void setCurve (std::vector<float> newDb, bool show);
        void paint (juce::Graphics&) override;

        static const std::vector<float>& getFrequencies();

    private:
        std::vector<float> db;
        bool showing = false;
    };

    ResponseGraph graph;
    juce::String lastResponseKey;
    void refreshGraph();

    void refreshBlocks();
    void timerCallback() override;   // keeps the band's edges in step with the knobs
    void selectBlock (int slot);
    void showAddMenu (juce::Rectangle<int> screenArea);
    void showBlockMenu (int slot);
    void removeBlock (int slot);

    CustomPedal& pedal;
    ParameterToggle power;
    juce::TextButton removePedalButton { "Remove" };
    juce::TextButton saveButton { "Save as my pedal" };
    CustomPedalLibrary& library;
    std::function<void()> onRenamed;
    void saveAsMyPedal();
    BlockRow blockRow { *this };
    ParameterKnob level;
    juce::Label hint, description, bandRange;

    // For modulators: which block they move.
    juce::ComboBox targetBox;
    juce::Label targetCaption;
    void refreshTargetChoices();
    juce::TextButton removeBlockButton { "Remove block" };

    std::vector<int> blocks;   // slots in signal order
    int selectedSlot = -1;

    juce::OwnedArray<ParameterKnob> knobs;
    juce::OwnedArray<WidthKnob> widthKnobs;
    juce::OwnedArray<ParameterChoice> choices;
    juce::OwnedArray<ParameterToggle> switches;
};
