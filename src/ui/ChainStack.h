#pragma once

#include "../engine/Parameter.h"
#include "../engine/Pedalboard.h"
#include "PedalLook.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>
#include <vector>

/** The signal chain drawn as a stack of Lego-style bricks, top to bottom: the
    pedals before the amp, the amp and cabinet as one block, then the pedals
    after the amp. An "Add pedal" brick ends the stack.

    Click a brick to show its controls. Click its light to switch it on or off.
    Drag a brick up or down to move it, including to the other side of the amp.
    Pedals dragged in from the pedal tray drop in wherever the orange line shows.
    Right-click a pedal for more options.
*/
class ChainStack final : public juce::Component,
                         public juce::DragAndDropTarget,
                         public juce::SettableTooltipClient
{
public:
    enum class Kind { pedal, amp, add };

    struct Tile
    {
        Kind kind;
        Pedalboard::Side side;   // for pedals
        int index;               // a pedal's position on its side
        juce::String name;
        juce::String detail;     // category for pedals; voicing and cab for the amp
        juce::Colour colour;
        Parameter* enabled;      // nullptr for the add brick
    };

    std::function<void (int tile)> onSelect;
    std::function<void (int tile)> onToggle;
    std::function<void (int tile)> onPedalMenu;
    std::function<void (juce::Rectangle<int> brick)> onAdd;
    std::function<void (Pedalboard::Side fromSide, int fromIndex, Pedalboard::Side toSide, int toIndex)> onMove;

    /** A pedal from the tray was dropped at this point. `description` is what the tray gave the drag. */
    std::function<void (const juce::var& description, Pedalboard::Side, int index)> onDropNew;

    ChainStack()
    {
        setTooltip ("Click a pedal to show it, or its light to switch it on or off. Drag to reorder. "
                    "Drag pedals in from the tray below. Right-click a pedal for more.");
    }

    void setTiles (std::vector<Tile> newTiles)
    {
        tiles = std::move (newTiles);
        drag.reset();
        setAmpDetail (ampDetail);
        repaint();
    }

    /** The amp brick's second line: its voicing and cabinet. */
    void setAmpDetail (const juce::String& text)
    {
        ampDetail = text;

        for (auto& tile : tiles)
        {
            if (tile.kind == Kind::amp && tile.detail != text)
            {
                tile.detail = text;
                repaint();
            }
        }
    }

    const std::vector<Tile>& getTiles() const noexcept   { return tiles; }

    /** -1 when nothing is selected, for example while the tuner is showing. */
    int getSelected() const noexcept   { return selected; }

    void setSelected (int index)
    {
        selected = index;
        repaint();
    }

    /** A brick's outline on screen, for placing a menu next to it. */
    juce::Rectangle<int> getBrickScreenBounds (int index) const
    {
        auto bounds = getTileBounds (index);

        if (index == selected)
            bounds = bounds.translated (8, 0);

        return localAreaToGlobal (bounds);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));

        for (const auto& [y, text] : getSectionLabels())
            g.drawText (text, juce::Rectangle<int> (0, y, getWidth(), labelHeight), juce::Justification::centredLeft);

        for (int i = 0; i < (int) tiles.size(); ++i)
        {
            const bool beingDragged = drag.has_value() && drag->moved && drag->tile == i;

            if (beingDragged)
            {
                g.setColour (juce::Colours::white.withAlpha (0.15f));
                g.drawRoundedRectangle (getTileBounds (i).toFloat().reduced (0.75f), 5.0f, 1.0f);
            }
            else
            {
                paintTile (g, i, getTileBounds (i).toFloat());
            }
        }

        if (drag.has_value() && drag->moved)
        {
            auto floating = getTileBounds (drag->tile).toFloat();
            floating.setY ((float) (drag->mouseY - drag->grabOffset));
            g.setOpacity (0.75f);
            paintTile (g, drag->tile, floating.translated (8.0f, 0.0f));
            g.setOpacity (1.0f);
        }

        if (dropLineY.has_value())
        {
            const auto y = (float) *dropLineY;
            g.setColour (Theme::warning.withAlpha (0.35f));
            g.fillRoundedRectangle (0.0f, y - 4.0f, (float) getWidth(), 8.0f, 4.0f);
            g.setColour (Theme::warning);
            g.fillRoundedRectangle (0.0f, y - 2.0f, (float) getWidth(), 4.0f, 2.0f);
            g.fillEllipse (0.0f, y - 6.0f, 12.0f, 12.0f);
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
                onAdd (getTileBounds (i));

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
            drag = Drag { i, event.y - getTileBounds (i).getY(), event.y, false };

        repaint();
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (! drag.has_value())
            return;

        drag->mouseY = event.y;
        drag->moved = drag->moved || event.getDistanceFromDragStart() > 5;

        if (drag->moved)
        {
            const auto target = findDropTarget (event.y, tiles[(size_t) drag->tile].side);
            dropLineY = target.has_value() ? std::optional<int> (target->y) : std::nullopt;
        }

        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dropLineY.reset();

        if (! drag.has_value())
            return;

        const auto finished = *drag;
        drag.reset();
        repaint();

        if (! finished.moved || onMove == nullptr)
            return;

        const auto& tile = tiles[(size_t) finished.tile];

        if (const auto target = findDropTarget (finished.mouseY, tile.side))
        {
            auto toIndex = target->index;

            // The index is counted after the pedal has left its old place.
            if (target->side == tile.side && toIndex > tile.index)
                --toIndex;

            if (target->side != tile.side || toIndex != tile.index)
                onMove (tile.side, tile.index, target->side, toIndex);
        }
    }

    //==============================================================================
    bool isInterestedInDragSource (const SourceDetails& details) override
    {
        return details.description.toString().startsWith ("pedal:");
    }

    void itemDragEnter (const SourceDetails& details) override   { itemDragMove (details); }

    void itemDragMove (const SourceDetails& details) override
    {
        const auto target = findDropTarget (details.localPosition.y, std::nullopt);
        dropLineY = target.has_value() ? std::optional<int> (target->y) : std::nullopt;
        repaint();
    }

    void itemDragExit (const SourceDetails&) override
    {
        dropLineY.reset();
        repaint();
    }

    void itemDropped (const SourceDetails& details) override
    {
        dropLineY.reset();
        repaint();

        if (const auto target = findDropTarget (details.localPosition.y, std::nullopt); target.has_value() && onDropNew != nullptr)
            onDropNew (details.description, target->side, target->index);
    }

    /** The point a new pedal goes when clicked in the tray: right after the
        selected pedal, or at the end of the pedals before the amp. */
    std::optional<std::pair<Pedalboard::Side, int>> getInsertPointAfterSelection() const
    {
        if (juce::isPositiveAndBelow (selected, (int) tiles.size()))
        {
            const auto& tile = tiles[(size_t) selected];

            if (tile.kind == Kind::pedal && countPedals (tile.side) < Pedalboard::maxPedalsPerSide)
                return std::pair { tile.side, tile.index + 1 };

            if (tile.kind == Kind::amp && countPedals (Pedalboard::afterAmp) < Pedalboard::maxPedalsPerSide)
                return std::pair { Pedalboard::afterAmp, 0 };
        }

        if (countPedals (Pedalboard::beforeAmp) < Pedalboard::maxPedalsPerSide)
            return std::pair { Pedalboard::beforeAmp, countPedals (Pedalboard::beforeAmp) };

        if (countPedals (Pedalboard::afterAmp) < Pedalboard::maxPedalsPerSide)
            return std::pair { Pedalboard::afterAmp, countPedals (Pedalboard::afterAmp) };

        return std::nullopt;
    }

