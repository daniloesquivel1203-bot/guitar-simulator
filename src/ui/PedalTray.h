#pragma once

#include "../engine/Pedalboard.h"
#include "PedalLook.h"

#include <juce_gui_basics/juce_gui_basics.h>

/** A drawer of mini pedals to add, one category at a time. Click a mini pedal
    to add it after the selected pedal, or drag it onto the chain stack.

    Drags carry "pedal:<key>" for a pedal type, or "mine:<name>" for a saved
    Custom pedal. Needs a juce::DragAndDropContainer above it. */
class PedalTray final : public juce::Component
{
public:
    /** Called with "pedal:<key>" or "mine:<name>" when a mini pedal is clicked. */
    std::function<void (const juce::String& description)> onPick;

    /** The names of the player's saved Custom pedals, asked for when that tab opens. */
    std::function<juce::StringArray()> getMyPedals;

    PedalTray();
    ~PedalTray() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Shows a category, for example the one of the selected pedal. */
    void showCategory (const juce::String& category);

private:
    class MiniPedal;

    void rebuildTiles();

    juce::StringArray categories;
    juce::OwnedArray<juce::TextButton> tabs;
    juce::String current;

    juce::Viewport viewport;
    juce::Component tileHolder;
    juce::OwnedArray<MiniPedal> tiles;
    juce::Label hint;
};
