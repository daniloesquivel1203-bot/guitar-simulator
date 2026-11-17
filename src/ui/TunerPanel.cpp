#include "TunerPanel.h"

namespace
{
const juce::Colour good { 0xff3ecf8e };
const juce::Colour off { 0xfff5a524 };
const juce::Colour highlight { 0xff3ea6cf };
}

TunerPanel::TunerPanel (AudioEngine& engineToUse)
    : TonePanel ("TUNER"), engine (engineToUse)
{
    const auto& tunings = Tuning::getAll();

    for (int i = 0; i < (int) tunings.size(); ++i)
        tuningPicker.addItem (tunings[(size_t) i].name, i + 1);

    tuningIndex = juce::jlimit (0, (int) tunings.size() - 1, engine.getTunerTuning());
    tuningPicker.setSelectedId (tuningIndex + 1, juce::dontSendNotification);
    tuningPicker.setTooltip ("The tuning you want the guitar to be in.");
    tuningPicker.onChange = [this] { tuningChanged(); };
    addAndMakeVisible (tuningPicker);

    muteWhileTuning.setToggleState (true, juce::dontSendNotification);
    muteWhileTuning.setTooltip ("Silences the output while the tuner is open.");
    muteWhileTuning.onClick = [this] { engine.getChain().setTunerMuted (muteWhileTuning.getToggleState()); };
    addAndMakeVisible (muteWhileTuning);

    autoButton.setTooltip ("Finds the string you are playing by itself.");
    autoButton.onClick = [this] { chooseString (autoString); };
    addAndMakeVisible (autoButton);

    for (int i = 0; i < numStrings; ++i)
    {
        auto& button = stringButtons[(size_t) i];
        button.setTooltip ("Tune string " + juce::String (numStrings - i)
                           + (i == 0 ? " (the thickest)." : i == numStrings - 1 ? " (the thinnest)." : "."));
        button.onClick = [this, i] { chooseString (i); };
        addAndMakeVisible (button);
    }

    for (auto* button : { &autoButton, &stringButtons[0], &stringButtons[1], &stringButtons[2],
                          &stringButtons[3], &stringButtons[4], &stringButtons[5] })
    {
        button->setColour (juce::TextButton::buttonOnColourId, highlight);
        button->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    }

    chosenString = juce::jlimit ((int) autoString, numStrings - 1, engine.getTunerString());
    activeString = chosenString;
    refreshStringButtons();

    engine.getChain().setTunerMuted (true);
    engine.getTunerTap().setActive (true);
    startTimerHz (20);
}

TunerPanel::~TunerPanel()
{
    engine.getTunerTap().setActive (false);
    engine.getChain().setTunerMuted (false);
}

const Tuning& TunerPanel::getTuning() const
{
    return Tuning::getAll()[(size_t) tuningIndex];
}

void TunerPanel::tuningChanged()
{
    tuningIndex = juce::jlimit (0, (int) Tuning::getAll().size() - 1, tuningPicker.getSelectedId() - 1);
    engine.setTunerTuning (tuningIndex);
    hasReading = false;
    inTune = false;
    refreshStringButtons();
    repaint();
}

void TunerPanel::chooseString (int string)
{
    chosenString = string;
    activeString = string;
    engine.setTunerString (string);
    hasReading = false;
    inTune = false;
    refreshStringButtons();
    repaint();
}

void TunerPanel::refreshStringButtons()
{
    autoButton.setToggleState (chosenString == autoString, juce::dontSendNotification);

    for (int i = 0; i < numStrings; ++i)
    {
        auto& button = stringButtons[(size_t) i];
        button.setButtonText (Tuning::noteName (getTuning().notes[(size_t) i]));
        button.setToggleState (i == activeString, juce::dontSendNotification);
    }
}

void TunerPanel::resized()
{
    auto header = getHeaderArea();
    muteWhileTuning.setBounds (header.removeFromRight (120));
    header.removeFromRight (12);
    tuningPicker.setBounds (header.removeFromRight (270));

    // One row: Auto, then the six strings from thickest to thinnest.
    auto row = getContentArea().removeFromBottom (44).reduced (20, 2);
    const auto gap = 8;
    const auto width = (row.getWidth() - gap * numStrings) / (numStrings + 1);

    autoButton.setBounds (row.removeFromLeft (width));

    for (auto& button : stringButtons)
    {
        row.removeFromLeft (gap);
        button.setBounds (row.removeFromLeft (width));
    }
}

