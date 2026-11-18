#pragma once

#include "engine/AudioEngine.h"
#include "ui/ChainStrip.h"
#include "ui/LevelMeter.h"
#include "ui/PresetBar.h"
#include "ui/TonePanels.h"

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
    PresetBar presetBar;
    juce::TextButton tunerButton { "Tuner" };
    juce::TextButton practiceButton { "Practice" };
    juce::TextButton settingsButton { "Audio settings" };

    juce::Label inputHeading { {}, "INPUT" };
    LevelMeter inputMeter { true };
    juce::Slider inputGain;

    juce::Label outputHeading { {}, "OUTPUT" };
    LevelMeter outputMeter { false };
    juce::Slider outputGain;
    juce::ToggleButton muteButton { "Mute" };

    /** Redraws the strip from the board. The selection stays on the same block
        if it still exists, otherwise on the tile at `fallbackIndex`, otherwise the amp. */
    void rebuildChain (int fallbackIndex = -1);
    void selectBlock (int index);
    void selectAmp();
    void showTuner();
    void showPractice();
    void showAddMenu (Pedalboard::Side, juce::Rectangle<int> screenArea);
    void showPedalMenu (int tile);
    void removePedal (Pedalboard::Side, int index);

    /** One per tile in the chain strip, in signal order. Empty for + tiles. */
    std::vector<std::function<std::unique_ptr<TonePanel>()>> panelMakers;

    /** Keeps the pedal behind the open panel alive, even after a preset has replaced it. */
    std::vector<std::shared_ptr<Pedal>> tilePedals;
    std::shared_ptr<Pedal> panelPedal;
    ChainStrip chainStrip;

    /** Identify blocks across rebuilds by their on/off switch: the one that
        should be selected, and the one whose panel is showing. */
    Parameter* selectedBlock = nullptr;
    Parameter* panelBlock = nullptr;
    bool showingTuner = false;
    bool showingPractice = false;
    std::unique_ptr<TonePanel> detailPanel;

    int ticksSinceStatusRefresh = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
