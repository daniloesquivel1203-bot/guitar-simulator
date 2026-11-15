#pragma once

#include "../engine/AudioEngine.h"

#include <juce_audio_utils/juce_audio_utils.h>

/** The audio settings screen: driver type, device, sample rate, buffer size,
    enabled channels, and which input channel the guitar is plugged into. */
class AudioSettingsPanel final : public juce::Component,
                                 private juce::ChangeListener,
                                 private juce::Timer
{
public:
    explicit AudioSettingsPanel (AudioEngine& engineToUse);
    ~AudioSettingsPanel() override;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    static constexpr int maxActivityRows = 8;
    static constexpr int activityRowHeight = 18;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    juce::Rectangle<int> getActivityArea() const;
    void refreshInputChoices();

    AudioEngine& engine;
    juce::AudioDeviceSelectorComponent deviceSelector;
    juce::Label guitarInputLabel { {}, "Guitar input" };
    juce::ComboBox guitarInput;
    juce::Label hint;

    juce::StringArray inputNames;
    std::array<float, maxActivityRows> activityDb {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioSettingsPanel)
};
