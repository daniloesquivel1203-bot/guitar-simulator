#include "TunerPanel.h"

namespace
{
const juce::Colour good { 0xff3ecf8e };
const juce::Colour off { 0xfff5a524 };
}

TunerPanel::TunerPanel (AudioEngine& engineToUse)
    : TonePanel ("TUNER"), engine (engineToUse)
{
    muteWhileTuning.setToggleState (true, juce::dontSendNotification);
    muteWhileTuning.onClick = [this] { engine.getChain().setTunerMuted (muteWhileTuning.getToggleState()); };
    addAndMakeVisible (muteWhileTuning);

    engine.getChain().setTunerMuted (true);
    engine.getTunerTap().setActive (true);
    startTimerHz (20);
}

TunerPanel::~TunerPanel()
{
    engine.getTunerTap().setActive (false);
    engine.getChain().setTunerMuted (false);
}

void TunerPanel::resized()
{
    muteWhileTuning.setBounds (getHeaderArea().removeFromRight (220));
}

void TunerPanel::timerCallback()
{
    engine.getTunerTap().copyLatest (window.data());

    const auto sampleRate = engine.getStatus().sampleRate;
    const auto pitch = sampleRate > 0.0 ? PitchDetector::detect (window.data(), (int) window.size(), sampleRate)
                                        : std::nullopt;

    if (pitch.has_value())
    {
        const auto latest = TunerReading::fromHz (*pitch);

        // Glide the needle within a note, and jump when the note changes.
        const bool sameNote = hasReading && latest.note == reading.note && latest.octave == reading.octave;
        shownCents = sameNote ? shownCents + 0.5 * (latest.cents - shownCents) : latest.cents;

        reading = latest;
        hasReading = true;
        updatesSinceReading = 0;
    }
    else if (hasReading && ++updatesSinceReading > holdUpdates)
    {
        hasReading = false;
    }

    repaint();
}

void TunerPanel::paint (juce::Graphics& g)
{
    TonePanel::paint (g);

    auto area = getContentArea();
    const bool inTune = hasReading && std::abs (shownCents) <= inTuneCents;
    const auto accent = inTune ? good : off;

    // Note name.
    auto noteArea = area.removeFromTop (area.getHeight() * 55 / 100);
    g.setFont (juce::FontOptions (juce::jmin (96.0f, (float) noteArea.getHeight() * 0.8f), juce::Font::bold));
    g.setColour (hasReading ? accent : juce::Colours::white.withAlpha (0.25f));
    g.drawText (hasReading ? reading.note + juce::String (reading.octave) : juce::String ("--"),
                noteArea, juce::Justification::centred);

    // Cents scale from -50 to +50.
    auto scale = area.removeFromTop (44).reduced (40, 0).toFloat();
    const auto track = scale.withSizeKeepingCentre (scale.getWidth(), 6.0f);
    g.setColour (juce::Colour (0xff1b2126));
    g.fillRoundedRectangle (track, 3.0f);

    const auto xForCents = [&track] (double cents)
    {
        return track.getX() + track.getWidth() * (float) ((juce::jlimit (-50.0, 50.0, cents) + 50.0) / 100.0);
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
    juce::String advice = "Play one open string";

    if (hasReading)
    {
        const auto cents = juce::roundToInt (shownCents);
        advice = inTune ? juce::String ("In tune")
                        : cents < 0 ? juce::String (-cents) + " cents flat. Tighten the string."
                                    : juce::String (cents) + " cents sharp. Loosen the string.";
        advice << "    " << juce::String (reading.hz, 1) << " Hz";
    }

    g.setFont (juce::FontOptions (15.0f));
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.drawText (advice, area.removeFromTop (30), juce::Justification::centred);
}
