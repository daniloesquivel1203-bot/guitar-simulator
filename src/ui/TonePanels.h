#pragma once

#include "../engine/AmpModel.h"
#include "../engine/Cabinet.h"
#include "ParameterControls.h"

/** A titled, rounded box that the amp and cabinet controls sit in. */
class TonePanel : public juce::Component
{
public:
    explicit TonePanel (const juce::String& titleToUse) : title (titleToUse) {}

    void paint (juce::Graphics&) override;

protected:
    /** The area below the title row. */
    juce::Rectangle<int> getContentArea() const   { return getLocalBounds().reduced (14).withTrimmedTop (34); }
    juce::Rectangle<int> getHeaderArea() const    { return getLocalBounds().reduced (14).removeFromTop (26); }

private:
    juce::String title;
};

class AmpPanel final : public TonePanel
{
public:
    explicit AmpPanel (AmpModel&);
    void resized() override;

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
