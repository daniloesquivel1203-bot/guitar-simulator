#pragma once

#include "../engine/AudioEngine.h"
#include "TonePanels.h"

/** A chromatic tuner. Shows the note being played and how sharp or flat it is. */
class TunerPanel final : public TonePanel,
                         private juce::Timer
{
public:
    explicit TunerPanel (AudioEngine& engineToUse);
    ~TunerPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshPower() override {}

private:
    static constexpr double inTuneCents = 3.0;
    static constexpr int holdUpdates = 8;   // keep the last reading briefly as a note fades

    void timerCallback() override;

    AudioEngine& engine;
    juce::ToggleButton muteWhileTuning { "Mute output while tuning" };
    std::vector<float> window = std::vector<float> ((size_t) TunerTap::windowSize);

    bool hasReading = false;
    TunerReading reading;
    double shownCents = 0.0;
    int updatesSinceReading = 0;
};
