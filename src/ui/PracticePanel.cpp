#include "PracticePanel.h"

#include "Dialogs.h"
#include "Theme.h"

namespace
{
const char* const bpmKey = "practice.bpm";
const char* const beatsKey = "practice.beatsPerBar";
const char* const clickVolumeKey = "practice.clickVolumeDb";
const char* const accentKey = "practice.accent";
const char* const loopLevelKey = "practice.loopLevelDb";

constexpr int rowHeight = 28;
constexpr int bigRowHeight = 40;
constexpr int gap = 8;

juce::String formatTime (double seconds)
{
    const auto whole = (int) seconds;
    return juce::String (whole / 60) + ":" + juce::String (whole % 60).paddedLeft ('0', 2);
}

juce::String layersText (int layers)
{
    return juce::String (layers) + (layers == 1 ? " layer" : " layers");
}

void styleHorizontalSlider (juce::Slider& slider, double min, double max, double step, const juce::String& suffix,
                            double value, double resetValue)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 22);
    slider.setRange (min, max, step);
    slider.setTextValueSuffix (suffix);
    slider.setDoubleClickReturnValue (true, resetValue);
    slider.setValue (value, juce::dontSendNotification);
}

void styleCaption (juce::Label& label)
{
    label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
}
}

juce::File PracticePanel::getRecordingsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Guitar Simulator")
        .getChildFile ("Recordings");
}

bool PracticePanel::isBusy (PracticeTools& tools)
{
    return tools.getMetronome().isRunning()
           || tools.getRecorder().isRecording()
           || tools.getLooper().getState() == Looper::State::recording
           || tools.getLooper().getState() == Looper::State::playing
           || tools.getLooper().getState() == Looper::State::overdubbing;
}

