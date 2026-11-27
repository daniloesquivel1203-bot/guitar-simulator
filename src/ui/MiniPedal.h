#pragma once

#include "ChainStack.h"
#include "PedalLook.h"

#include <juce_gui_basics/juce_gui_basics.h>

/** A small stompbox, or a small amp and cab, for views that show several
    blocks at once: the neighbours beside the open pedal and the whole chain. */
namespace mini_pedal
{
/** The part of `box` the drawing really covers. The amp is shorter than a pedal. */
inline juce::Rectangle<float> getOutline (juce::Rectangle<float> box, ChainStack::Kind kind)
{
    return kind == ChainStack::Kind::amp ? box.withHeight (juce::jmin (box.getHeight(), box.getWidth() * 1.1f)) : box;
}

/** A pedal's footswitch. Clicking it switches the pedal on or off. */
inline juce::Rectangle<float> getFootswitch (juce::Rectangle<float> box)
{
    const auto foot = box.getWidth() * 0.34f;
    return juce::Rectangle<float> (foot, foot).withCentre ({ box.getCentreX(), box.getY() + box.getHeight() * 0.84f });
}

/** Where a cable plugs in on the left (input) or right (output) side. */
inline juce::Point<float> getJack (juce::Rectangle<float> box, ChainStack::Kind kind, bool input)
{
    const auto outline = getOutline (box, kind);
    return { input ? outline.getX() - 4.0f : outline.getRight() + 4.0f, outline.getY() + juce::jmin (outline.getHeight() * 0.5f, box.getHeight() * 0.3f) };
}

inline void paintJack (juce::Graphics& g, juce::Rectangle<float> box, ChainStack::Kind kind, bool input)
{
    const auto at = getJack (box, kind, input);
    g.setColour (juce::Colour (0xff7a7a7a));
    g.fillRoundedRectangle (juce::Rectangle<float> (11.0f, 18.0f).withCentre (at.translated (input ? 1.5f : -1.5f, 0.0f)), 2.0f);
}

inline void paintPedal (juce::Graphics& g, juce::Rectangle<float> box, const ChainStack::Tile& tile)
{
    const bool on = tile.enabled != nullptr && tile.enabled->isOn();
    auto face = tile.colour;

    if (! on)
        face = face.withMultipliedSaturation (0.45f).withMultipliedBrightness (0.6f);

    pedal_look::paintEnclosure (g, box, face, juce::jmin (10.0f, box.getWidth() * 0.1f));
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

    auto text = box.reduced (box.getWidth() * 0.07f, 0.0f).withTrimmedTop (box.getHeight() * 0.3f).withHeight (box.getHeight() * 0.3f);
    g.setColour (ink);
    g.setFont (juce::FontOptions (juce::jlimit (9.0f, 16.0f, box.getWidth() / 7.5f), juce::Font::bold | juce::Font::italic));
    g.drawFittedText (tile.name.toUpperCase(), text.removeFromTop (text.getHeight() * 0.65f).toNearestInt(),
                      juce::Justification::centredBottom, 2, 0.7f);
    g.setColour (ink.withAlpha (0.6f));
    g.setFont (juce::FontOptions (juce::jlimit (7.5f, 9.0f, box.getWidth() / 11.0f)));
    g.drawFittedText (tile.detail.toUpperCase(), text.toNearestInt(), juce::Justification::centredTop, 1, 0.7f);

    pedal_look::paintLed (g, { box.getCentreX(), box.getY() + box.getHeight() * 0.68f }, juce::jmax (5.0f, box.getWidth() * 0.06f), on);
    pedal_look::paintFootswitch (g, getFootswitch (box), false);
}

inline void paintAmp (juce::Graphics& g, juce::Rectangle<float> box, const ChainStack::Tile& tile)
{
    box = getOutline (box, ChainStack::Kind::amp);
    pedal_look::paintTolex (g, box, 6.0f);
    g.setColour (pedal_look::AmpKnobs::gold.withAlpha (0.7f));
    g.drawRoundedRectangle (box.reduced (1.0f), 6.0f, 1.5f);

    auto inner = box.reduced (juce::jmin (8.0f, box.getWidth() * 0.07f));
    auto label = inner.removeFromTop (juce::jmin (34.0f, inner.getHeight() * 0.3f));
    g.setColour (pedal_look::AmpKnobs::gold);
    g.setFont (juce::FontOptions (juce::jlimit (9.0f, 12.0f, box.getWidth() / 9.0f), juce::Font::bold));
    g.drawFittedText ("AMP & CAB", label.removeFromTop (label.getHeight() * 0.55f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
    g.setColour (pedal_look::AmpKnobs::gold.withAlpha (0.7f));
    g.setFont (juce::FontOptions (juce::jlimit (7.5f, 9.0f, box.getWidth() / 11.0f)));
    g.drawFittedText (tile.detail.upToFirstOccurrenceOf (" | ", false, false), label.toNearestInt(), juce::Justification::centredTop, 1, 0.7f);
    inner.removeFromTop (4.0f);
    pedal_look::paintGrill (g, inner, 1);
}

/** A pedal or the amp, whichever `tile` is. */
inline void paint (juce::Graphics& g, juce::Rectangle<float> box, const ChainStack::Tile& tile)
{
    if (tile.kind == ChainStack::Kind::amp)
        paintAmp (g, box, tile);
    else
        paintPedal (g, box, tile);
}

/** A patch cable that sags a little between its plugs. */
inline void paintCable (juce::Graphics& g, juce::Point<float> from, juce::Point<float> to)
{
    juce::Path cable;
    const auto sag = juce::jmin (60.0f, std::abs (to.x - from.x) * 0.35f + 12.0f);
    cable.startNewSubPath (from);
    cable.cubicTo (from.x + (to.x - from.x) * 0.35f, juce::jmax (from.y, to.y) + sag,
                   from.x + (to.x - from.x) * 0.65f, juce::jmax (from.y, to.y) + sag, to.x, to.y);

    const juce::PathStrokeType stroke (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.strokePath (cable, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                  juce::AffineTransform::translation (0.0f, 3.0f));
    g.setColour (juce::Colour (0xff111214));
    g.strokePath (cable, stroke);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.strokePath (cable, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                  juce::AffineTransform::translation (0.0f, -1.0f));

    for (auto p : { from, to })
    {
        g.setColour (juce::Colour (0xff3a3c42));
        g.fillRoundedRectangle (juce::Rectangle<float> (12.0f, 9.0f).withCentre (p), 2.0f);
    }
}
}
