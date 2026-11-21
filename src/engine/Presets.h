#pragma once

#include "SignalChain.h"

#include <juce_core/juce_core.h>

/** Saving and loading complete sounds.

    A preset holds which pedals are on the board and in what order, the value
    of every knob and switch, the chosen cabinet and the amp capture file. It
    does not hold the input trim or output level, which belong to your guitar
    and headphones and should not jump when you change sounds.

    Factory presets are built into the app and cannot be changed. User presets
    are JSON files in a folder, one file per preset, so they are easy to back
    up or share.
*/
class PresetManager
{
public:
    struct Entry
    {
        juce::String name;
        bool isFactory = false;

        bool operator== (const Entry& other) const   { return name == other.name && isFactory == other.isFactory; }
    };

    PresetManager (SignalChain& chainToUse, juce::File userFolderToUse);

    //==============================================================================
    /** Captures the chain's current state as a JSON-ready value. */
    static juce::var capture (SignalChain& chain, const juce::String& name);

    /** Applies a captured state. Parameters missing from it go to their defaults
        and unknown ones are ignored, so old and new preset files both load. A
        version 1 preset, which has no board, gets the fixed V1 pedal order.
        Returns false if the value is not a preset at all. */
    static bool apply (SignalChain& chain, const juce::var& preset);

    /** Turns a name typed by the user into one that is safe as a file name.
        "Folder/Name" keeps one folder level, so presets can be grouped. */
    static juce::String cleanName (const juce::String& name);

    //==============================================================================
    juce::StringArray getFactoryNames() const;
    /** User presets, with those in a folder named "Folder/Name". */
    juce::StringArray getUserNames() const;
    juce::File getUserFolder() const   { return userFolder; }

    /** Each returns an error message, or an empty string on success. */
    juce::String load (const Entry& entry);
    juce::String saveUser (const juce::String& name);
    juce::String renameUser (const juce::String& oldName, const juce::String& newName);
    juce::String deleteUser (const juce::String& name);

    bool userPresetExists (const juce::String& name) const   { return getFile (name).existsAsFile(); }

    /** The preset most recently loaded or saved. The name is empty if there is none. */
    Entry getCurrent() const   { return current; }
    void setCurrent (const Entry& entry);

    /** True when a knob has been moved since the current preset was loaded or saved. */
    bool isModified() const;

private:
    juce::File getFile (const juce::String& name) const;
    void removeFolderIfEmpty (const juce::File& folder) const;

public:
    /** The current state as text, for comparing states and for undo. */
    juce::String snapshot() const;

private:

    SignalChain& chain;
    juce::File userFolder;
    Entry current;
    juce::String loadedSnapshot;
};
