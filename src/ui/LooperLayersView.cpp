#include "LooperLayersView.h"

#include "Theme.h"

#include <cmath>

namespace
{
constexpr int labelWidth = 30;
constexpr int removeWidth = 20;
constexpr int laneGap = 2;
constexpr int maxLaneHeight = 36;

juce::Colour laneColour (int lane)
{
    static const std::array<juce::Colour, 6> colours { Theme::good,
                                                       Theme::accent,
                                                       juce::Colour (0xffb48ef0),
                                                       juce::Colour (0xfff07fa8),
                                                       juce::Colour (0xff7fd6d0),
                                                       juce::Colour (0xffd8c76a) };
    return colours[(size_t) lane % colours.size()];
}

juce::String laneName (int lane, int folded)
{
    if (lane == 0)
        return folded > 0 ? "1-" + juce::String (folded + 1) : juce::String ("1");

    return juce::String (lane + folded + 1);
}
}

LooperLayersView::LooperLayersView (Looper& looperToShow) : looper (looperToShow)
{
}

juce::Rectangle<int> LooperLayersView::getLaneBounds (int lane, int lanes) const
{
    const auto area = getLocalBounds().reduced (3);
    const auto height = juce::jmin (maxLaneHeight, (area.getHeight() - (lanes - 1) * laneGap) / juce::jmax (1, lanes));
    return area.withY (area.getY() + lane * (height + laneGap)).withHeight (height);
}

bool LooperLayersView::canRemoveLayers() const
{
    const auto state = looper.getState();
    return looper.getNumLayerLanes() > 1 && state != Looper::State::recording && state != Looper::State::empty;
}

int LooperLayersView::getRemoveButtonAt (juce::Point<int> point) const
{
    if (! canRemoveLayers())
        return -1;

    const auto lanes = looper.getNumLayerLanes();

    for (int lane = 0; lane < lanes; ++lane)
        if (getLaneBounds (lane, lanes).removeFromRight (removeWidth).contains (point))
            return lane;

    return -1;
}

void LooperLayersView::setHoveredButton (int lane)
{
    if (lane == hoveredButton)
        return;

    hoveredButton = lane;
    setMouseCursor (lane >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    setTooltip (lane >= 0 ? "Remove layer " + laneName (lane, looper.getNumFoldedLayers()) : juce::String());
    repaint();
}

void LooperLayersView::mouseMove (const juce::MouseEvent& event)
{
    setHoveredButton (getRemoveButtonAt (event.getPosition()));
}

void LooperLayersView::mouseExit (const juce::MouseEvent&)
{
    setHoveredButton (-1);
}

void LooperLayersView::mouseDown (const juce::MouseEvent& event)
{
    const auto lane = getRemoveButtonAt (event.getPosition());
    pressedLayerId = lane >= 0 ? looper.getLayerId (lane) : 0;
}

void LooperLayersView::mouseUp (const juce::MouseEvent& event)
{
    const auto lane = getRemoveButtonAt (event.getPosition());

    // The id, not the lane number, so the right layer goes even if the lanes moved.
    if (lane >= 0 && pressedLayerId != 0 && looper.getLayerId (lane) == pressedLayerId)
        looper.removeLayer (pressedLayerId);

    pressedLayerId = 0;
    setHoveredButton (-1);
}

void LooperLayersView::paint (juce::Graphics& g)
{
    const auto state = looper.getState();
    const auto lanes = looper.getNumLayerLanes();
    const auto bounds = getLocalBounds();

    g.setColour (Theme::sunken);
    g.fillRoundedRectangle (bounds.toFloat(), 4.0f);

    if (state == Looper::State::empty || lanes == 0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Layers appear here", bounds, juce::Justification::centred);
        return;
    }

    const auto laneHeight = getLaneBounds (0, lanes).getHeight();
    const auto folded = looper.getNumFoldedLayers();
    const auto removable = canRemoveLayers();
    juce::Rectangle<int> waves;

    g.setFont (juce::FontOptions (juce::jlimit (9.0f, 11.0f, (float) laneHeight - 2.0f), juce::Font::bold));

    for (int lane = 0; lane < lanes; ++lane)
    {
        auto row = getLaneBounds (lane, lanes);

        const auto live = (state == Looper::State::recording && lane == 0)
                          || (state == Looper::State::overdubbing && lane == lanes - 1);
        auto colour = state == Looper::State::recording   ? Theme::danger
                      : live                              ? Theme::warning
                                                          : laneColour (lane + (lane > 0 ? folded : 0));

        if (state == Looper::State::stopped)
            colour = colour.withMultipliedSaturation (0.45f).withMultipliedBrightness (0.75f);

        g.setColour (live ? colour : juce::Colours::white.withAlpha (0.6f));
        g.drawText (laneName (lane, folded), row.removeFromLeft (labelWidth), juce::Justification::centred, false);

        if (removable)
        {
            const auto button = row.removeFromRight (removeWidth);
            const auto hovered = lane == hoveredButton;

            if (hovered)
            {
                g.setColour (Theme::danger);
                g.fillRect (button);
            }

            const auto size = juce::jmin (7.0f, (float) button.getHeight() - 4.0f);
            const auto cross = button.toFloat().withSizeKeepingCentre (size, size);

            g.setColour (juce::Colours::white.withAlpha (hovered ? 1.0f : 0.55f));
            g.drawLine ({ cross.getTopLeft(), cross.getBottomRight() }, 1.4f);
            g.drawLine ({ cross.getTopRight(), cross.getBottomLeft() }, 1.4f);
        }

        g.setColour (Theme::panel.withAlpha (live ? 0.95f : 0.6f));
        g.fillRect (row);
        paintLane (g, row, lane, colour);
        waves = waves.isEmpty() ? row : waves.getUnion (row);
    }

    if (state != Looper::State::recording && looper.getLength() > 0)
    {
        const auto fraction = (float) looper.getPosition() / (float) looper.getLength();
        const auto x = waves.getX() + juce::roundToInt (fraction * (float) (waves.getWidth() - 1));

        g.setColour (juce::Colours::white.withAlpha (state == Looper::State::stopped ? 0.3f : 0.9f));
        g.drawVerticalLine (x, (float) waves.getY(), (float) waves.getBottom());
    }
}

void LooperLayersView::paintLane (juce::Graphics& g, juce::Rectangle<int> wave, int lane, juce::Colour colour) const
{
    const auto bins = looper.getNumLayerBinsFilled();
    const auto width = wave.getWidth();

    if (bins <= 0 || width <= 0)
        return;

    const auto centre = (float) wave.getCentreY();
    const auto halfHeight = (float) wave.getHeight() * 0.5f;

    g.setColour (colour);

    for (int x = 0; x < width; ++x)
    {
        const auto first = x * bins / width;
        const auto last = juce::jmin (bins, juce::jmax (first + 1, (x + 1) * bins / width));
        auto peak = 0.0f;

        for (int bin = first; bin < last; ++bin)
            peak = juce::jmax (peak, looper.getLayerPeak (lane, bin));

        if (peak <= 0.0005f)
            continue;

        // A gentle curve, so quiet playing is still easy to see.
        const auto extent = juce::jmax (0.5f, std::pow (juce::jmin (peak, 1.0f), 0.6f) * halfHeight);
        g.drawVerticalLine (wave.getX() + x, centre - extent, centre + extent);
    }
}
