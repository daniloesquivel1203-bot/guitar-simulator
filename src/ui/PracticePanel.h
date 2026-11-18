#pragma once

#include "../engine/AudioEngine.h"
#include "TonePanels.h"

/** The practice tools: a metronome with tap tempo, a looper, and a recorder
    that saves takes as WAV files. They keep running when the panel is closed. */
class PracticePanel final : public TonePanel,
                            private juce::Timer
{
public:
    explicit PracticePanel (AudioEngine& engineToUse);
    ~PracticePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshPower() override {}

    /** Where takes are saved. */
    static juce::File getRecordingsFolder();

    /** True while anything here is running, so the header can show it. */
    static bool isBusy (PracticeTools&);

private:
    void timerCallback() override;

    void applySettings();
    void refreshMetronome();
    void refreshLooper();
    void refreshRecorder();
    void toggleRecording();

    AudioEngine& engine;
    PracticeTools& tools;
    TapTempo tapTempo;

    juce::Rectangle<int> metronomeArea, looperArea, recorderArea;
    juce::Rectangle<int> beatLightsArea, loopBarArea;

    // Metronome
    juce::TextButton clickButton { "Start" };
    juce::Slider bpm;
    juce::TextButton tapButton { "Tap" };
    juce::ComboBox beatsPerBar;
    juce::ToggleButton accent { "Accent beat 1" };
    juce::Label clickVolumeLabel { {}, "Volume" };
    juce::Slider clickVolume;

    // Looper
    juce::TextButton loopButton { "Record" };
    juce::TextButton stopButton { "Stop" };
    juce::TextButton undoButton { "Undo" };
    juce::TextButton clearButton { "Clear" };
    juce::Label loopStatus;
    juce::Label loopLevelLabel { {}, "Level" };
    juce::Slider loopLevel;

    // Recorder
    juce::TextButton recordButton { "Record take" };
    juce::Label recordTime;
    juce::Label recordStatus;
    juce::TextButton openFolderButton { "Open folder" };

    int shownBeat = -1;
};
