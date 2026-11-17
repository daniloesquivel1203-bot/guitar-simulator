#include "Theme.h"

Theme::Theme()
{
    setColour (juce::ResizableWindow::backgroundColourId, background);

    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, sunken);
    setColour (juce::Slider::thumbColourId, juce::Colours::white);
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::Slider::backgroundColourId, sunken);
    setColour (juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha (0.9f));
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, accent.withAlpha (0.5f));

    setColour (juce::TextButton::buttonColourId, raised);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, juce::Colours::white.withAlpha (0.9f));
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);

    setColour (juce::ComboBox::backgroundColourId, sunken);
    setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::arrowColourId, juce::Colours::white.withAlpha (0.6f));

    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.35f));
    setColour (juce::PopupMenu::headerTextColourId, juce::Colours::white.withAlpha (0.5f));

    setColour (juce::ToggleButton::tickColourId, good);
    setColour (juce::ToggleButton::tickDisabledColourId, juce::Colours::white.withAlpha (0.4f));

    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::outlineColourId, raised);
    setColour (juce::TextEditor::backgroundColourId, sunken);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);

    setColour (juce::TooltipWindow::backgroundColourId, sunken);
    setColour (juce::TooltipWindow::outlineColourId, raised);
}

void Theme::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                              float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (6.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle = startAngle + position * (endAngle - startAngle);
    const auto thickness = juce::jlimit (3.0f, 6.0f, radius * 0.14f);
    const auto arcRadius = radius - thickness * 0.5f;
    const auto stroke = juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    // The full sweep, then the part up to the current value on top of it.
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
    g.strokePath (track, stroke);

    if (slider.isEnabled())
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
        g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
        g.strokePath (value, stroke);
    }

    // The knob body and a pointer line.
    const auto bodyRadius = arcRadius - thickness * 1.6f;
    const bool active = slider.isMouseOverOrDragging();

    g.setColour (active ? raised.brighter (0.12f) : raised);
    g.fillEllipse (juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre));

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -bodyRadius + 3.0f, 3.0f, bodyRadius * 0.45f, 1.5f);
    g.setColour (slider.findColour (juce::Slider::thumbColourId));
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void Theme::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                              float minPosition, float maxPosition, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, position, minPosition, maxPosition, style, slider);
        return;
    }

    // A slim fader: a track, a filled part below the handle, and a wide flat handle.
    const auto centreX = (float) x + (float) width * 0.5f;
    const auto track = juce::Rectangle<float> (centreX - 2.5f, (float) y, 5.0f, (float) height);

    g.setColour (slider.findColour (juce::Slider::backgroundColourId));
    g.fillRoundedRectangle (track, 2.5f);

    g.setColour (slider.findColour (juce::Slider::trackColourId).withAlpha (0.8f));
    g.fillRoundedRectangle (track.withTop (position), 2.5f);

    const auto handle = juce::Rectangle<float> (28.0f, 12.0f).withCentre ({ centreX, position });
    g.setColour (slider.isMouseOverOrDragging() ? juce::Colours::white : juce::Colours::white.withAlpha (0.9f));
    g.fillRoundedRectangle (handle, 3.0f);
    g.setColour (sunken);
    g.fillRect (handle.withSizeKeepingCentre (18.0f, 1.5f));
}

void Theme::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                  bool highlighted, bool down)
{
    auto colour = backgroundColour;

    if (down)
        colour = colour.brighter (0.2f);
    else if (highlighted)
        colour = colour.brighter (0.08f);

    g.setColour (colour.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
    g.fillRoundedRectangle (button.getLocalBounds().toFloat(), 6.0f);
}

void Theme::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat();

    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, 6.0f);

    if (box.hasKeyboardFocus (true) || box.isPopupActive())
    {
        g.setColour (accent.withAlpha (0.8f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
    }

    // A small downward arrow on the right.
    const auto arrow = juce::Rectangle<float> ((float) width - 24.0f, 0.0f, 12.0f, (float) height);
    juce::Path path;
    path.startNewSubPath (arrow.getX(), arrow.getCentreY() - 2.5f);
    path.lineTo (arrow.getCentreX(), arrow.getCentreY() + 2.5f);
    path.lineTo (arrow.getRight(), arrow.getCentreY() - 2.5f);

    g.setColour (box.findColour (juce::ComboBox::arrowColourId).withAlpha (box.isEnabled() ? 0.8f : 0.3f));
    g.strokePath (path, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Label* Theme::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (juce::FontOptions (13.0f));
    return label;
}
