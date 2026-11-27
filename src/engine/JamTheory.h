#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <vector>

/** The music theory behind the jam tracks: keys, chords that fit them,
    common chord progressions and the scales to solo with.

    Everything is worked out relative to the key, so a progression keeps its
    shape when the key changes. Nothing here touches audio. */
namespace jam
{
enum class Mode { major, minor };

enum class Quality
{
    major, minor, diminished, dominant7, major7, minor7, halfDiminished7, sus4, power
};

/** A chord as a distance from the key's root, so it moves with the key. */
struct Chord
{
    int degree = 0;                     // semitones above the key's root, 0 to 11
    Quality quality = Quality::major;

    bool operator== (const Chord& other) const noexcept   { return degree == other.degree && quality == other.quality; }
};

/** One chord in a progression, held for a number of beats (2, 4 or 8). */
struct Slot
{
    Chord chord;
    int beats = 4;

    bool operator== (const Slot& other) const noexcept   { return chord == other.chord && beats == other.beats; }
};

using Progression = std::vector<Slot>;

struct Key
{
    int root = 9;   // pitch class, 0 = C. A by default.
    Mode mode = Mode::minor;
};

/** Note names, spelt with sharps or flats to suit the key. */
juce::String noteName (int pitchClass, bool useFlats);
bool keyUsesFlats (Key);
juce::String keyName (Key);

/** "m", "7", "maj7" and so on. */
juce::String qualitySuffix (Quality);
juce::String qualityName (Quality);

/** The chord's name in the key, such as "F#m7". */
juce::String chordName (Chord, Key);

/** The chord's Roman numeral, such as "ii7" or "bVII". */
juce::String romanNumeral (Chord, Mode);

/** The chord's notes as intervals above its root, lowest first. */
std::vector<int> chordIntervals (Quality);

/** The pitch classes of the chord in the key. */
std::vector<int> chordPitchClasses (Chord, Key);

/** The seven chords built on the key's scale, as triads and as seventh chords,
    plus a few borrowed chords that are common in songs in this mode. */
struct ChordChoice
{
    Chord chord;
    juce::String group;   // "In the key", "Seventh chords" or "Borrowed"
};

std::vector<ChordChoice> chordsForKey (Mode);

/** True if every note of the chord is in the key's scale. */
bool chordFitsKey (Chord, Mode);

/** Common progressions, each written for one mode. */
struct ProgressionPreset
{
    juce::String name;
    Mode mode;
    Progression slots;
};

const std::vector<ProgressionPreset>& getProgressionPresets();

/** Scales a guitarist can solo with over the key, with their notes. */
struct ScaleSuggestion
{
    juce::String name;
    juce::String notes;
};

std::vector<ScaleSuggestion> scalesForKey (Key, bool includeBlues);

/** Writes a progression as text for the settings file, and reads it back.
    Returns an empty progression if the text cannot be read. */
juce::String progressionToString (const Progression&);
Progression progressionFromString (const juce::String&);

int totalBeats (const Progression&);

constexpr int maxProgressionBeats = 32 * 4;
}
