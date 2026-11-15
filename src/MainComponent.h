#pragma once

#include "engine/AudioEngine.h"
#include "ui/LevelMeter.h"

#include <juce_gui_extra/juce_gui_extra.h>

/** Top-level content of the app window: status bar, input and output strips,
    and the space where the pedalboard and amp will go. */
class MainComponent final : public juce::Component,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    explicit MainComponent (AudioEngine& engineToUse);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void refreshStatus();
    void showAudioSettings();
    static void styleGainSlider (juce::Slider&, double minDb, double maxDb, double valueDb);

    AudioEngine& engine;

    juce::Label title { {}, "Guitar Simulator" };
    juce::Label statusLine;
    juce::Label detailLine;
    juce::TextButton settingsButton { "Audio settings" };

    juce::Label inputHeading { {}, "INPUT" };
    LevelMeter inputMeter { true };
    juce::Slider inputGain;

    juce::Label outputHeading { {}, "OUTPUT" };
    LevelMeter outputMeter { false };
    juce::Slider outputGain;
    juce::ToggleButton muteButton { "Mute" };

    int ticksSinceStatusRefresh = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