private:
    static constexpr int labelHeight = 18;
    static constexpr int maxBrickHeight = 40;
    static constexpr int minBrickHeight = 24;
    static constexpr int ampExtra = 18;

    struct Drag
    {
        int tile;
        int grabOffset;
        int mouseY;
        bool moved;
    };

    struct DropTarget
    {
        Pedalboard::Side side;
        int index;
        int y;
    };

    void paintTile (juce::Graphics& g, int i, juce::Rectangle<float> area) const
    {
        const auto& tile = tiles[(size_t) i];

        if (tile.kind == Kind::add)
        {
            g.setColour (juce::Colours::white.withAlpha (0.3f));
            const float dashes[] = { 6.0f, 4.0f };
            juce::Path outline;
            outline.addRoundedRectangle (area.reduced (1.0f), 5.0f);
            juce::Path dashed;
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, outline, dashes, 2);
            g.fillPath (dashed);
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText ("+ Add pedal", area, juce::Justification::centred);
            return;
        }

        const bool on = tile.enabled->isOn();
        const bool isSelected = i == selected;

        if (isSelected)
            area = area.translated (8.0f, 0.0f);

        if (tile.kind == Kind::amp)
        {
            pedal_look::paintTolex (g, area, 6.0f);
            g.setColour (pedal_look::AmpKnobs::gold.withAlpha (isSelected ? 1.0f : 0.7f));
            g.drawRoundedRectangle (area.reduced (1.0f), 6.0f, isSelected ? 2.0f : 1.5f);

            auto inner = area.reduced (10.0f, 7.0f);
            auto grill = inner.removeFromLeft (inner.getHeight() * 1.3f);
            pedal_look::paintGrill (g, grill, 2);
            inner.removeFromLeft (10.0f);

            g.setColour (pedal_look::AmpKnobs::gold);
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            g.drawText (tile.name, inner.removeFromTop (inner.getHeight() * 0.5f), juce::Justification::bottomLeft);
            g.setColour (pedal_look::AmpKnobs::gold.withAlpha (0.7f));
            g.setFont (juce::FontOptions (11.0f));
            g.drawFittedText (tile.detail, inner.withTrimmedRight (24.0f).toNearestInt(), juce::Justification::topLeft, 1, 0.8f);
        }
        else
        {
            auto face = tile.colour;

            if (! on)
                face = face.withMultipliedSaturation (0.45f).withMultipliedBrightness (0.6f);

            pedal_look::paintBrick (g, area, face);

            const auto ink = pedal_look::inkFor (face);
            auto text = area.reduced (12.0f, 0.0f).withTrimmedRight (24.0f);
            g.setColour (ink);
            g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
            const auto nameWidth = juce::jmin (text.getWidth() * 0.6f, (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), tile.name) + 10.0f);
            g.drawFittedText (tile.name, text.removeFromLeft (nameWidth).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
            g.setColour (ink.withAlpha (0.6f));
            g.setFont (juce::FontOptions (10.0f));
            g.drawFittedText (tile.detail.toUpperCase(), text.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        }

        if (isSelected)
        {
            g.setColour (juce::Colours::white);
            g.drawRoundedRectangle (area.expanded (1.0f), 6.0f, 2.0f);

            juce::Path arrow;
            const auto y = area.getCentreY();
            const auto x = area.getRight() + 5.0f;
            arrow.addTriangle (x, y - 6.0f, x, y + 6.0f, x + 6.0f, y);
            g.fillPath (arrow);
        }

        const auto light = getLightBounds (i).toFloat().translated (isSelected ? 8.0f : 0.0f, 0.0f);
        pedal_look::paintLed (g, light.getCentre(), 9.0f, on,
                              tile.kind == Kind::amp ? juce::Colour (0xffff3b30) : juce::Colour (0xff3cff6e));
    }

    int findTileAt (juce::Point<int> position) const
    {
        for (int i = 0; i < (int) tiles.size(); ++i)
            if (getTileBounds (i).expanded (0, 3).contains (position))
                return i;

        return -1;
    }

    /** The insertion point nearest to `y`. With `movingFrom` set, a full side
        only accepts moves within itself. */
    std::optional<DropTarget> findDropTarget (int y, std::optional<Pedalboard::Side> movingFrom) const
    {
        std::vector<DropTarget> candidates;

        for (auto side : { Pedalboard::beforeAmp, Pedalboard::afterAmp })
        {
            const auto count = countPedals (side);

            if (count >= Pedalboard::maxPedalsPerSide && movingFrom != side)
                continue;

            for (int index = 0; index <= count; ++index)
                candidates.push_back ({ side, index, getInsertY (side, index) });
        }

        std::optional<DropTarget> best;
        int bestDistance = std::numeric_limits<int>::max();

        for (const auto& c : candidates)
        {
            if (const auto distance = std::abs (c.y - y); distance < bestDistance)
            {
                bestDistance = distance;
                best = c;
            }
        }

        return best;
    }

    /** The y of the gap before pedal `index` on `side` (or after the last one). */
    int getInsertY (Pedalboard::Side side, int index) const
    {
        int lastBottom = -1;

        for (int i = 0; i < (int) tiles.size(); ++i)
        {
            const auto& tile = tiles[(size_t) i];

            if (side == Pedalboard::beforeAmp && tile.kind == Kind::amp)
                return lastBottom >= 0 ? lastBottom + gap() / 2 : getTileBounds (i).getY() - gap() / 2 - 2;

            if (tile.kind == Kind::pedal && tile.side == side && tile.index == index)
                return getTileBounds (i).getY() - gap() / 2 - 2;

            if (tile.kind == Kind::pedal && tile.side == side)
                lastBottom = getTileBounds (i).getBottom();

            if (side == Pedalboard::afterAmp && tile.kind == Kind::amp)
                lastBottom = getTileBounds (i).getBottom();
        }

        return lastBottom + gap() / 2;
    }

    int countPedals (Pedalboard::Side side) const
    {
        return (int) std::count_if (tiles.begin(), tiles.end(),
                                    [side] (const Tile& t) { return t.kind == Kind::pedal && t.side == side; });
    }

    /** Bricks shrink when the stack is full, so everything fits without scrolling. */
    int brickHeight() const
    {
        const auto numBricks = (int) tiles.size();
        const auto available = getHeight() - 2 * labelHeight - 2 * 6 - ampExtra;
        const auto perBrick = numBricks > 0 ? available / numBricks : maxBrickHeight;
        return juce::jlimit (minBrickHeight, maxBrickHeight, perBrick - gap());
    }

    int gap() const   { return 9; }

    std::vector<std::pair<int, juce::String>> getSectionLabels() const
    {
        std::vector<std::pair<int, juce::String>> labels { { 0, "BEFORE AMP" } };

        for (int i = 0; i < (int) tiles.size(); ++i)
            if (tiles[(size_t) i].kind == Kind::amp)
                labels.push_back ({ getTileBounds (i).getBottom() + 8, "AFTER AMP" });

        return labels;
    }

    juce::Rectangle<int> getTileBounds (int index) const
    {
        const auto h = brickHeight();
        auto y = labelHeight + 6;

        for (int i = 0; i <= index; ++i)
        {
            const auto& tile = tiles[(size_t) i];
            const auto height = tile.kind == Kind::amp ? h + ampExtra : h;

            if (tile.kind == Kind::amp)
                y += 4;

            if (i == index)
                return { 0, y, getWidth() - 18, height };

            y += height + gap();

            if (tile.kind == Kind::amp)
                y += labelHeight + 4;
        }

        return {};
    }

    /** The clickable area around the light, generous enough to hit easily. */
    juce::Rectangle<int> getLightBounds (int index) const
    {
        return getTileBounds (index).removeFromRight (30);
    }

    std::vector<Tile> tiles;
    int selected = -1;
    std::optional<Drag> drag;
    std::optional<int> dropLineY;
    juce::String ampDetail;
};
