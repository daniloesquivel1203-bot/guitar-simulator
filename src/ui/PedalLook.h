#pragma once

#include "../engine/Pedalboard.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

/** The hardware look: pedal colours, enclosures, Lego-style bricks, the amp's
    tolex and grill cloth, and knob styles for pedals and the amp. Shared by the
    chain stack, the stompbox view, the pedal tray and the amp. */
namespace pedal_look
{
/** The colour of a pedal category, such as olive for Drive or teal for Modulation. */
juce::Colour categoryColour (const juce::String& category);

/** A pedal's own enclosure colour: its category's colour, nudged a little per
    pedal so pedals of one category are told apart but still belong together. */
juce::Colour pedalColour (const PedalType&);

/** Dark or light text, whichever reads best on `face`. */
juce::Colour inkFor (juce::Colour face);

/** A stompbox enclosure: rounded metal box with a highlight and shadow. */
void paintEnclosure (juce::Graphics&, juce::Rectangle<float> area, juce::Colour face, float cornerSize);

/** A Lego-style brick with studs along its top edge. */
void paintBrick (juce::Graphics&, juce::Rectangle<float> area, juce::Colour face, bool withStuds = true);

/** A status light. Red for pedals, as on real stompboxes. */
void paintLed (juce::Graphics&, juce::Point<float> centre, float diameter, bool on, juce::Colour lit = juce::Colour (0xffff3b30));

/** A round metal footswitch. */
void paintFootswitch (juce::Graphics&, juce::Rectangle<float> area, bool pressed);

/** The textured black vinyl that covers amps and cabinets. */
void paintTolex (juce::Graphics&, juce::Rectangle<float> area, float cornerSize);

/** Grill cloth with the outlines of `numSpeakers` speakers behind it. */
void paintGrill (juce::Graphics&, juce::Rectangle<float> area, int numSpeakers);

/** Black knobs with a white pointer, as on most pedals. Captions and values use `ink`. */
class StompboxKnobs final : public Theme
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float position,
                           float startAngle, float endAngle, juce::Slider&) override;
};

/** Knobs for the amp head: dark with a gold ring of the current value, gold
    caps for Gain and Master. */
class AmpKnobs final : public Theme
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float position,
                           float startAngle, float endAngle, juce::Slider&) override;

    static inline const juce::Colour gold { 0xffe7d6a8 };
};
}
