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

/** The number of knobs on a pedal: its controls that are neither switches nor choices. */
inline int countKnobs (Effect& effect)
{
    const auto controls = effect.getControls();
    return (int) std::count_if (controls.begin(), controls.end(), [] (Parameter* p) { return ! p->isChoice() && ! p->isSwitch(); });
}

/** `count` knobs laid out like the big stompbox does: one row of up to three,
    then two rows of two or three, then rows of four. Each row is centred in `area`. */
inline void paintKnobs (juce::Graphics& g, juce::Rectangle<float> area, int count, float maxSize, bool withPointers)
{
    if (count <= 0)
        return;

    const auto columns = count <= 3 ? count : (count == 4 ? 2 : (count <= 6 ? 3 : 4));
    const auto rows = (count + columns - 1) / columns;
    const auto size = juce::jmin (maxSize, area.getWidth() / (float) columns * 0.8f, area.getHeight() / (float) rows * 0.85f);
    const auto stepX = juce::jmin (size * 1.45f, area.getWidth() / (float) columns);
    const auto stepY = area.getHeight() / (float) rows;

    for (int i = 0; i < count; ++i)
    {
        const auto row = i / columns;
        const auto inRow = juce::jmin (columns, count - row * columns);
        const auto c = juce::Point<float> (area.getCentreX() + ((float) (i % columns) - (float) (inRow - 1) * 0.5f) * stepX,
                                           area.getY() + stepY * ((float) row + 0.5f));

        g.setColour (juce::Colour (0xff1b1b1e));
        g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (c));

        if (withPointers)
        {
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.drawLine (c.x, c.y, c.x, c.y - size * 0.4f, juce::jmax (1.0f, size * 0.07f));
        }
    }
}

inline void paintPedal (juce::Graphics& g, juce::Rectangle<float> box, const ChainStack::Tile& tile)
{
    const bool on = tile.enabled != nullptr && tile.enabled->isOn();
    auto face = tile.colour;

    if (! on)
        face = face.withMultipliedSaturation (0.45f).withMultipliedBrightness (0.6f);

    pedal_look::paintEnclosure (g, box, face, juce::jmin (10.0f, box.getWidth() * 0.1f));
    const auto ink = pedal_look::inkFor (face);

    // The pedal's own knobs, across the top.
    paintKnobs (g, box.reduced (box.getWidth() * 0.08f, 0.0f).withTrimmedTop (box.getHeight() * 0.03f).withHeight (box.getHeight() * 0.27f),
                tile.numKnobs, box.getWidth() * 0.22f, true);

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
