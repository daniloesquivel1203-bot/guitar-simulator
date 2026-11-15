#include "AudioSettingsPanel.h"

AudioSettingsPanel::AudioSettingsPanel (AudioEngine& engineToUse)
    : engine (engineToUse),
      deviceSelector (engine.getDeviceManager(), 0, 256, 1, 256, false, false, false, false)
{
    addAndMakeVisible (deviceSelector);

    guitarInputLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (guitarInputLabel);

    guitarInput.setTextWhenNoChoicesAvailable ("No active input channels");
    guitarInput.onChange = [this]
    {
        if (guitarInput.getSelectedItemIndex() >= 0)
            engine.setInputChannel (guitarInput.getSelectedItemIndex());
    };
    addAndMakeVisible (guitarInput);

    hint.setText ("Pick the channel your guitar is plugged into. It is sent to both ears.\n"
                  "Turn off Direct Monitor on your interface, or you will hear the dry guitar on top.\n"
                  "A smaller buffer size means less delay. Raise it if you hear crackles.",
                  juce::dontSendNotification);
    hint.setJustificationType (juce::Justification::topLeft);
    hint.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
    hint.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (hint);

    engine.addChangeListener (this);
    refreshInputChoices();
    setSize (560, 620);
}

AudioSettingsPanel::~AudioSettingsPanel()
{
    engine.removeChangeListener (this);
}

void AudioSettingsPanel::resized()
{
    auto area = getLocalBounds().reduced (12);

    hint.setBounds (area.removeFromBottom (64));
    area.removeFromBottom (8);

    // Line the combo box up with the device selector's own rows.
    auto row = area.removeFromBottom (24);
    guitarInputLabel.setBounds (row.removeFromLeft (proportionOfWidth (0.35f) - 12));
    guitarInput.setBounds (row);
    area.removeFromBottom (12);

    deviceSelector.setBounds (area);
}

void AudioSettingsPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshInputChoices();
}

void AudioSettingsPanel::refreshInputChoices()
{
    const auto names = engine.getActiveInputChannelNames();

    guitarInput.clear (juce::dontSendNotification);

    for (int i = 0; i < names.size(); ++i)
        guitarInput.addItem (juce::String (i + 1) + ": " + names[i], i + 1);

    if (! names.isEmpty())
        guitarInput.setSelectedItemIndex (juce::jlimit (0, names.size() - 1, engine.getInputChannel()),
                                          juce::dontSendNotification);
}
