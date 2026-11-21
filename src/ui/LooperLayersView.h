#pragma once

#include "../engine/Practice.h"

#include <juce_gui_basics/juce_gui_basics.h>

/** Shows each layer of the loop as its own waveform lane, oldest at the top,
    with a line marking where the loop is playing. Each lane has an x button
    that removes that layer. */
class LooperLayersView final : public juce::Component,
                               public juce::SettableTooltipClient
{
public:
    explicit LooperLayersView (Looper& looperToShow);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> getLaneBounds (int lane, int lanes) const;
    bool canRemoveLayers() const;
    int getRemoveButtonAt (juce::Point<int>) const;
    void setHoveredButton (int lane);
    void paintLane (juce::Graphics&, juce::Rectangle<int> wave, int lane, juce::Colour) const;

    Looper& looper;
    int hoveredButton = -1;
    int pressedLayerId = 0;
};
