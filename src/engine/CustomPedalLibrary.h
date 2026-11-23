#pragma once

#include "effects/CustomPedal.h"

#include <juce_core/juce_core.h>

/** Custom pedals saved under the player's own names, so they can be added to
    the board again from the add menu. Each is a small JSON file holding the
    pedal's knob settings, in a "Custom Pedals" folder next to the presets. */
class CustomPedalLibrary
{
public:
    explicit CustomPedalLibrary (juce::File folderToUse) : folder (std::move (folderToUse)) {}

    juce::File getFolder() const   { return folder; }

    /** Saved names, sorted. */
    juce::StringArray getNames() const;

    /** Saves the pedal's settings under `name` (replacing one of that name)
        and gives the pedal that name. Returns an error message, or empty. */
    juce::String save (const juce::String& name, CustomPedal&);

    /** Sets up `pedal` from a saved one, including its name. Returns false if
        there is no such pedal or the file is damaged. */
    bool load (const juce::String& name, CustomPedal& pedal) const;

    /** Returns an error message, or empty. */
    juce::String remove (const juce::String& name);

    /** The settings of a pedal as JSON, and back. Used by the library and presets. */
    static juce::var captureSettings (CustomPedal&);
    static void applySettings (CustomPedal&, const juce::var& settings);

private:
    juce::File getFile (const juce::String& name) const;

    juce::File folder;
};