void TunerPanel::timerCallback()
{
    engine.getTunerTap().copyLatest (window.data());

    const auto sampleRate = engine.getStatus().sampleRate;
    const auto pitch = sampleRate > 0.0 ? tracker.update (window.data(), (int) window.size(), sampleRate)
                                        : std::nullopt;

    if (pitch.has_value())
    {
        const auto string = chosenString == autoString ? getTuning().closestString (*pitch) : chosenString;
        const bool newNote = ! hasReading || string != activeString;

        if (string != activeString)
        {
            activeString = string;
            refreshStringButtons();
        }

        // The tracker has already steadied the pitch, so the needle shows it directly.
        shownCents = Tuning::centsFrom (*pitch, getTuning().notes[(size_t) string]);
        inTune = std::abs (shownCents) <= (inTune && ! newNote ? outOfTuneCents : inTuneCents);

        if (newNote || ++updatesSinceText >= textEveryUpdates)
        {
            textCents = shownCents;
            textHz = *pitch;
            updatesSinceText = 0;
        }

        hasReading = true;
        updatesSinceReading = 0;
    }
    else if (hasReading && ++updatesSinceReading > holdUpdates)
    {
        // Keep the last reading on screen for a second after the note fades.
        hasReading = false;
        inTune = false;
    }

    repaint();
}

void TunerPanel::paint (juce::Graphics& g)
{
    TonePanel::paint (g);

    auto area = getContentArea();
    area.removeFromBottom (44 + 8);   // the string buttons

    const bool hasTarget = activeString != autoString;
    const auto accent = inTune ? good : off;

    // The note this string should be tuned to.
    auto noteArea = area.removeFromTop (area.getHeight() * 50 / 100);
    const auto target = hasTarget ? Tuning::noteName (getTuning().notes[(size_t) activeString]) : juce::String ("--");

    g.setFont (juce::FontOptions (juce::jmin (84.0f, (float) noteArea.getHeight() * 0.85f), juce::Font::bold));
    g.setColour (hasReading ? accent : juce::Colours::white.withAlpha (hasTarget ? 0.6f : 0.25f));
    g.drawText (target, noteArea, juce::Justification::centred);

    if (hasTarget)
    {
        g.setFont (juce::FontOptions (13.0f));
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.drawText ("String " + juce::String (numStrings - activeString), noteArea.removeFromTop (16),
                    juce::Justification::centred);
    }

    // Scale from 50 cents flat to 50 cents sharp of the target note.
    auto scale = area.removeFromTop (40).reduced (40, 0).toFloat();
    const auto track = scale.withSizeKeepingCentre (scale.getWidth(), 6.0f);
    g.setColour (juce::Colour (0xff1b2126));
    g.fillRoundedRectangle (track, 3.0f);

    const auto xForCents = [&track] (double cents)
    {
        return track.getX() + track.getWidth() * (float) ((juce::jlimit (-scaleCents, scaleCents, cents) + scaleCents)
                                                           / (2.0 * scaleCents));
    };

    g.setColour (good.withAlpha (0.35f));
    g.fillRoundedRectangle (juce::Rectangle<float> (xForCents (-inTuneCents), track.getY(),
                                                    xForCents (inTuneCents) - xForCents (-inTuneCents), track.getHeight()), 3.0f);

    g.setColour (juce::Colours::white.withAlpha (0.4f));

    for (auto mark : { -50.0, -25.0, 0.0, 25.0, 50.0 })
        g.fillRect (xForCents (mark) - 0.5f, track.getY() - 8.0f, 1.0f, 22.0f);

    if (hasReading)
    {
        g.setColour (accent);
        g.fillRoundedRectangle (juce::Rectangle<float> (xForCents (shownCents) - 4.0f, scale.getY(), 8.0f, scale.getHeight()), 3.0f);
    }

    // Plain-language instruction underneath.
    juce::String advice = chosenString == autoString ? "Play one open string"
                                                     : "Play string " + juce::String (numStrings - chosenString);

    if (hasReading)
    {
        const auto cents = juce::roundToInt (textCents);

        if (inTune)
            advice = "In tune";
        else if (std::abs (textCents) > scaleCents)
            advice = textCents < 0.0 ? "Too low. Tighten the string." : "Too high. Loosen the string.";
        else
            advice = cents < 0 ? juce::String (-cents) + " cents flat. Tighten the string."
                               : juce::String (cents) + " cents sharp. Loosen the string.";

        advice << "    " << juce::String (textHz, 1) << " Hz";
    }

    g.setFont (juce::FontOptions (15.0f));
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.drawText (advice, area.removeFromTop (28), juce::Justification::centred);
}
