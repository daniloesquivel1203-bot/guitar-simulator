#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

/** Top-level content of the app window.

    Milestone 1: a placeholder. The signal chain, meters and audio settings
    are added here in later milestones.
*/
class MainComponent final : public juce::Component
{
public:
    MainComponent();

    void paint (juce::Graphics&) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
