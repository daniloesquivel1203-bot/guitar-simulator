#pragma once

#include "MiniPedal.h"
#include "TonePanels.h"

#include <functional>
#include <vector>

/** The whole signal chain at a glance, shown when the amp is picked: every
    pedal and the amp as small stompboxes, patched together in order, left to
    right and row by row.

    Click a pedal to open it, or its footswitch to switch it on or off.
    Right-click a pedal to remove it. */
class ChainOverview final : public TonePanel,
                            public juce::SettableTooltipClient,
                            private juce::Timer
{
public:
    ChainOverview() : TonePanel ({})
    {
        setTooltip ("Click a pedal to open it, or its footswitch to switch it on or off. Right-click a pedal to remove it.");
        startTimerHz (10);
    }

    /** `tile` is the chain stack's index of the block. */
    std::function<void (int tile)> onOpen;
    std::function<void (int tile, juce::Rectangle<int> screenArea)> onMenu;

    void setTiles (const std::vector<ChainStack::Tile>& newTiles)
    {
        tiles = newTiles;
        shown.clear();

        for (int i = 0; i < (int) tiles.size(); ++i)
            if (tiles[(size_t) i].kind != ChainStack::Kind::add)
                shown.push_back (i);

        hovered = -1;
        layout();
        repaint();
    }

    void refreshPower() override   { repaint(); }
    void resized() override        { layout(); }

    void paint (juce::Graphics& g) override
    {
        auto header = getLocalBounds().reduced (24, 0).removeFromTop (headerHeight).toFloat();
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("SIGNAL CHAIN", header.removeFromTop (header.getHeight() * 0.55f), juce::Justification::bottomLeft);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (shown.size() > 1 ? "Guitar in, left to right, out to your speakers. Click a pedal to open it."
                                     : "No pedals yet. Add some from the tray below.",
                    header, juce::Justification::topLeft);

        if (boxes.empty())
            return;

        // Cables first, so the pedals sit on top of their plugs.
        for (size_t i = 0; i + 1 < boxes.size(); ++i)
        {
            const auto from = mini_pedal::getJack (boxes[i], kindOf (i), false);
            const auto to = mini_pedal::getJack (boxes[i + 1], kindOf (i + 1), true);

            if (std::abs (boxes[i].getY() - boxes[i + 1].getY()) < 1.0f)
                mini_pedal::paintCable (g, from, to);
            else
                paintRowChange (g, from, to, (boxes[i].getBottom() + boxes[i + 1].getY()) / 2.0f);
        }

        for (size_t i = 0; i < boxes.size(); ++i)
        {
            const auto& tile = tiles[(size_t) shown[i]];
            mini_pedal::paintJack (g, boxes[i], tile.kind, true);
            mini_pedal::paintJack (g, boxes[i], tile.kind, false);
            mini_pedal::paint (g, boxes[i], tile);

            if ((int) i == hovered && tile.kind == ChainStack::Kind::pedal)
            {
                g.setColour (juce::Colours::white.withAlpha (0.8f));
                g.drawRoundedRectangle (boxes[i].expanded (3.0f), 10.0f, 1.5f);
            }
        }
    }

    void mouseMove (const juce::MouseEvent& event) override
    {
        const auto i = findAt (event.position);

        if (i != hovered)
        {
            hovered = i;
            setMouseCursor (i >= 0 && kindOf ((size_t) i) == ChainStack::Kind::pedal ? juce::MouseCursor::PointingHandCursor
                                                                                     : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hovered = -1;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        const auto i = findAt (event.position);

        if (i < 0 || kindOf ((size_t) i) != ChainStack::Kind::pedal)
            return;

        const auto tileIndex = shown[(size_t) i];
        const auto& tile = tiles[(size_t) tileIndex];

        if (event.mods.isPopupMenu())
        {
            if (onMenu != nullptr)
                onMenu (tileIndex, localAreaToGlobal (boxes[(size_t) i].toNearestInt()));

            return;
        }

        if (tile.enabled != nullptr && mini_pedal::getFootswitch (boxes[(size_t) i]).expanded (4.0f).contains (event.position))
        {
            tile.enabled->set (tile.enabled->isOn() ? 0.0f : 1.0f);
            repaint();
            return;
        }

        if (onOpen != nullptr)
            onOpen (tileIndex);
    }

private:
    static constexpr int headerHeight = 48;

    void timerCallback() override   { repaint(); }   // the lights follow switches flipped elsewhere

    ChainStack::Kind kindOf (size_t i) const   { return tiles[(size_t) shown[i]].kind; }

    int findAt (juce::Point<float> position) const
    {
        for (size_t i = 0; i < boxes.size(); ++i)
            if (mini_pedal::getOutline (boxes[i], kindOf (i)).expanded (4.0f).contains (position))
                return (int) i;

        return -1;
    }

    /** Picks the number of rows that lets the pedals be biggest, then places them. */
    void layout()
    {
        boxes.clear();
        const auto n = (int) shown.size();

        if (n == 0)
            return;

        auto area = getLocalBounds().toFloat().reduced (24.0f, 12.0f);
        area.removeFromTop ((float) headerHeight);

        float bestWidth = 0.0f;
        int bestRows = 1;

        for (int rows = 1; rows <= n; ++rows)
        {
            const auto cols = (n + rows - 1) / rows;
            // Each pedal needs its width plus room for a cable, and each row its height plus room to route the cable down.
            const auto byWidth = area.getWidth() / ((float) cols + (float) (cols - 1) * spacing);
            const auto byHeight = area.getHeight() / ((float) rows * aspect + (float) (rows - 1) * rowSpacing);
            const auto width = juce::jmin (maxWidth, byWidth, byHeight);

            if (width > bestWidth + 0.5f)
            {
                bestWidth = width;
                bestRows = rows;
            }
        }

        const auto cols = (n + bestRows - 1) / bestRows;
        const auto w = bestWidth, h = bestWidth * aspect;
        const auto rowStep = h + w * rowSpacing;
        const auto gridHeight = (float) bestRows * h + (float) (bestRows - 1) * w * rowSpacing;
        const auto top = area.getY() + juce::jmax (0.0f, (area.getHeight() - gridHeight) * 0.4f);

        for (int i = 0; i < n; ++i)
        {
            const auto row = i / cols;
            const auto inRow = juce::jmin (cols, n - row * cols);
            const auto rowWidth = (float) inRow * w + (float) (inRow - 1) * w * spacing;
            const auto x = area.getCentreX() - rowWidth / 2.0f + (float) (i % cols) * w * (1.0f + spacing);
            boxes.push_back ({ x, top + (float) row * rowStep, w, h });
        }
    }

    /** The cable from the end of one row to the start of the next: down, back across, and in. */
    void paintRowChange (juce::Graphics& g, juce::Point<float> from, juce::Point<float> to, float laneY) const
    {
        const auto rightX = from.x + 14.0f, leftX = to.x - 14.0f;

        juce::Path route;
        route.startNewSubPath (from);
        route.lineTo (rightX, from.y);
        route.lineTo (rightX, laneY);
        route.lineTo (leftX, laneY);
        route.lineTo (leftX, to.y);
        route.lineTo (to);
        route = route.createPathWithRoundedCorners (10.0f);

        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.strokePath (route, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                      juce::AffineTransform::translation (0.0f, 3.0f));
        g.setColour (juce::Colour (0xff111214));
        g.strokePath (route, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        for (auto p : { from, to })
        {
            g.setColour (juce::Colour (0xff3a3c42));
            g.fillRoundedRectangle (juce::Rectangle<float> (12.0f, 9.0f).withCentre (p), 2.0f);
        }
    }

    static constexpr float aspect = 1.6f;       // pedal height over width
    static constexpr float spacing = 0.4f;      // gap between pedals, over pedal width
    static constexpr float rowSpacing = 0.45f;  // gap between rows, over pedal width
    static constexpr float maxWidth = 120.0f;

    std::vector<ChainStack::Tile> tiles;
    std::vector<int> shown;                     // tile indices, in signal order
    std::vector<juce::Rectangle<float>> boxes;  // one per shown tile
    int hovered = -1;
};
