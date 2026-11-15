#include "AudioSettingsPanel.h"

AudioSettingsPanel::AudioSettingsPanel (AudioEngine& engineToUse)
    : engine (engineToUse),
      deviceSelector (engine.getDeviceManager(), 0, 256, 1, 256, false, false, false, false)
{
    addAndMakeVisible (deviceSelector);

    guitarInputLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (guitarInputLabel);

    guitarInput.setTextWhenNoChoicesAvailable ("This device has no inputs");
    guitarInput.setTextWhenNothingSelected ("Choose an input");
    guitarInput.onChange = [this]
    {
        if (guitarInput.getSelectedItemIndex() >= 0)
            engine.setInputChannel (guitarInput.getSelectedItemIndex());
    };
    addAndMakeVisible (guitarInput);

    hint.setText ("Play and watch the level bars above. Set Guitar input to the one that moves. It is sent to both ears.\n"
                  "Turn off Direct Monitor on your interface, or you will hear the dry guitar on top.\n"
                  "A smaller buffer size means less delay. Raise it if you hear crackles.",
                  juce::dontSendNotification);
    hint.setJustificationType (juce::Justification::topLeft);
    hint.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
    hint.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (hint);

    activityDb.fill (-60.0f);

    engine.addChangeListener (this);
    refreshInputChoices();
    startTimerHz (30);
    setSize (560, 720);
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

    // Space for the per-input activity meters, painted in paint().
    area.removeFromBottom (maxActivityRows * activityRowHeight + 8);

    // Line the combo box up with the device selector's own rows.
    auto row = area.removeFromBottom (24);
    guitarInputLabel.setBounds (row.removeFromLeft (proportionOfWidth (0.35f) - 12));
    guitarInput.setBounds (row);
    area.removeFromBottom (12);

    deviceSelector.setBounds (area);
}

juce::Rectangle<int> AudioSettingsPanel::getActivityArea() const
{
    return getLocalBounds().reduced (12)
                           .withTrimmedBottom (64 + 8)
                           .removeFromBottom (maxActivityRows * activityRowHeight);
}

void AudioSettingsPanel::paint (juce::Graphics& g)
{
    auto area = getActivityArea();
    const auto labelWidth = proportionOfWidth (0.35f) - 12;
    const auto rows = juce::jmin (inputNames.size(), (int) maxActivityRows);

    g.setFont (juce::FontOptions (13.0f));

    for (int row = 0; row < rows; ++row)
    {
        auto line = area.removeFromTop (activityRowHeight);
        const bool chosen = row == engine.getInputChannel();

        g.setColour (juce::Colours::white.withAlpha (chosen ? 0.9f : 0.5f));
        g.drawText ("Input " + juce::String (row + 1) + " level", line.removeFromLeft (labelWidth).withTrimmedRight (6),
                    juce::Justification::centredRight);

        auto bar = line.reduced (0, 5).toFloat();
        g.setColour (juce::Colour (0xff1b2126));
        g.fillRoundedRectangle (bar, 2.0f);

        const auto proportion = juce::jlimit (0.0f, 1.0f, (activityDb[(size_t) row] + 60.0f) / 60.0f);
        g.setColour (chosen ? juce::Colour (0xff3ecf8e) : juce::Colour (0xff6b7780));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * proportion), 2.0f);
    }
}

void AudioSettingsPanel::timerCallback()
{
    const auto rows = juce::jmin (inputNames.size(), (int) maxActivityRows);

    for (int row = 0; row < rows; ++row)
    {
        const auto db = juce::Decibels::gainToDecibels (engine.readInputChannelPeak (row), -60.0f);
        activityDb[(size_t) row] = juce::jmax (db, activityDb[(size_t) row] - 1.5f);
    }

    repaint (getActivityArea());
}

void AudioSettingsPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshInputChoices();
}

void AudioSettingsPanel::refreshInputChoices()
{
    const auto names = engine.getInputChannelNames();
    inputNames = names;

    guitarInput.clear (juce::dontSendNotification);

    for (int i = 0; i < names.size(); ++i)
        guitarInput.addItem (juce::String (i + 1) + ": " + names[i], i + 1);

    // Leave the box empty when the saved channel does not exist on this device.
    if (engine.getInputChannel() < names.size())
        guitarInput.setSelectedItemIndex (engine.getInputChannel(), juce::dontSendNotification);
}
