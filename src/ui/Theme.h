#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** The app's colours and the flat look of its knobs, sliders, buttons and lists. */
class Theme : public juce::LookAndFeel_V4
{
public:
    static inline const juce::Colour background { 0xff22282d };
    static inline const juce::Colour panel { 0xff2b3238 };
    static inline const juce::Colour raised { 0xff36404a };
    static inline const juce::Colour sunken { 0xff1b2126 };
    static inline const juce::Colour accent { 0xff42a2c8 };
    static inline const juce::Colour good { 0xff3ecf8e };
    static inline const juce::Colour warning { 0xfff5a524 };
    static inline const juce::Colour danger { 0xffe5484d };

    Theme();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float position,
                           float startAngle, float endAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float position,
                           float minPosition, float maxPosition, juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool down, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;

    juce::Label* createSliderTextBox (juce::Slider&) override;
};
