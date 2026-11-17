#pragma once

#include "../engine/AmpModel.h"
#include "../engine/Cabinet.h"
#include "../engine/effects/Effect.h"
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

class AmpPanel final : public TonePanel
{
public:
    explicit AmpPanel (AmpModel&);
    void resized() override;
    void refreshPower() override   { power.refresh(); }

private:
    AmpModel& amp;
    ParameterToggle power;
    juce::ComboBox voicing;
    ParameterKnob gain, bass, mid, treble, presence, master;
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

/** Controls for any pedal, built from the pedal's own list of parameters. */
class EffectPanel final : public TonePanel
{
public:
    explicit EffectPanel (Effect&);
    void resized() override;
    void refreshPower() override   { power.refresh(); }

private:
    ParameterToggle power;
    juce::OwnedArray<ParameterKnob> knobs;
    juce::OwnedArray<ParameterToggle> switches;
};
