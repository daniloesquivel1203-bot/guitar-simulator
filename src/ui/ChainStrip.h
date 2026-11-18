#pragma once

#include "../engine/Parameter.h"
#include "../engine/Pedalboard.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>
#include <vector>

/** The signal chain drawn as a row of tiles: the pedals before the amp, the
    amp, the cabinet, then the pedals after the amp. Each group of pedals ends
    with a + tile for adding one.

    Click a tile to show its controls. Click the light on a tile to switch that
    block on or off. Drag a pedal to move it, including to the other side of
    the amp. Right-click a pedal for more options.
*/
class ChainStrip final : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    enum class Kind { pedal, amp, cabinet, add };

    struct Tile
    {
        Kind kind;
        Pedalboard::Side side;   // for pedals and + tiles
        int index;               // a pedal's position on its side
        juce::String shortName;
        Parameter* enabled;      // nullptr for + tiles
    };

    std::function<void (int tile)> onSelect;
    std::function<void (int tile)> onToggle;
    std::function<void (int tile)> onPedalMenu;
    std::function<void (Pedalboard::Side, juce::Rectangle<int> screenArea)> onAdd;
    std::function<void (Pedalboard::Side fromSide, int fromIndex, Pedalboard::Side toSide, int toIndex)> onMove;

    ChainStrip()
    {
        setTooltip ("Click a block to show its controls, or its light to switch it on or off. "
                    "Drag a pedal to move it. Right-click a pedal to remove it.");
    }

    void setTiles (std::vector<Tile> newTiles)
    {
        tiles = std::move (newTiles);
        drag.reset();
        repaint();
    }

    const std::vector<Tile>& getTiles() const noexcept   { return tiles; }

    /** -1 when nothing is selected, for example while the tuner is showing. */
    int getSelected() const noexcept   { return selected; }

    void setSelected (int index)
    {
        selected = index;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        for (int i = 0; i < (int) tiles.size(); ++i)
        {
            const bool beingDragged = drag.has_value() && drag->moved && drag->tile == i;

            if (beingDragged)
            {
                // Leave a faint outline where the pedal came from.
                g.setColour (juce::Colours::white.withAlpha (0.12f));
                g.drawRoundedRectangle (getTileBounds (i).toFloat().reduced (0.75f), 6.0f, 1.0f);
            }
            else
            {
                paintTile (g, i, getTileBounds (i).toFloat());
            }

            // A small arrow shows the direction the signal flows.
            if (i + 1 < (int) tiles.size())
            {
                const auto tile = getTileBounds (i).toFloat();
                const auto x = tile.getRight() + gap * 0.5f;
                const auto y = tile.getCentreY();
                juce::Path arrow;
                arrow.addTriangle (x - 2.5f, y - 4.0f, x - 2.5f, y + 4.0f, x + 2.5f, y);
                g.setColour (juce::Colours::white.withAlpha (0.25f));
                g.fillPath (arrow);
            }
        }

        if (drag.has_value() && drag->moved)
        {
            auto floating = getTileBounds (drag->tile).toFloat();
            floating.setX ((float) (drag->mouseX - drag->grabOffset));
            g.setOpacity (0.6f);
            paintTile (g, drag->tile, floating);
            g.setOpacity (1.0f);

            // Drawn on top of the dragged tile so the drop point is always visible.
            if (const auto target = findDropTarget (drag->mouseX))
            {
                g.setColour (Theme::accent);
                g.fillRoundedRectangle ((float) target->x - 1.5f, 0.0f, 3.0f, (float) getHeight(), 1.5f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        drag.reset();
        const auto i = findTileAt (event.getPosition());

        if (i < 0)
            return;

        const auto& tile = tiles[(size_t) i];

        if (tile.kind == Kind::add)
        {
            if (onAdd != nullptr)
                onAdd (tile.side, localAreaToGlobal (getTileBounds (i)));

            return;
        }

        if (event.mods.isPopupMenu())
        {
            if (tile.kind == Kind::pedal && onPedalMenu != nullptr)
                onPedalMenu (i);

            return;
        }

        if (getLightBounds (i).contains (event.getPosition()))
        {
            tile.enabled->set (tile.enabled->isOn() ? 0.0f : 1.0f);

            if (onToggle != nullptr)
                onToggle (i);

            repaint();
            return;
        }

        if (onSelect != nullptr)
            onSelect (i);

        if (tile.kind == Kind::pedal)
            drag = Drag { i, event.x - getTileBounds (i).getX(), event.x, false };

        repaint();
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (! drag.has_value())
            return;

        drag->mouseX = event.x;
        drag->moved = drag->moved || event.getDistanceFromDragStart() > 5;
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! drag.has_value())
            return;

        const auto finished = *drag;
        drag.reset();
        repaint();

        if (! finished.moved || onMove == nullptr)
            return;

        if (const auto target = findDropTarget (finished.mouseX))
        {
            const auto& tile = tiles[(size_t) finished.tile];
            auto toIndex = target->index;

            // The index is counted after the pedal has left its old place.
            if (target->side == tile.side && toIndex > tile.index)
                --toIndex;

            if (target->side != tile.side || toIndex != tile.index)
                onMove (tile.side, tile.index, target->side, toIndex);
        }
    }

private:
    static constexpr float gap = 12.0f;
    static constexpr int addTileWidth = 30;
    static constexpr int maxTileWidth = 110;

    struct Drag
    {
        int tile;
        int grabOffset;
        int mouseX;
        bool moved;
    };

    struct DropTarget
    {
        Pedalboard::Side side;
        int index;
        int x;
    };

    void paintTile (juce::Graphics& g, int i, juce::Rectangle<float> area) const
    {
        const auto& tile = tiles[(size_t) i];

        if (tile.kind == Kind::add)
        {
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            g.drawRoundedRectangle (area.reduced (0.75f), 6.0f, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.setFont (juce::FontOptions (20.0f));
            g.drawText ("+", area, juce::Justification::centred);
            return;
        }

        const bool on = tile.enabled->isOn();
        const bool isSelected = i == selected;
        const bool isAmpOrCab = tile.kind != Kind::pedal;

        g.setColour (isSelected ? Theme::raised : (isAmpOrCab ? juce::Colour (0xff313a42) : Theme::panel));
        g.fillRoundedRectangle (area, 6.0f);

        if (isSelected)
        {
            g.setColour (Theme::accent);
            g.drawRoundedRectangle (area.reduced (0.75f), 6.0f, 1.5f);
        }

        const auto light = area.withHeight (26.0f).withSizeKeepingCentre (10.0f, 10.0f);
        g.setColour (on ? Theme::good : juce::Colour (0xff4a545c));
        g.fillEllipse (light);

        g.setColour (juce::Colours::white.withAlpha (on ? 0.95f : 0.45f));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawFittedText (tile.shortName, area.withTrimmedTop (22.0f).toNearestInt().reduced (2, 0),
                          juce::Justification::centred, 1, 0.7f);
    }

    int findTileAt (juce::Point<int> position) const
    {
        for (int i = 0; i < (int) tiles.size(); ++i)
            if (getTileBounds (i).contains (position))
                return i;

        return -1;
    }

    /** The insertion point nearest to `x`: before any pedal, or before a + tile
        (which means the end of that side). There is none between amp and cabinet. */
    std::optional<DropTarget> findDropTarget (int x) const
    {
        std::optional<DropTarget> best;
        int bestDistance = std::numeric_limits<int>::max();
        const auto& dragged = tiles[(size_t) drag->tile];

        for (int i = 0; i < (int) tiles.size(); ++i)
        {
            const auto& tile = tiles[(size_t) i];

            if (tile.kind != Kind::pedal && tile.kind != Kind::add)
                continue;

            // A full side only accepts moves within itself.
            if (tile.side != dragged.side && countPedals (tile.side) >= Pedalboard::maxPedalsPerSide)
                continue;

            const auto candidateX = juce::roundToInt ((float) getTileBounds (i).getX() - gap * 0.5f);
            const auto distance = std::abs (candidateX - x);

            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = DropTarget { tile.side, tile.kind == Kind::add ? countPedals (tile.side) : tile.index,
                                    candidateX };
            }
        }

        return best;
    }

    int countPedals (Pedalboard::Side side) const
    {
        return (int) std::count_if (tiles.begin(), tiles.end(),
                                    [side] (const Tile& t) { return t.kind == Kind::pedal && t.side == side; });
    }

    juce::Rectangle<int> getTileBounds (int index) const
    {
        const auto numAdd = (int) std::count_if (tiles.begin(), tiles.end(),
                                                 [] (const Tile& t) { return t.kind == Kind::add; });
        const auto numWide = juce::jmax (1, (int) tiles.size() - numAdd);
        const auto totalGaps = gap * (float) juce::jmax (0, (int) tiles.size() - 1);
        const auto wide = juce::jmin ((float) maxTileWidth,
                                      ((float) getWidth() - totalGaps - (float) (numAdd * addTileWidth)) / (float) numWide);

        const auto rowWidth = wide * (float) numWide + (float) (numAdd * addTileWidth) + totalGaps;
        auto x = juce::jmax (0.0f, ((float) getWidth() - rowWidth) * 0.5f);

        for (int i = 0; i < index; ++i)
            x += (tiles[(size_t) i].kind == Kind::add ? (float) addTileWidth : wide) + gap;

        const auto width = tiles[(size_t) index].kind == Kind::add ? (float) addTileWidth : wide;
        return juce::Rectangle<float> (x, 0.0f, width, (float) getHeight()).toNearestInt();
    }

    /** The clickable area around the on/off light, generous enough to hit easily. */
    juce::Rectangle<int> getLightBounds (int index) const
    {
        return getTileBounds (index).removeFromTop (26).withSizeKeepingCentre (30, 26);
    }

    std::vector<Tile> tiles;
    int selected = -1;
    std::optional<Drag> drag;
};
