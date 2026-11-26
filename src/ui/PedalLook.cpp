#include "PedalLook.h"

namespace pedal_look
{
juce::Colour categoryColour (const juce::String& category)
{
    if (category == "Dynamics")    return juce::Colour (0xff8b97a3);
    if (category == "Drive")       return juce::Colour (0xff8fa65a);
    if (category == "Filter")      return juce::Colour (0xffc9a24a);
    if (category == "Modulation")  return juce::Colour (0xff4e9a9a);
    if (category == "Pitch")       return juce::Colour (0xffb56a8a);
    if (category == "Delay")       return juce::Colour (0xff7e6aa6);
    if (category == "Reverb")      return juce::Colour (0xff5a7aa8);
    return juce::Colour (0xff9a9a9a);
}

juce::Colour pedalColour (const PedalType& type)
{
    const auto base = categoryColour (type.category);

    // Spread the pedals of a category a little in hue and brightness, the same
    // way every time, by their position within the category.
    int position = 0;

    for (const auto& other : getPedalTypes())
    {
        if (&other == &type || juce::String (other.key) == type.key)
            break;

        if (juce::String (other.category) == type.category)
            ++position;
    }

    static constexpr float hueSteps[] = { 0.0f, 0.035f, -0.035f, 0.07f, -0.07f, 0.0f, 0.05f, -0.05f, 0.1f };
    static constexpr float brightSteps[] = { 0.0f, 0.06f, -0.06f, -0.02f, 0.08f, -0.1f, 0.1f, 0.03f, -0.04f };
    const auto i = (size_t) position % std::size (hueSteps);

    auto hue = base.getHue() + hueSteps[i];
    hue -= std::floor (hue);
    return juce::Colour::fromHSV (hue, base.getSaturation(), juce::jlimit (0.25f, 0.95f, base.getBrightness() + brightSteps[i]), 1.0f);
}

juce::Colour inkFor (juce::Colour face)
{
    return face.getPerceivedBrightness() > 0.45f ? juce::Colour (0xff15170f) : juce::Colour (0xfff0f0f0);
}

void paintEnclosure (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour face, float cornerSize)
{
    // Drop shadow.
    for (int i = 6; i > 0; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.07f));
        g.fillRoundedRectangle (area.translated (0.0f, (float) i * 2.0f).expanded ((float) i * 0.8f), cornerSize + (float) i);
    }

    g.setGradientFill (juce::ColourGradient (face.brighter (0.35f), area.getX(), area.getY(),
                                             face.darker (0.3f), area.getRight(), area.getBottom(), false));
    g.fillRoundedRectangle (area, cornerSize);

    // A soft sheen across the top, and a darker lip along the bottom edge.
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.18f), area.getX(), area.getY(),
                                             juce::Colours::transparentWhite, area.getX(), area.getY() + area.getHeight() * 0.4f, false));
    g.fillRoundedRectangle (area, cornerSize);

    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.drawRoundedRectangle (area.reduced (0.5f), cornerSize, 1.5f);
    g.setColour (juce::Colours::white.withAlpha (0.3f));
    g.drawRoundedRectangle (area.reduced (2.0f).withTrimmedBottom (area.getHeight() * 0.6f), cornerSize - 2.0f, 1.0f);
}

void paintBrick (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour face, bool withStuds)
{
    if (withStuds)
    {
        // Studs poke out above the brick, so the brick above seems to sit on them.
        const auto studWidth = 18.0f, studHeight = 6.0f, pitch = 30.0f;

        for (auto x = area.getX() + 18.0f; x + studWidth < area.getRight() - 50.0f; x += pitch)
        {
            const juce::Rectangle<float> stud (x, area.getY() - studHeight + 1.0f, studWidth, studHeight + 2.0f);
            g.setColour (face.brighter (0.12f));
            g.fillRoundedRectangle (stud, 3.0f);
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.fillRoundedRectangle (stud.withHeight (2.5f).reduced (3.0f, 0.0f), 1.0f);
        }
    }

    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (area.translated (0.0f, 2.0f), 5.0f);

    g.setGradientFill (juce::ColourGradient (face.brighter (0.25f), area.getX(), area.getY(),
                                             face.darker (0.2f), area.getX(), area.getBottom(), false));
    g.fillRoundedRectangle (area, 5.0f);

    g.setColour (juce::Colours::black.withAlpha (0.22f));
    g.fillRect (area.withTop (area.getBottom() - 3.0f).reduced (4.0f, 0.0f));
}

