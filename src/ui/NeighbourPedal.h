#pragma once

#include "MiniPedal.h"

#include <functional>

/** A small, dimmed stompbox beside the open pedal: the block just before it
    (on the left) or just after it (on the right) in the signal chain. Click it
    to open that block instead. Right-click a pedal for its menu. */
class NeighbourPedal final : public juce::Component,
                             public juce::SettableTooltipClient
{
public:
    enum class Placement { left, right };

    explicit NeighbourPedal (Placement placementToUse) : placement (placementToUse)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    std::function<void()> onClick;
    std::function<void()> onMenu;

    void setTile (const ChainStack::Tile& tileToShow)
    {
        tile = tileToShow;
        setTooltip ("Show " + tile.name + (placement == Placement::left ? " (comes before this pedal)"
                                                                        : " (comes after this pedal)"));
        repaint();
    }

    /** The pedal's box, inside this component. */
    juce::Rectangle<float> getPedalBounds() const
    {
        auto area = getLocalBounds().toFloat().reduced (6.0f, 0.0f);
        area.removeFromTop (labelHeight);
        const auto w = juce::jmin (area.getWidth(), 120.0f);
        const auto h = juce::jmin (area.getHeight(), w * 1.6f);
        return juce::Rectangle<float> (w, h).withCentre ({ area.getCentreX(), area.getY() + area.getHeight() * 0.42f });
    }

    /** Where a cable plugs into the side facing the open pedal, in this component. */
    juce::Point<float> getJack() const
    {
        return mini_pedal::getJack (getPedalBounds(), tile.kind, placement == Placement::right);
    }

    void paint (juce::Graphics& g) override
    {
        const auto box = getPedalBounds();

        g.setColour (juce::Colours::white.withAlpha (hovering ? 0.6f : 0.35f));
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (placement == Placement::left ? juce::String::fromUTF8 ("\xe2\x97\x80 BEFORE") : juce::String::fromUTF8 ("AFTER \xe2\x96\xb6"),
                    juce::Rectangle<float> (0.0f, box.getY() - labelHeight, (float) getWidth(), (float) labelHeight - 4.0f),
                    juce::Justification::centred);

        mini_pedal::paintJack (g, box, tile.kind, placement == Placement::right);

        g.setOpacity (hovering ? 0.95f : 0.7f);
        mini_pedal::paint (g, box, tile);
        g.setOpacity (1.0f);
    }

    void mouseEnter (const juce::MouseEvent&) override   { hovering = true;  repaint(); }
    void mouseExit (const juce::MouseEvent&) override    { hovering = false; repaint(); }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (! getPedalBounds().expanded (6.0f).contains (event.position))
            return;

        if (event.mods.isPopupMenu())
        {
            if (tile.kind == ChainStack::Kind::pedal && onMenu != nullptr)
                onMenu();

            return;
        }

        if (onClick != nullptr)
            onClick();
    }

private:
    static constexpr int labelHeight = 22;

    Placement placement;
    ChainStack::Tile tile { ChainStack::Kind::pedal, Pedalboard::beforeAmp, -1, {}, {}, {}, nullptr };
    bool hovering = false;
};
