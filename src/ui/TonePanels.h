#pragma once

#include "../engine/AmpCapture.h"
#include "../engine/AmpModel.h"
#include "../engine/Cabinet.h"
#include "../engine/Practice.h"
#include "../engine/effects/Effect.h"
#include "../engine/effects/TempoSync.h"
#include "ParameterControls.h"
#include "PedalLook.h"

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

/** The tempo shared by the metronome and the delays' Sync setting. */
struct SharedTempo
{
    std::function<double()> get;
    std::function<void (double bpm)> set;
};

/** Any pedal, drawn as a stompbox: its knobs across the top, its name, and a
    footswitch that switches it on and off. With `onRemove` set, a Remove
    button calls it.

    Delays also get a Tap button. With Sync off, tapping sets the Time knob to
    the gap between taps. With Sync on, tapping sets the shared tempo (the
    metronome's), and the delay follows it. */
class EffectPanel final : public TonePanel,
                          private juce::Timer
{
public:
    EffectPanel (Effect&, juce::Colour face, const juce::String& category,
                 std::function<void()> onRemove = {}, SharedTempo tempo = {});
    ~EffectPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshPower() override   { repaint(); }

    /** The pedal's outline, sized for its controls and centred in the panel. */
    juce::Rectangle<int> getEnclosure() const;

    /** Where the input (left) and output (right) cables plug in, in this panel. */
    juce::Point<float> getJack (bool input) const;

    /** The narrowest panel the pedal still looks right in. */
    int getComfortableWidth() const;

private:
    class Footswitch;

    void timerCallback() override;
    void tapped();
    void refreshTempoInfo();

    int getEnclosureWidth() const;
    int getColumns() const;
    int getKnobRows() const;
    bool hasExtras() const;

    Effect& effect;
    juce::Colour face;
    juce::String category;
    SharedTempo sharedTempo;
    pedal_look::StompboxKnobs knobLook;
    std::unique_ptr<Footswitch> footswitch;
    juce::TextButton removeButton { "Remove" };
    juce::OwnedArray<ParameterKnob> knobs;
    juce::OwnedArray<ParameterToggle> switches;
    juce::OwnedArray<ParameterChoice> choices;
    bool lastOn = false;

    Parameter* tapTime = nullptr;
    Parameter* tempoSync = nullptr;
    ParameterKnob* timeKnob = nullptr;
    juce::TextButton tapButton { "Tap" };
    juce::Label tempoInfo;
    std::unique_ptr<TapTempo> tapTempo;
    double keepTappingUntil = 0.0;
};
