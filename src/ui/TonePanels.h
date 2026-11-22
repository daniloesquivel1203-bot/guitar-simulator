#pragma once

#include "../engine/AmpCapture.h"
#include "../engine/AmpModel.h"
#include "../engine/Cabinet.h"
#include "../engine/Practice.h"
#include "../engine/effects/Effect.h"
#include "../engine/effects/TempoSync.h"
#include "ParameterControls.h"

/** A titled, rounded box that the amp and cabinet controls sit in. */
class TonePanel : public juce::Component
{
public:
    /** `powerToUse` is the block's on/off switch, if it has one. */
    explicit TonePanel (const juce::String& titleToUse, Parameter* powerToUse = nullptr)
        : title (titleToUse), powerSwitch (powerToUse) {}

    void paint (juce::Graphics&) override;

    /** Updates the on/off switch after it was changed from the chain strip. */
    virtual void refreshPower() = 0;

    /** Fades the knobs while the block is switched off. Call regularly. */
    void refreshDimming();

protected:
    /** The area below the title row. */
    juce::Rectangle<int> getContentArea() const   { return getLocalBounds().reduced (14).withTrimmedTop (34); }
    juce::Rectangle<int> getHeaderArea() const    { return getLocalBounds().reduced (14).removeFromTop (26); }

    /** Lays knobs out in a centred row, at their preferred size where there is room. */
    static void layoutKnobRow (juce::Rectangle<int> area, const std::vector<juce::Component*>& knobs,
                               juce::Justification alignment = juce::Justification::centred);

private:
    juce::String title;
    Parameter* powerSwitch = nullptr;
    bool dimmed = false;
};

/** The amp block. The drop-down picks one of the built-in voicings or a
    loaded capture (.nam file), and the knobs below change to match. */
class AmpPanel final : public TonePanel
{
public:
    AmpPanel (AmpModel&, AmpCapture&, Cabinet&);
    void resized() override;
    void refreshPower() override   { power.refresh(); }

private:
    enum ItemIds { firstVoicingId = 1, captureId = 100, loadCaptureId = 200, removeCaptureId = 300 };

    void refreshChoices();
    void choiceChanged();
    void chooseCaptureFile();
    void showCaptureDetails();

    AmpModel& amp;
    AmpCapture& capture;
    Cabinet& cabinet;
    ParameterToggle power;
    juce::ComboBox choice;
    ParameterKnob gain, bass, mid, treble, presence, master;
    ParameterKnob captureInput, captureBass, captureMid, captureTreble, captureOutput;
    juce::Label message;
    std::unique_ptr<juce::FileChooser> fileChooser;
};

class CabinetPanel final : public TonePanel
{
public:
    explicit CabinetPanel (Cabinet&);
    void resized() override;
    void refreshPower() override   { power.refresh(); }

private:
    enum ItemIds { firstBuiltInId = 1, loadedFileId = 100, loadFileId = 200 };

    void refreshChoices();
    void choiceChanged();
    void chooseFile();

    Cabinet& cabinet;
    ParameterToggle power;
    juce::ComboBox choice;
    ParameterKnob lowCut, highCut;
    juce::Label message;
    std::unique_ptr<juce::FileChooser> fileChooser;
};

/** The tempo shared by the metronome and the delays' Sync setting. */
struct SharedTempo
{
    std::function<double()> get;
    std::function<void (double bpm)> set;
};

/** Controls for any pedal, built from the pedal's own list of parameters.
    With `onRemove` set, a Remove button calls it.

    Delays also get a Tap button. With Sync off, tapping sets the Time knob to
    the gap between taps. With Sync on, tapping sets the shared tempo (the
    metronome's), and the delay follows it. */
class EffectPanel final : public TonePanel,
                          private juce::Timer
{
public:
    explicit EffectPanel (Effect&, std::function<void()> onRemove = {}, SharedTempo tempo = {});
    void resized() override;
    void refreshPower() override   { power.refresh(); }

private:
    void timerCallback() override;
    void tapped();
    void refreshTempoInfo();

    Effect& effect;
    SharedTempo sharedTempo;
    ParameterToggle power;
    juce::TextButton removeButton { "Remove" };
    juce::OwnedArray<ParameterKnob> knobs;
    juce::OwnedArray<ParameterToggle> switches;
    juce::OwnedArray<ParameterChoice> choices;

    Parameter* tapTime = nullptr;
    Parameter* tempoSync = nullptr;
    ParameterKnob* timeKnob = nullptr;
    juce::TextButton tapButton { "Tap" };
    juce::Label tempoInfo;
    std::unique_ptr<TapTempo> tapTempo;
    double keepTappingUntil = 0.0;
};
