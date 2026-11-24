#pragma once

#include "../engine/Parameter.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

/** The Width knob of the band-pass, notch and peak blocks. Underneath it is
    the filter's Q, but it reads as the width of the affected band in Hz at
    the block's current frequency (about frequency / Q), and turning it right
    makes the band wider. */
class BlockWidthKnob final : public juce::Component,
                             private juce::Timer
{
public:
    BlockWidthKnob (Parameter& qToUse, Parameter& frequencyToUse)
        : q (qToUse), frequency (frequencyToUse)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 84, 20);
        slider.setTooltip ("Width: how wide a band of frequencies around the centre this block works on. "
                           "Turn right for wider. Drag, scroll, or type a width in Hz. Double-click to reset.");
        slider.setRange (0.0, 1.0);
        slider.textFromValueFunction = [this] (double position) { return formatHz (frequency.get() / qFromPosition (position)); };
        slider.valueFromTextFunction = [this] (const juce::String& text)
        {
            auto hz = text.retainCharacters ("0123456789.").getDoubleValue();

            if (text.containsIgnoreCase ("k"))
                hz *= 1000.0;

            return hz > 0.0 ? positionFromQ (frequency.get() / hz) : slider.getValue();
        };
        slider.setDoubleClickReturnValue (true, positionFromQ (q.defaultValue));
        slider.setValue (positionFromQ (q.get()), juce::dontSendNotification);
        slider.onValueChange = [this] { q.set ((float) qFromPosition (slider.getValue())); };
        addAndMakeVisible (slider);

        caption.setText ("Width", juce::dontSendNotification);
        caption.setJustificationType (juce::Justification::centred);
        caption.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        caption.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
        addAndMakeVisible (caption);

        lastFrequency = frequency.get();
        startTimerHz (15);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        caption.setBounds (area.removeFromTop (18));
        slider.setBounds (area);
    }

private:
    /** Position 0 is the narrowest band (highest Q), 1 the widest (lowest Q), spread by ratio. */
    double qFromPosition (double position) const
    {
        const auto narrow = std::log ((double) q.maxValue), wide = std::log ((double) q.minValue);
        return std::exp (narrow + juce::jlimit (0.0, 1.0, position) * (wide - narrow));
    }

    double positionFromQ (double value) const
    {
        const auto narrow = std::log ((double) q.maxValue), wide = std::log ((double) q.minValue);
        return juce::jlimit (0.0, 1.0, (std::log (juce::jmax (1.0e-3, value)) - narrow) / (wide - narrow));
    }

    static juce::String formatHz (double hz)
    {
        if (hz >= 1000.0)
            return juce::String (hz / 1000.0, hz >= 10000.0 ? 0 : 1) + " kHz";

        return juce::String (juce::roundToInt (hz)) + " Hz";
    }

    /** The width in Hz depends on the Frequency knob too, so it follows it. */
    void timerCallback() override
    {
        if (! juce::exactlyEqual (frequency.get(), lastFrequency))
        {
            lastFrequency = frequency.get();
            slider.updateText();
        }
    }

    Parameter& q;
    Parameter& frequency;
    juce::Slider slider;
    juce::Label caption;
    float lastFrequency = 0.0f;
};


//==============================================================================
/** A frequency response curve: level change in dB from 20 Hz to 20 kHz. */
class ResponseGraph final : public juce::Component,
                            public juce::SettableTooltipClient
{
public:
    explicit ResponseGraph (const juce::String& tooltip);
    void setCurve (std::vector<float> newDb, bool show, const juce::String& emptyText = "Add a filter to see its curve");
    void paint (juce::Graphics&) override;

    /** The frequencies setCurve's values are for. */
    static const std::vector<float>& getFrequencies();

private:
    std::vector<float> db;
    bool showing = false;
    juce::String emptyMessage;
};
