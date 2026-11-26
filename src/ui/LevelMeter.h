#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** A peak meter with a decibel scale and an optional clip light. It runs
    left to right when it is wider than it is tall, otherwise bottom to top. */
class LevelMeter final : public juce::Component
{
public:
    explicit LevelMeter (bool showClipLight) : hasClipLight (showClipLight) {}

    /** Feed the latest peak (linear gain, 1.0 is full scale). Call from a UI timer. */
    void update (float peak, bool clipped)
    {
        const auto db = juce::Decibels::gainToDecibels (peak, minDb);

        // Jump up instantly, fall back slowly so short peaks stay visible.
        levelDb = juce::jmax (db, levelDb - fallDbPerUpdate);

        if (clipped)
            clipHoldUpdates = clipHoldLength;
        else if (clipHoldUpdates > 0)
            --clipHoldUpdates;

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        const bool horizontal = area.getWidth() > area.getHeight();

        if (hasClipLight)
        {
            auto light = horizontal ? area.removeFromRight (10.0f) : area.removeFromTop (10.0f);

            if (horizontal)
                area.removeFromRight (4.0f);
            else
                area.removeFromTop (4.0f);

            g.setColour (clipHoldUpdates > 0 ? juce::Colour (0xffe5484d) : juce::Colour (0xff3a2a2c));
            g.fillRoundedRectangle (light, 2.0f);
        }

        g.setColour (juce::Colour (0xff1b2126));
        g.fillRoundedRectangle (area, 3.0f);

        const auto proportion = juce::jlimit (0.0f, 1.0f, (levelDb - minDb) / -minDb);
        auto bar = horizontal ? area.withWidth (area.getWidth() * proportion)
                              : area.withTrimmedTop (area.getHeight() * (1.0f - proportion));

        g.setColour (levelDb > -6.0f ? juce::Colour (0xfff5a524) : juce::Colour (0xff3ecf8e));
        g.fillRoundedRectangle (bar, 3.0f);

        g.setColour (juce::Colours::white.withAlpha (0.12f));

        for (auto markDb : { -6.0f, -12.0f, -24.0f, -36.0f, -48.0f })
        {
            if (horizontal)
                g.fillRect (area.getRight() - area.getWidth() * (markDb / minDb), area.getY(), 1.0f, area.getHeight());
            else
                g.fillRect (area.getX(), area.getY() + area.getHeight() * (markDb / minDb), area.getWidth(), 1.0f);
        }
    }

private:
    static constexpr float minDb = -60.0f;
    static constexpr float fallDbPerUpdate = 1.5f;
    static constexpr int clipHoldLength = 45;

    const bool hasClipLight;
    float levelDb = minDb;
    int clipHoldUpdates = 0;
};
