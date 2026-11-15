#pragma once

#include "../engine/Parameter.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

/** The signal chain drawn as a row of tiles, one per pedal, amp or cabinet.

    Click a tile to show its controls. Click the light on a tile to switch that
    block on or off.
*/
class ChainStrip final : public juce::Component
{
public:
    struct Block
    {
        juce::String shortName;
        Parameter* enabled;
    };

    std::function<void (int)> onSelect;
    std::function<void (int)> onToggle;

    void setBlocks (std::vector<Block> newBlocks)
    {
        blocks = std::move (newBlocks);
        repaint();
    }

    int getSelected() const noexcept   { return selected; }

    void setSelected (int index)
    {
        selected = index;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        for (int i = 0; i < (int) blocks.size(); ++i)
        {
            const auto tile = getTileBounds (i).toFloat();
            const bool on = blocks[(size_t) i].enabled->isOn();
            const bool isSelected = i == selected;

            g.setColour (isSelected ? juce::Colour (0xff36404a) : juce::Colour (0xff2b3238));
            g.fillRoundedRectangle (tile, 6.0f);

            if (isSelected)
            {
                g.setColour (juce::Colour (0xff42a2c8));
                g.drawRoundedRectangle (tile.reduced (0.75f), 6.0f, 1.5f);
            }

            g.setColour (on ? juce::Colour (0xff3ecf8e) : juce::Colour (0xff4a545c));
            g.fillEllipse (getLightBounds (i).toFloat().withSizeKeepingCentre (10.0f, 10.0f));

            g.setColour (juce::Colours::white.withAlpha (on ? 0.95f : 0.45f));
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (blocks[(size_t) i].shortName, tile.withTrimmedTop (22.0f), juce::Justification::centred);

            // A small arrow shows the direction the signal flows.
            if (i + 1 < (int) blocks.size())
            {
                const auto x = tile.getRight() + gap * 0.5f;
                const auto y = tile.getCentreY();
                juce::Path arrow;
                arrow.addTriangle (x - 2.5f, y - 4.0f, x - 2.5f, y + 4.0f, x + 2.5f, y);
                g.setColour (juce::Colours::white.withAlpha (0.25f));
                g.fillPath (arrow);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        for (int i = 0; i < (int) blocks.size(); ++i)
        {
            if (! getTileBounds (i).contains (event.getPosition()))
                continue;

            if (getLightBounds (i).contains (event.getPosition()))
            {
                auto* enabled = blocks[(size_t) i].enabled;
                enabled->set (enabled->isOn() ? 0.0f : 1.0f);

                if (onToggle != nullptr)
                    onToggle (i);
            }
            else if (onSelect != nullptr)
            {
                onSelect (i);
            }

            repaint();
            return;
        }
    }

private:
    static constexpr float gap = 12.0f;

    juce::Rectangle<int> getTileBounds (int index) const
    {
        const auto count = (float) juce::jmax (1, (int) blocks.size());
        const auto width = ((float) getWidth() - gap * (count - 1.0f)) / count;
        return juce::Rectangle<float> ((float) index * (width + gap), 0.0f, width, (float) getHeight()).toNearestInt();
    }

    /** The clickable area around the on/off light, generous enough to hit easily. */
    juce::Rectangle<int> getLightBounds (int index) const
    {
        return getTileBounds (index).removeFromTop (26).withSizeKeepingCentre (30, 26);
    }

    std::vector<Block> blocks;
    int selected = 0;
};
