#include "LooperLayersView.h"

#include "Theme.h"

#include <cmath>

namespace
{
constexpr int labelWidth = 30;
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
}

LooperLayersView::LooperLayersView (const Looper& looperToShow) : looper (looperToShow)
{
    setInterceptsMouseClicks (false, false);
}

void LooperLayersView::paint (juce::Graphics& g)
{
    const auto state = looper.getState();
    const auto lanes = looper.getNumLayerLanes();
    auto bounds = getLocalBounds();

    g.setColour (Theme::sunken);
    g.fillRoundedRectangle (bounds.toFloat(), 4.0f);

    if (state == Looper::State::empty || lanes == 0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Layers appear here", bounds, juce::Justification::centred);
        return;
    }

    bounds.reduce (3, 3);
    const auto laneHeight = juce::jmin (maxLaneHeight, (bounds.getHeight() - (lanes - 1) * laneGap) / lanes);
    const auto folded = looper.getNumFoldedLayers();
    juce::Rectangle<int> waves;

    g.setFont (juce::FontOptions (juce::jlimit (9.0f, 11.0f, (float) laneHeight - 2.0f), juce::Font::bold));

    for (int lane = 0; lane < lanes; ++lane)
    {
        auto row = bounds.removeFromTop (laneHeight);
        bounds.removeFromTop (laneGap);

        const auto live = (state == Looper::State::recording && lane == 0)
                          || (state == Looper::State::overdubbing && lane == lanes - 1);
        auto colour = state == Looper::State::recording   ? Theme::danger
                      : live                              ? Theme::warning
                                                          : laneColour (lane + (lane > 0 ? folded : 0));

        if (state == Looper::State::stopped)
            colour = colour.withMultipliedSaturation (0.45f).withMultipliedBrightness (0.75f);

        const auto number = lane == 0 ? (folded > 0 ? "1-" + juce::String (folded + 1) : juce::String ("1"))
                                      : juce::String (lane + folded + 1);

        g.setColour (live ? colour : juce::Colours::white.withAlpha (0.6f));
        g.drawText (number, row.removeFromLeft (labelWidth), juce::Justification::centred, false);

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
        const auto last = juce::jmax (first + 1, (x + 1) * bins / width);
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
