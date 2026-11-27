#pragma once

#include "ChainStack.h"
#include "PedalLook.h"

#include <juce_gui_basics/juce_gui_basics.h>

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
        const auto box = getPedalBounds();
        return { placement == Placement::left ? box.getRight() + 4.0f : box.getX() - 4.0f, box.getY() + box.getHeight() * 0.3f };
    }

    void paint (juce::Graphics& g) override
    {
        const auto box = getPedalBounds();
        const bool on = tile.enabled != nullptr && tile.enabled->isOn();

        g.setColour (juce::Colours::white.withAlpha (hovering ? 0.6f : 0.35f));
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (placement == Placement::left ? juce::String::fromUTF8 ("\xe2\x97\x80 BEFORE") : juce::String::fromUTF8 ("AFTER \xe2\x96\xb6"),
                    juce::Rectangle<float> (0.0f, box.getY() - labelHeight, (float) getWidth(), (float) labelHeight - 4.0f),
                    juce::Justification::centred);

        // The jack on the side facing the open pedal.
        {
            const juce::Rectangle<float> jack (placement == Placement::left ? box.getRight() - 3.0f : box.getX() - 8.0f,
                                               box.getY() + box.getHeight() * 0.3f - 9.0f, 11.0f, 18.0f);
            g.setColour (juce::Colour (0xff7a7a7a));
            g.fillRoundedRectangle (jack, 2.0f);
        }

        g.setOpacity (hovering ? 0.95f : 0.7f);

        if (tile.kind == ChainStack::Kind::amp)
            paintAmp (g, box);
        else
            paintPedal (g, box, on);

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

    void paintPedal (juce::Graphics& g, juce::Rectangle<float> box, bool on) const
    {
        auto face = tile.colour;

        if (! on)
            face = face.withMultipliedSaturation (0.45f).withMultipliedBrightness (0.6f);

        pedal_look::paintEnclosure (g, box, face, 10.0f);
        const auto ink = pedal_look::inkFor (face);

        // Two knobs across the top.
        const auto knob = box.getWidth() * 0.22f;
        for (auto fx : { 0.3f, 0.7f })
        {
            const auto c = juce::Point<float> (box.getX() + box.getWidth() * fx, box.getY() + box.getHeight() * 0.16f);
            g.setColour (juce::Colour (0xff1b1b1e));
            g.fillEllipse (juce::Rectangle<float> (knob, knob).withCentre (c));
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.drawLine (c.x, c.y, c.x, c.y - knob * 0.4f, 1.5f);
        }

        auto text = box.reduced (8.0f, 0.0f).withTrimmedTop (box.getHeight() * 0.3f).withHeight (box.getHeight() * 0.3f);
        g.setColour (ink);
        g.setFont (juce::FontOptions (juce::jlimit (11.0f, 16.0f, box.getWidth() / 7.5f), juce::Font::bold | juce::Font::italic));
        g.drawFittedText (tile.name.toUpperCase(), text.removeFromTop (text.getHeight() * 0.65f).toNearestInt(),
                          juce::Justification::centredBottom, 2, 0.7f);
        g.setColour (ink.withAlpha (0.6f));
        g.setFont (juce::FontOptions (9.0f));
        g.drawFittedText (tile.detail.toUpperCase(), text.toNearestInt(), juce::Justification::centredTop, 1, 0.7f);

        pedal_look::paintLed (g, { box.getCentreX(), box.getY() + box.getHeight() * 0.68f }, 7.0f, on);
        const auto foot = box.getWidth() * 0.34f;
        pedal_look::paintFootswitch (g, juce::Rectangle<float> (foot, foot).withCentre ({ box.getCentreX(), box.getY() + box.getHeight() * 0.84f }), false);
    }

    void paintAmp (juce::Graphics& g, juce::Rectangle<float> box) const
    {
        box = box.withHeight (juce::jmin (box.getHeight(), box.getWidth() * 1.1f));
        pedal_look::paintTolex (g, box, 6.0f);
        g.setColour (pedal_look::AmpKnobs::gold.withAlpha (0.7f));
        g.drawRoundedRectangle (box.reduced (1.0f), 6.0f, 1.5f);

        auto inner = box.reduced (8.0f);
        auto label = inner.removeFromTop (34.0f);
        g.setColour (pedal_look::AmpKnobs::gold);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawFittedText ("AMP & CAB", label.removeFromTop (18.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
        g.setColour (pedal_look::AmpKnobs::gold.withAlpha (0.7f));
        g.setFont (juce::FontOptions (9.0f));
        g.drawFittedText (tile.detail.upToFirstOccurrenceOf (" | ", false, false), label.toNearestInt(), juce::Justification::centredTop, 1, 0.7f);
        inner.removeFromTop (4.0f);
        pedal_look::paintGrill (g, inner, 1);
    }

    Placement placement;
    ChainStack::Tile tile { ChainStack::Kind::pedal, Pedalboard::beforeAmp, -1, {}, {}, {}, nullptr };
    bool hovering = false;
};