PracticePanel::PracticePanel (AudioEngine& engineToUse)
    : TonePanel ("PRACTICE"), engine (engineToUse), tools (engine.getPractice()),
      layersView (tools.getLooper())
{
    applySettings();

    auto& metronome = tools.getMetronome();
    auto& settings = engine.getSettings();

    // Metronome
    clickButton.setClickingTogglesState (true);
    clickButton.setColour (juce::TextButton::buttonOnColourId, Theme::accent);
    clickButton.setTooltip ("Starts or stops the click.");
    clickButton.onClick = [this, &metronome]
    {
        metronome.setRunning (clickButton.getToggleState());
        refreshMetronome();
    };
    addAndMakeVisible (clickButton);

    styleHorizontalSlider (bpm, Metronome::minBpm, Metronome::maxBpm, 1.0, " BPM", metronome.getBpm(), 100.0);
    bpm.setTooltip ("Tempo in beats per minute. Drag, or click the number to type one.");
    bpm.onValueChange = [this, &metronome, &settings]
    {
        metronome.setBpm (bpm.getValue());
        settings.setValue (bpmKey, bpm.getValue());
    };
    addAndMakeVisible (bpm);

    tapButton.setTooltip ("Tap along with the beat, four times or more, to set the tempo.");
    tapButton.onClick = [this]
    {
        if (const auto tapped = tapTempo.tap (juce::Time::getMillisecondCounterHiRes() / 1000.0))
            bpm.setValue (std::round (*tapped), juce::sendNotificationSync);
    };
    addAndMakeVisible (tapButton);

    for (int beats = 1; beats <= Metronome::maxBeatsPerBar; ++beats)
        beatsPerBar.addItem (juce::String (beats) + (beats == 1 ? " beat per bar" : " beats per bar"), beats);

    beatsPerBar.setSelectedId (metronome.getBeatsPerBar(), juce::dontSendNotification);
    beatsPerBar.onChange = [this, &metronome, &settings]
    {
        metronome.setBeatsPerBar (beatsPerBar.getSelectedId());
        settings.setValue (beatsKey, beatsPerBar.getSelectedId());
        repaint();
    };
    addAndMakeVisible (beatsPerBar);

    accent.setToggleState (metronome.getAccentFirstBeat(), juce::dontSendNotification);
    accent.setTooltip ("Plays a higher click on the first beat of each bar.");
    accent.onClick = [this, &metronome, &settings]
    {
        metronome.setAccentFirstBeat (accent.getToggleState());
        settings.setValue (accentKey, accent.getToggleState());
    };
    addAndMakeVisible (accent);

    styleCaption (clickVolumeLabel);
    addAndMakeVisible (clickVolumeLabel);
    styleHorizontalSlider (clickVolume, Metronome::minVolumeDb, Metronome::maxVolumeDb, 0.5, " dB",
                           metronome.getVolumeDb(), -12.0);
    clickVolume.setTooltip ("How loud the click is. It is never recorded or looped.");
    clickVolume.onValueChange = [this, &metronome, &settings]
    {
        metronome.setVolumeDb ((float) clickVolume.getValue());
        settings.setValue (clickVolumeKey, clickVolume.getValue());
    };
    addAndMakeVisible (clickVolume);

    // Looper
    auto& looper = tools.getLooper();

    loopButton.setTooltip ("Press to record a phrase, then press Play to start it looping.");
    loopButton.onClick = [&looper] { looper.pressMain(); };
    addAndMakeVisible (loopButton);

    overdubButton.setTooltip ("Starts adding a new layer on top of the loop. Press again to keep it.");
    overdubButton.onClick = [&looper] { looper.pressOverdub(); };
    addAndMakeVisible (overdubButton);

    stopButton.setTooltip ("Stops the loop. Play starts it again from the beginning.");
    stopButton.onClick = [&looper] { looper.pressStop(); };
    addAndMakeVisible (stopButton);

    undoButton.setTooltip ("Takes back the newest layer. Press again to go further back.");
    undoButton.onClick = [&looper] { looper.pressUndo(); };
    addAndMakeVisible (undoButton);

    clearButton.setTooltip ("Throws the loop away so you can record a new one.");
    clearButton.onClick = [&looper] { looper.pressClear(); };
    addAndMakeVisible (clearButton);

    loopStatus.setFont (juce::FontOptions (13.0f));
    loopStatus.setJustificationType (juce::Justification::topLeft);
    loopStatus.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    addAndMakeVisible (loopStatus);

    styleCaption (loopLevelLabel);
    addAndMakeVisible (loopLevelLabel);
    styleHorizontalSlider (loopLevel, Looper::minLevelDb, Looper::maxLevelDb, 0.5, " dB", looper.getLevelDb(), 0.0);
    loopLevel.setTooltip ("How loud the loop plays back.");
    loopLevel.onValueChange = [this, &looper, &settings]
    {
        looper.setLevelDb ((float) loopLevel.getValue());
        settings.setValue (loopLevelKey, loopLevel.getValue());
    };
    addAndMakeVisible (loopLevel);
    addAndMakeVisible (layersView);

    // Recorder
    recordButton.setClickingTogglesState (false);
    recordButton.setColour (juce::TextButton::buttonOnColourId, Theme::danger);
    recordButton.setTooltip ("Records what you hear, plus the plain guitar signal, to WAV files.");
    recordButton.onClick = [this] { toggleRecording(); };
    addAndMakeVisible (recordButton);

    recordTime.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    recordTime.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (recordTime);

    recordStatus.setFont (juce::FontOptions (13.0f));
    recordStatus.setJustificationType (juce::Justification::topLeft);
    recordStatus.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    recordStatus.setText ("Each take saves two files: what you hear, and the dry guitar so it can be "
                          "played through a different sound later.",
                          juce::dontSendNotification);
    addAndMakeVisible (recordStatus);

    openFolderButton.setTooltip ("Opens " + getRecordingsFolder().getFullPathName());
    openFolderButton.onClick = []
    {
        const auto folder = getRecordingsFolder();
        folder.createDirectory();
        folder.startAsProcess();
    };
    addAndMakeVisible (openFolderButton);

    refreshMetronome();
    refreshLooper();
    refreshRecorder();
    startTimerHz (30);
}

PracticePanel::~PracticePanel() = default;

void PracticePanel::applySettings()
{
    auto& settings = engine.getSettings();
    auto& metronome = tools.getMetronome();

    metronome.setBpm (settings.getDoubleValue (bpmKey, 100.0));
    metronome.setBeatsPerBar (settings.getIntValue (beatsKey, 4));
    metronome.setVolumeDb ((float) settings.getDoubleValue (clickVolumeKey, -12.0));
    metronome.setAccentFirstBeat (settings.getBoolValue (accentKey, true));
    tools.getLooper().setLevelDb ((float) settings.getDoubleValue (loopLevelKey, 0.0));
}

