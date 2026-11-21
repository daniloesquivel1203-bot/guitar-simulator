#pragma once

#include "../engine/Parameter.h"

#include <juce_gui_basics/juce_gui_basics.h>

/** A rotary knob with a caption, bound to one Parameter. */
class ParameterKnob final : public juce::Component
{
public:
    /** `captionToUse` replaces the parameter's own name above the knob. */
    explicit ParameterKnob (Parameter& parameterToUse, const juce::String& captionToUse = {})
        : parameter (parameterToUse)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 84, 20);
        const auto label = captionToUse.isNotEmpty() ? captionToUse : juce::String (parameter.name);
        slider.setTooltip (label + ". Drag up or down, scroll, or type a value. Double-click to reset.");
        slider.setRange (parameter.minValue, parameter.maxValue, parameter.step);
        slider.setTextValueSuffix (parameter.unit);
        slider.setDoubleClickReturnValue (true, parameter.defaultValue);

        if (parameter.logarithmic)
            slider.setSkewFactorFromMidPoint (std::sqrt ((double) parameter.minValue * parameter.maxValue));

        slider.setValue (parameter.get(), juce::dontSendNotification);
        slider.onValueChange = [this] { parameter.set ((float) slider.getValue()); };
        addAndMakeVisible (slider);

        caption.setText (label, juce::dontSendNotification);
        caption.setJustificationType (juce::Justification::centred);
        caption.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        caption.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
        addAndMakeVisible (caption);
    }

    /** The size a knob looks best at. Panels centre knobs of this size in their space. */
    static constexpr int preferredWidth = 96;
    static constexpr int preferredHeight = 134;

    void resized() override
    {
        auto area = getLocalBounds();
        caption.setBounds (area.removeFromTop (18));
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

/** A drop-down list bound to one choice Parameter, with a caption above it. */
class ParameterChoice final : public juce::Component
{
public:
    explicit ParameterChoice (Parameter& parameterToUse, const juce::String& captionToUse = {})
        : parameter (parameterToUse)
    {
        const auto& names = parameter.getChoices();

        for (int i = 0; i < (int) names.size(); ++i)
            box.addItem (names[(size_t) i], i + 1);

        box.setSelectedId (parameter.getIndex() + 1, juce::dontSendNotification);
        box.onChange = [this] { parameter.set ((float) (box.getSelectedId() - 1)); };
        addAndMakeVisible (box);

        caption.setText (captionToUse.isNotEmpty() ? captionToUse : juce::String (parameter.name), juce::dontSendNotification);
        caption.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        caption.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
        addAndMakeVisible (caption);
    }

    static constexpr int preferredHeight = 46;

    void resized() override
    {
        auto area = getLocalBounds();
        caption.setBounds (area.removeFromTop (18));
        box.setBounds (area.removeFromTop (26));
    }

private:
    Parameter& parameter;
    juce::ComboBox box;
    juce::Label caption;
};
