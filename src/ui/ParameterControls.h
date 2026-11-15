#pragma once

#include "../engine/Parameter.h"

#include <juce_gui_basics/juce_gui_basics.h>

/** A rotary knob with a caption, bound to one Parameter. */
class ParameterKnob final : public juce::Component
{
public:
    explicit ParameterKnob (Parameter& parameterToUse)
        : parameter (parameterToUse)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
        slider.setRange (parameter.minValue, parameter.maxValue, parameter.step);
        slider.setTextValueSuffix (parameter.unit);
        slider.setDoubleClickReturnValue (true, parameter.defaultValue);

        if (parameter.logarithmic)
            slider.setSkewFactorFromMidPoint (std::sqrt ((double) parameter.minValue * parameter.maxValue));

        slider.setValue (parameter.get(), juce::dontSendNotification);
        slider.onValueChange = [this] { parameter.set ((float) slider.getValue()); };
        addAndMakeVisible (slider);

        caption.setText (parameter.name, juce::dontSendNotification);
        caption.setJustificationType (juce::Justification::centred);
        caption.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        caption.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
        addAndMakeVisible (caption);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        caption.setBounds (area.removeFromTop (16));
        slider.setBounds (area);
    }

private:
    Parameter& parameter;
    juce::Slider slider;
    juce::Label caption;
};

/** An on/off switch bound to one Parameter. */
class ParameterToggle final : public juce::ToggleButton
{
public:
    explicit ParameterToggle (Parameter& parameterToUse)
        : juce::ToggleButton (parameterToUse.name), parameter (parameterToUse)
    {
        refresh();
        onClick = [this] { parameter.set (getToggleState() ? 1.0f : 0.0f); };
    }

    /** Call when the parameter may have been changed from somewhere else. */
    void refresh()   { setToggleState (parameter.isOn(), juce::dontSendNotification); }

private:
    Parameter& parameter;
};