void PracticePanel::resized()
{
    auto content = getContentArea();

    // The looper gets the widest column, to fit its layers.
    const auto sideWidth = (content.getWidth() - 2 * 16) * 27 / 100;

    metronomeArea = content.removeFromLeft (sideWidth);
    content.removeFromLeft (16);
    recorderArea = content.removeFromRight (sideWidth);
    content.removeFromRight (16);
    looperArea = content;

    const auto inner = [] (juce::Rectangle<int> column) { return column.reduced (12).withTrimmedTop (22); };

    // Metronome
    {
        auto area = inner (metronomeArea);
        auto row = area.removeFromTop (bigRowHeight);
        tapButton.setBounds (row.removeFromRight (row.getWidth() / 3));
        row.removeFromRight (gap);
        clickButton.setBounds (row);

        area.removeFromTop (gap);
        bpm.setBounds (area.removeFromTop (rowHeight));
        area.removeFromTop (gap);
        beatsPerBar.setBounds (area.removeFromTop (rowHeight - 2));
        area.removeFromTop (4);
        accent.setBounds (area.removeFromTop (rowHeight - 4));
        area.removeFromTop (4);

        row = area.removeFromTop (rowHeight);
        clickVolumeLabel.setBounds (row.removeFromLeft (56));
        clickVolume.setBounds (row);

        area.removeFromTop (gap);
        beatLightsArea = area.removeFromTop (18);
    }

    // Looper
    {
        auto area = inner (looperArea);

        // The buttons share one row, which leaves the height for the layers.
        auto row = area.removeFromTop (bigRowHeight - 4);
        const auto mainWidth = (row.getWidth() - 4 * gap) * 27 / 100;
        loopButton.setBounds (row.removeFromLeft (mainWidth));
        row.removeFromLeft (gap);
        overdubButton.setBounds (row.removeFromLeft (mainWidth));
        row.removeFromLeft (gap);
        const auto third = (row.getWidth() - 2 * gap) / 3;
        stopButton.setBounds (row.removeFromLeft (third));
        row.removeFromLeft (gap);
        undoButton.setBounds (row.removeFromLeft (third));
        row.removeFromLeft (gap);
        clearButton.setBounds (row);

        area.removeFromTop (4);
        auto levelRow = area.removeFromTop (rowHeight);
        loopLevelLabel.setBounds (levelRow.removeFromLeft (56));
        loopLevel.setBounds (levelRow);

        loopStatus.setBounds (area.removeFromBottom (36));
        area.removeFromBottom (4);

        area.removeFromTop (4);
        loopBarArea = area.removeFromTop (6);
        area.removeFromTop (4);
        layersView.setBounds (area);
    }

    // Recorder
    {
        auto area = inner (recorderArea);
        recordButton.setBounds (area.removeFromTop (bigRowHeight));
        area.removeFromTop (gap);
        recordTime.setBounds (area.removeFromTop (36));
        openFolderButton.setBounds (area.removeFromBottom (rowHeight));
        area.removeFromBottom (gap);
        recordStatus.setBounds (area);
    }
}

void PracticePanel::paint (juce::Graphics& g)
{
    TonePanel::paint (g);

    for (auto [column, heading] : { std::pair { metronomeArea, "METRONOME" },
                                    std::pair { looperArea, "LOOPER" },
                                    std::pair { recorderArea, "RECORD TO WAV" } })
    {
        g.setColour (Theme::sunken.withAlpha (0.6f));
        g.fillRoundedRectangle (column.toFloat(), 6.0f);

        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (heading, column.reduced (12, 8).removeFromTop (16), juce::Justification::centredLeft);
    }

    // One light per beat; the first is larger when it is accented.
    const auto beats = tools.getMetronome().getBeatsPerBar();
    const auto size = (float) juce::jmin (beatLightsArea.getHeight(), (beatLightsArea.getWidth() - (beats - 1) * 6) / beats);
    auto x = (float) beatLightsArea.getX();

    for (int beat = 0; beat < beats; ++beat)
    {
        const auto light = juce::Rectangle<float> (x, (float) beatLightsArea.getCentreY() - size / 2.0f, size, size).reduced (2.0f);
        const bool lit = beat == shownBeat;
        const auto colour = beat == 0 && accent.getToggleState() && beats > 1 ? Theme::warning : Theme::accent;

        g.setColour (lit ? colour : Theme::raised);
        g.fillEllipse (light);
        x += size + 6.0f;
    }

    // Loop progress.
    auto& looper = tools.getLooper();
    const auto state = looper.getState();
    g.setColour (Theme::sunken);
    g.fillRoundedRectangle (loopBarArea.toFloat(), 4.0f);

    float fraction = 0.0f;
    auto barColour = Theme::good;

    if (state == Looper::State::recording)
    {
        fraction = (float) looper.getLength() / (float) juce::jmax (1, looper.getCapacity());
        barColour = Theme::danger;
    }
    else if (looper.getLength() > 0 && state != Looper::State::empty)
    {
        fraction = (float) looper.getPosition() / (float) looper.getLength();
        barColour = state == Looper::State::overdubbing ? Theme::warning
                  : state == Looper::State::stopped     ? Theme::raised
                                                        : Theme::good;
    }

    if (fraction > 0.0f)
    {
        g.setColour (barColour);
        g.fillRoundedRectangle (loopBarArea.toFloat().withWidth ((float) loopBarArea.getWidth() * juce::jlimit (0.0f, 1.0f, fraction)), 4.0f);
    }
}

