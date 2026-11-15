#pragma once

#include "../engine/AudioEngine.h"

#include <juce_audio_utils/juce_audio_utils.h>

/** The audio settings screen: driver type, device, sample rate, buffer size,
    enabled channels, and which input channel the guitar is plugged into. */
class AudioSettingsPanel final : public juce::Component,
                                 private juce::ChangeListener
{
public:
    explicit AudioSettingsPanel (AudioEngine& engineToUse);
    ~AudioSettingsPanel() override;

    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshInputChoices();

    AudioEngine& engine;
    juce::AudioDeviceSelectorComponent deviceSelector;
    juce::Label guitarInputLabel { {}, "Guitar input" };
    juce::ComboBox guitarInput;
    juce::Label hint;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioSettingsPanel)
};
