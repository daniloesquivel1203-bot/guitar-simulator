#pragma once

#include "../engine/AudioEngine.h"
#include "TonePanels.h"

#include <array>

/** A guitar tuner. Pick a tuning, then either let it find the string you are
    playing or choose one of the six strings yourself. */
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
    static constexpr int numStrings = 6;
    static constexpr int autoString = -1;
    static constexpr double inTuneCents = 3.0;
    static constexpr double scaleCents = 50.0;
    static constexpr int holdUpdates = 8;   // keep the last reading briefly as a note fades

    void timerCallback() override;

    const Tuning& getTuning() const;
    void tuningChanged();
    void chooseString (int string);
    void refreshStringButtons();

    AudioEngine& engine;
    juce::ComboBox tuningPicker;
    juce::ToggleButton muteWhileTuning { "Mute output" };
    juce::TextButton autoButton { "Auto" };
    std::array<juce::TextButton, numStrings> stringButtons;
    std::vector<float> window = std::vector<float> ((size_t) TunerTap::windowSize);

    int tuningIndex = 0;
    int chosenString = autoString;   // what the user picked
    int activeString = autoString;   // the string being measured right now

    bool hasReading = false;
    double hz = 0.0;
    double shownCents = 0.0;
    int updatesSinceReading = 0;
};
