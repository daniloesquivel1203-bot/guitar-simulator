#pragma once

#include "../engine/AmpCapture.h"
#include "../engine/AmpModel.h"
#include "../engine/Cabinet.h"
#include "ParameterControls.h"
#include "PedalLook.h"

/** The amp and cabinet as one unit: an amp head sitting on a speaker cabinet.

    The head's display picks a voicing or a loaded capture (.nam file), and
    its knobs change to match. The cabinet's display picks a built-in cabinet
    or an impulse response file. With Match voicing on, picking a voicing also
    picks the cabinet that suits it; choosing a cabinet yourself turns it off. */
class AmpRig final : public juce::Component
{
public:
    AmpRig (AmpModel&, AmpCapture&, Cabinet&, juce::PropertiesFile& settings);
    ~AmpRig() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    /** Updates every control, after a preset, undo or A/B switch changed them. */
    void refreshAll();

    /** Fades knobs of a block that is off and redraws the lights. Call regularly. */
    void refreshDimming();

    /** The built-in cabinet that suits a voicing, used by Match voicing. */
    static int getMatchingCabinet (int voicing);

private:
    enum AmpIds { firstVoicingId = 1, captureId = 100, loadCaptureId = 200, removeCaptureId = 300 };
    enum CabIds { firstBuiltInId = 1, loadedFileId = 100, loadFileId = 200 };

    void refreshAmpChoices();
    void ampChoiceChanged();
    void chooseCaptureFile();
    juce::String describeCapture() const;

    void refreshCabChoices();
    void cabChoiceChanged();
    void chooseCabFile();
    void applyMatch();
    int countSpeakers() const;

    juce::Rectangle<int> getHeadArea() const;
    juce::Rectangle<int> getCabArea() const;
    juce::Rectangle<int> getAmpLightArea() const;
    juce::Rectangle<int> getCabLightArea() const;

    AmpModel& amp;
    AmpCapture& capture;
    Cabinet& cabinet;
    juce::PropertiesFile& settings;

    pedal_look::AmpKnobs ampKnobsLook;

    juce::ComboBox ampChoice;
    ParameterKnob gain, bass, mid, treble, presence, master;
    ParameterKnob captureInput, captureBass, captureMid, captureTreble, captureOutput;
    juce::Label captureNote;
    std::unique_ptr<juce::FileChooser> captureChooser;

    juce::ComboBox cabChoice;
    juce::ToggleButton matchButton { "Match voicing" };
    ParameterKnob lowCut, highCut;
    juce::Label cabNote;
    std::unique_ptr<juce::FileChooser> cabChooser;

    bool ampDimmed = false, cabDimmed = false;
    bool lastAmpOn = true, lastCabOn = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpRig)
};