void PracticePanel::timerCallback()
{
    const auto beat = tools.getMetronome().getCurrentBeat();

    if (beat != shownBeat)
    {
        shownBeat = beat;
        repaint (beatLightsArea);
    }

    refreshLooper();
    refreshRecorder();
    repaint (loopBarArea);
    layersView.repaint();
}

void PracticePanel::refreshMetronome()
{
    const auto running = tools.getMetronome().isRunning();
    clickButton.setToggleState (running, juce::dontSendNotification);
    clickButton.setButtonText (running ? "Stop" : "Start");
}

void PracticePanel::refreshLooper()
{
    auto& looper = tools.getLooper();
    const auto state = looper.getState();
    const auto rate = looper.getSampleRate();
    const auto seconds = rate > 0.0 ? looper.getLength() / rate : 0.0;

    juce::String text;
    juce::String action;
    auto colour = Theme::raised;

    switch (state)
    {
        case Looper::State::empty:
            action = "Record";
            colour = Theme::danger.withMultipliedSaturation (0.7f);
            text = "Press Record and play a phrase. Press again when it should start repeating. "
                   "Up to " + juce::String ((int) Looper::maxLoopSeconds) + " seconds.";
            break;

        case Looper::State::recording:
            action = "Play";
            colour = Theme::good.darker (0.3f);
            text = "Recording  " + juce::String (seconds, 1) + " s\nPress Play to loop it, or Overdub to loop and keep playing.";
            break;

        case Looper::State::playing:
            action = "Playing";
            colour = Theme::good.darker (0.3f);
            text = "Playing a " + juce::String (seconds, 1) + " s loop, " + layersText (looper.getNumLayers())
                   + ".\nPress Overdub to add a layer.";
            break;

        case Looper::State::overdubbing:
            action = "Play";
            colour = Theme::good.darker (0.3f);
            text = "Overdubbing layer " + juce::String (looper.getNumLayers())
                   + ". Press Overdub or Play to keep it, or Undo to drop it.";
            break;

        case Looper::State::stopped:
            action = "Play";
            colour = Theme::good.darker (0.3f);
            text = "Stopped. The " + juce::String (seconds, 1) + " s loop and its "
                   + layersText (looper.getNumLayers()) + " are kept.";
            break;
    }

    if (loopButton.getButtonText() != action)
        loopButton.setButtonText (action);

    loopButton.setColour (juce::TextButton::buttonColourId, colour);

    if (loopStatus.getText() != text)
        loopStatus.setText (text, juce::dontSendNotification);

    const bool hasLoop = state != Looper::State::empty;
    loopButton.setEnabled (state != Looper::State::playing);
    overdubButton.setEnabled (hasLoop);
    overdubButton.setColour (juce::TextButton::buttonColourId,
                             state == Looper::State::overdubbing ? Theme::warning : Theme::warning.darker (0.75f));
    overdubButton.setColour (juce::TextButton::textColourOffId,
                             state == Looper::State::overdubbing ? Theme::sunken : juce::Colours::white);
    stopButton.setEnabled (hasLoop && state != Looper::State::stopped);
    clearButton.setEnabled (hasLoop);
    undoButton.setEnabled (looper.canUndo());
}

void PracticePanel::refreshRecorder()
{
    auto& recorder = tools.getRecorder();

    if (recorder.isRecording() && recorder.wasInterrupted())
    {
        recorder.stop();
        recordStatus.setText ("The take was stopped because the sample rate changed. "
                              "What was recorded before that is saved.",
                              juce::dontSendNotification);
    }

    const bool recording = recorder.isRecording();
    recordButton.setToggleState (recording, juce::dontSendNotification);
    recordButton.setButtonText (recording ? "Stop recording" : "Record take");

    const auto time = recording ? formatTime (recorder.getSecondsRecorded()) : juce::String ("0:00");

    if (recordTime.getText() != time)
        recordTime.setText (time, juce::dontSendNotification);

    recordTime.setColour (juce::Label::textColourId, recording ? Theme::danger : juce::Colours::white.withAlpha (0.4f));
}

void PracticePanel::toggleRecording()
{
    auto& recorder = tools.getRecorder();

    if (recorder.isRecording())
    {
        const auto take = recorder.stop();
        recordStatus.setText ("Saved \"" + take.processed.getFileName() + "\" and its dry track.",
                              juce::dontSendNotification);
    }
    else
    {
        const auto name = "Take " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
        const auto error = recorder.start (getRecordingsFolder(), name);

        if (error.isNotEmpty())
            dialogs::showMessage ("Could not start recording", error);
        else
            recordStatus.setText ("Recording to \"" + name + ".wav\". Open folder shows where takes are saved.",
                                  juce::dontSendNotification);
    }

    refreshRecorder();
}