void paintLed (juce::Graphics& g, juce::Point<float> centre, float diameter, bool on, juce::Colour lit)
{
    const auto r = juce::Rectangle<float> (diameter, diameter).withCentre (centre);

    if (on)
    {
        g.setGradientFill (juce::ColourGradient (lit.withAlpha (0.55f), centre.x, centre.y,
                                                 lit.withAlpha (0.0f), centre.x + diameter * 1.4f, centre.y, true));
        g.fillEllipse (r.expanded (diameter * 1.2f));
        g.setColour (lit);
        g.fillEllipse (r);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.fillEllipse (r.reduced (diameter * 0.3f).translated (-diameter * 0.1f, -diameter * 0.12f));
    }
    else
    {
        g.setColour (lit.darker (2.2f).withAlpha (0.9f));
        g.fillEllipse (r);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (r, 1.0f);
    }
}

void paintFootswitch (juce::Graphics& g, juce::Rectangle<float> area, bool pressed)
{
    const auto c = area.getCentre();
    const auto d = juce::jmin (area.getWidth(), area.getHeight());

    g.setColour (juce::Colour (0xff777777));
    g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (c));
    g.setColour (juce::Colour (0xff2a2a2a));
    g.fillEllipse (juce::Rectangle<float> (d - 4.0f, d - 4.0f).withCentre (c));

    const auto cap = juce::Rectangle<float> (d * 0.72f, d * 0.72f).withCentre (c).translated (0.0f, pressed ? 1.5f : 0.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff2f2f2), cap.getX() + cap.getWidth() * 0.35f, cap.getY() + cap.getHeight() * 0.3f,
                                             juce::Colour (0xff555555), cap.getRight(), cap.getBottom(), true));
    g.fillEllipse (cap);
}

void paintTolex (juce::Graphics& g, juce::Rectangle<float> area, float cornerSize)
{
    g.setColour (juce::Colour (0xff0a0a0a));
    g.fillRoundedRectangle (area, cornerSize);

    const auto inner = area.reduced (3.0f);
    g.setColour (juce::Colour (0xff181818));
    g.fillRoundedRectangle (inner, cornerSize - 2.0f);

    // A fine grain, drawn as a sparse dot pattern that is the same every time.
    juce::Graphics::ScopedSaveState state (g);
    juce::Path clip;
    clip.addRoundedRectangle (inner, cornerSize - 2.0f);
    g.reduceClipRegion (clip);
    g.setColour (juce::Colours::white.withAlpha (0.035f));

    juce::Random random (42);

    for (int i = 0; i < (int) (inner.getWidth() * inner.getHeight() / 30.0f); ++i)
        g.fillRect (inner.getX() + random.nextFloat() * inner.getWidth(), inner.getY() + random.nextFloat() * inner.getHeight(), 1.5f, 1.5f);

    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.06f), inner.getX(), inner.getY(),
                                             juce::Colours::transparentWhite, inner.getX(), inner.getY() + 40.0f, false));
    g.fillRect (inner);
}

void paintGrill (juce::Graphics& g, juce::Rectangle<float> area, int numSpeakers)
{
    juce::Graphics::ScopedSaveState state (g);
    g.setColour (juce::Colour (0xff2a2a2a));
    g.fillRoundedRectangle (area, 4.0f);
    g.reduceClipRegion (area.toNearestInt());

    // A woven diagonal pattern.
    g.setColour (juce::Colour (0xff3d3d3d));

    for (auto x = area.getX() - area.getHeight(); x < area.getRight(); x += 4.0f)
        g.drawLine (x, area.getBottom(), x + area.getHeight(), area.getY(), 1.2f);

    g.setColour (juce::Colour (0xff222222).withAlpha (0.6f));

    for (auto x = area.getX(); x < area.getRight() + area.getHeight(); x += 4.0f)
        g.drawLine (x, area.getBottom(), x - area.getHeight(), area.getY(), 0.8f);

    // The speakers show faintly through the cloth.
    const int columns = numSpeakers >= 2 ? 2 : 1;
    const int rows = numSpeakers >= 4 ? 2 : 1;
    const auto cellW = area.getWidth() / (float) columns, cellH = area.getHeight() / (float) rows;
    const auto diameter = juce::jmin (cellW, cellH) * (numSpeakers == 3 ? 0.62f : 0.82f);

    auto drawSpeaker = [&] (juce::Point<float> c, float d)
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawEllipse (juce::Rectangle<float> (d, d).withCentre (c), 3.0f);
        g.fillEllipse (juce::Rectangle<float> (d * 0.22f, d * 0.22f).withCentre (c));
    };

    if (numSpeakers == 3)
    {
        drawSpeaker ({ area.getX() + area.getWidth() * 0.25f, area.getCentreY() }, diameter);
        drawSpeaker ({ area.getCentreX(), area.getCentreY() }, diameter);
        drawSpeaker ({ area.getX() + area.getWidth() * 0.75f, area.getCentreY() }, diameter);
    }
    else
    {
        for (int r = 0; r < rows; ++r)
            for (int col = 0; col < columns; ++col)
                drawSpeaker ({ area.getX() + cellW * ((float) col + 0.5f), area.getY() + cellH * ((float) r + 0.5f) }, diameter);
    }

    // Shade the edges so the cloth looks set into the cabinet.
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, area.getCentreX(), area.getCentreY(),
                                             juce::Colours::black.withAlpha (0.55f), area.getX(), area.getY(), true));
    g.fillRect (area);
}

