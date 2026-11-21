#pragma once

#include "../engine/Practice.h"

#include <juce_gui_basics/juce_gui_basics.h>

/** Shows each layer of the loop as its own waveform lane, oldest at the top,
    with a line marking where the loop is playing. */
class LooperLayersView final : public juce::Component
{
public:
    explicit LooperLayersView (const Looper& looperToShow);

    void paint (juce::Graphics&) override;

private:
    void paintLane (juce::Graphics&, juce::Rectangle<int> wave, int lane, juce::Colour) const;

    const Looper& looper;
};
