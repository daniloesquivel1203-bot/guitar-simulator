#include "BlockEditorParts.h"

#include "Theme.h"

#include <cmath>

//==============================================================================
ResponseGraph::ResponseGraph (const juce::String& tooltip)
{
    setTooltip (tooltip);
}

const std::vector<float>& ResponseGraph::getFrequencies()
{
    static const auto frequencies = []
    {
        std::vector<float> f;

        for (int i = 0; i < 160; ++i)
            f.push_back (20.0f * std::pow (1000.0f, (float) i / 159.0f));

        return f;
    }();

    return frequencies;
}

void ResponseGraph::setCurve (std::vector<float> newDb, bool show, const juce::String& emptyText)
{
    db = std::move (newDb);
    showing = show;
    emptyMessage = emptyText;
    repaint();
}

void ResponseGraph::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (Theme::sunken);
    g.fillRoundedRectangle (area, 4.0f);

    constexpr float range = 24.0f;   // dB above and below the middle
    const auto yFor = [&] (float value) { return area.getCentreY() - juce::jlimit (-range, range, value) / range * area.getHeight() * 0.5f; };
    const auto xFor = [&] (float hz) { return area.getX() + std::log (hz / 20.0f) / std::log (1000.0f) * area.getWidth(); };

    g.setColour (juce::Colours::white.withAlpha (0.08f));

    for (auto hz : { 100.0f, 1000.0f, 10000.0f })
        g.drawVerticalLine (juce::roundToInt (xFor (hz)), area.getY(), area.getBottom());

    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawHorizontalLine (juce::roundToInt (yFor (0.0f)), area.getX(), area.getRight());

    const float dashes[] = { 2.0f, 3.0f };

    for (auto level : { 12.0f, -12.0f })
        g.drawDashedLine (juce::Line<float> (area.getX(), yFor (level), area.getRight(), yFor (level)), dashes, 2, 1.0f);

    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("100", juce::Rectangle<float> (xFor (100.0f) + 2.0f, area.getBottom() - 12.0f, 30.0f, 12.0f), juce::Justification::left);
    g.drawText ("1k", juce::Rectangle<float> (xFor (1000.0f) + 2.0f, area.getBottom() - 12.0f, 30.0f, 12.0f), juce::Justification::left);
    g.drawText ("10k", juce::Rectangle<float> (xFor (10000.0f) + 2.0f, area.getBottom() - 12.0f, 30.0f, 12.0f), juce::Justification::left);

    if (! showing || db.empty())
    {
        g.drawText (emptyMessage, area, juce::Justification::centred);
        return;
    }

    const auto& frequencies = getFrequencies();
    juce::Path curve;

    for (size_t i = 0; i < db.size() && i < frequencies.size(); ++i)
    {
        const juce::Point<float> point (xFor (frequencies[i]), yFor (db[i]));

        if (i == 0)
            curve.startNewSubPath (point);
        else
            curve.lineTo (point);
    }

    g.saveState();
    g.reduceClipRegion (getLocalBounds());
    g.setColour (Theme::accent);
    g.strokePath (curve, juce::PathStrokeType (1.75f));
    g.restoreState();
}