//==============================================================================
void StompboxKnobs::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                                      float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const auto radius = juce::jmin (juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f, 32.0f);
    const auto centre = bounds.getCentre();
    const auto angle = startAngle + position * (endAngle - startAngle);

    // Printed tick marks around the knob, like a pedal's screen print.
    const auto ink = slider.findColour (juce::Slider::textBoxTextColourId).withAlpha (0.55f);
    g.setColour (ink);

    for (int i = 0; i <= 10; ++i)
    {
        const auto a = startAngle + (float) i / 10.0f * (endAngle - startAngle);
        const auto p1 = centre.getPointOnCircumference (radius + 1.0f, a);
        const auto p2 = centre.getPointOnCircumference (radius + (i % 5 == 0 ? 6.0f : 4.0f), a);
        g.drawLine ({ p1, p2 }, 1.2f);
    }

    const auto body = juce::Rectangle<float> (radius * 2.0f - 4.0f, radius * 2.0f - 4.0f).withCentre (centre);

    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillEllipse (body.translated (0.0f, 3.0f));

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4a4a), body.getX() + body.getWidth() * 0.35f, body.getY() + body.getHeight() * 0.3f,
                                             juce::Colour (0xff080808), body.getRight(), body.getBottom(), true));
    g.fillEllipse (body);

    if (slider.isMouseOverOrDragging())
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillEllipse (body);
    }

    // Knurled edge.
    g.setColour (juce::Colours::white.withAlpha (0.08f));

    for (int i = 0; i < 24; ++i)
    {
        const auto a = juce::MathConstants<float>::twoPi * (float) i / 24.0f;
        g.drawLine ({ centre.getPointOnCircumference (body.getWidth() * 0.5f - 3.0f, a),
                      centre.getPointOnCircumference (body.getWidth() * 0.5f - 0.5f, a) }, 1.0f);
    }

    juce::Path pointer;
    const auto bodyRadius = body.getWidth() * 0.5f;
    pointer.addRoundedRectangle (-1.5f, -bodyRadius + 4.0f, 3.0f, bodyRadius * 0.55f, 1.5f);
    g.setColour (juce::Colours::white);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void AmpKnobs::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                                 float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const auto radius = juce::jmin (juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f, 30.0f);
    const auto centre = bounds.getCentre();
    const auto angle = startAngle + position * (endAngle - startAngle);
    const bool goldCap = (bool) slider.getProperties()["goldCap"];

    // The value as a gold arc around the knob.
    const auto arcRadius = radius - 2.0f;
    juce::Path track, value;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
    const juce::PathStrokeType stroke (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.strokePath (track, stroke);
    g.setColour (gold);
    g.strokePath (value, stroke);

    const auto body = juce::Rectangle<float> ((radius - 6.0f) * 2.0f, (radius - 6.0f) * 2.0f).withCentre (centre);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (body.translated (0.0f, 3.0f));

    const auto light = goldCap ? juce::Colour (0xfff6e7b8) : juce::Colour (0xff5a5a5a);
    const auto dark = goldCap ? juce::Colour (0xffa88a3a) : juce::Colour (0xff111111);
    g.setGradientFill (juce::ColourGradient (light, body.getX() + body.getWidth() * 0.35f, body.getY() + body.getHeight() * 0.3f,
                                             dark, body.getRight(), body.getBottom(), true));
    g.fillEllipse (body);

    if (slider.isMouseOverOrDragging())
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillEllipse (body);
    }

    juce::Path pointer;
    const auto bodyRadius = body.getWidth() * 0.5f;
    pointer.addRoundedRectangle (-1.5f, -bodyRadius + 3.0f, 3.0f, bodyRadius * 0.55f, 1.5f);
    g.setColour (goldCap ? juce::Colour (0xff222222) : juce::Colours::white);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}
}
