#include "JamTheory.h"

namespace jam
{
namespace
{
constexpr std::array<int, 7> majorScale { 0, 2, 4, 5, 7, 9, 11 };
constexpr std::array<int, 7> minorScale { 0, 2, 3, 5, 7, 8, 10 };

const std::array<const char*, 12> sharpNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const std::array<const char*, 12> flatNames  { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };

struct QualityInfo
{
    Quality quality;
    const char* code;     // in the settings file
    const char* suffix;   // in chord names
    const char* name;     // in menus
};

const std::array<QualityInfo, 9> qualities { {
    { Quality::major,           "maj",  "",     "major" },
    { Quality::minor,           "min",  "m",    "minor" },
    { Quality::diminished,      "dim",  "dim",  "diminished" },
    { Quality::dominant7,       "7",    "7",    "seventh" },
    { Quality::major7,          "maj7", "maj7", "major seventh" },
    { Quality::minor7,          "m7",   "m7",   "minor seventh" },
    { Quality::halfDiminished7, "m7b5", "m7b5", "half-diminished" },
    { Quality::sus4,            "sus4", "sus4", "suspended fourth" },
    { Quality::power,           "5",    "5",    "power chord" },
} };

const QualityInfo& infoFor (Quality quality)
{
    for (const auto& info : qualities)
        if (info.quality == quality)
            return info;

    return qualities[0];
}

int wrap (int pitch)
{
    return ((pitch % 12) + 12) % 12;
}

const std::array<int, 7>& scaleFor (Mode mode)
{
    return mode == Mode::major ? majorScale : minorScale;
}

bool isMinorish (Quality quality)
{
    return quality == Quality::minor || quality == Quality::minor7
           || quality == Quality::diminished || quality == Quality::halfDiminished7;
}

/** Chords on these degrees are named with flats, such as bVII in a sharp key. */
bool isFlatDegree (int degree, Mode mode)
{
    if (mode == Mode::major)
        return degree == 1 || degree == 3 || degree == 6 || degree == 8 || degree == 10;

    return degree == 1 || degree == 6;
}

Slot slot (int degree, Quality quality, int beats = 4)
{
    return { { degree, quality }, beats };
}

juce::String joinNotes (int root, std::initializer_list<int> intervals, bool flats)
{
    juce::StringArray names;

    for (auto interval : intervals)
        names.add (noteName (root + interval, flats));

    return names.joinIntoString (" ");
}
}

//==============================================================================
juce::String noteName (int pitchClass, bool useFlats)
{
    return (useFlats ? flatNames : sharpNames)[(size_t) wrap (pitchClass)];
}

bool keyUsesFlats (Key key)
{
    const auto major = key.mode == Mode::major ? key.root : wrap (key.root + 3);
    return major == 1 || major == 3 || major == 5 || major == 8 || major == 10;
}

juce::String keyName (Key key)
{
    return noteName (key.root, keyUsesFlats (key)) + (key.mode == Mode::major ? " major" : " minor");
}

juce::String qualitySuffix (Quality quality)   { return infoFor (quality).suffix; }
juce::String qualityName (Quality quality)     { return infoFor (quality).name; }

juce::String chordName (Chord chord, Key key)
{
    const auto flats = keyUsesFlats (key) || isFlatDegree (chord.degree, key.mode);
    return noteName (key.root + chord.degree, flats) + qualitySuffix (chord.quality);
}

juce::String romanNumeral (Chord chord, Mode mode)
{
    static const std::array<const char*, 12> forMajor { "I", "bII", "II", "bIII", "III", "IV", "bV", "V", "bVI", "VI", "bVII", "VII" };
    static const std::array<const char*, 12> forMinor { "I", "bII", "II", "III", "#III", "IV", "bV", "V", "VI", "#VI", "VII", "#VII" };

    juce::String numeral = (mode == Mode::major ? forMajor : forMinor)[(size_t) wrap (chord.degree)];

    if (isMinorish (chord.quality))
        numeral = numeral.toLowerCase().replace ("b", "b").replace ("#", "#");

    switch (chord.quality)
    {
        case Quality::diminished:       return numeral + juce::String::fromUTF8 ("\xc2\xb0");
        case Quality::halfDiminished7:  return numeral + juce::String::fromUTF8 ("\xc3\xb8" "7");
        case Quality::dominant7:
        case Quality::minor7:           return numeral + "7";
        case Quality::major7:           return numeral + "maj7";
        case Quality::sus4:             return numeral + "sus4";
        case Quality::power:            return numeral + "5";
        case Quality::major:
        case Quality::minor:            break;
    }

    return numeral;
}

std::vector<int> chordIntervals (Quality quality)
{
    switch (quality)
    {
        case Quality::major:            return { 0, 4, 7 };
        case Quality::minor:            return { 0, 3, 7 };
        case Quality::diminished:       return { 0, 3, 6 };
        case Quality::dominant7:        return { 0, 4, 7, 10 };
        case Quality::major7:           return { 0, 4, 7, 11 };
        case Quality::minor7:           return { 0, 3, 7, 10 };
        case Quality::halfDiminished7:  return { 0, 3, 6, 10 };
        case Quality::sus4:             return { 0, 5, 7 };
        case Quality::power:            return { 0, 7 };
    }

    return { 0, 4, 7 };
}

std::vector<int> chordPitchClasses (Chord chord, Key key)
{
    std::vector<int> notes;

    for (auto interval : chordIntervals (chord.quality))
        notes.push_back (wrap (key.root + chord.degree + interval));

    return notes;
}

std::vector<ChordChoice> chordsForKey (Mode mode)
{
    std::vector<ChordChoice> choices;

    const std::array<Quality, 7> majorTriads { Quality::major, Quality::minor, Quality::minor, Quality::major,
                                               Quality::major, Quality::minor, Quality::diminished };
    const std::array<Quality, 7> majorSevenths { Quality::major7, Quality::minor7, Quality::minor7, Quality::major7,
                                                 Quality::dominant7, Quality::minor7, Quality::halfDiminished7 };
    const std::array<Quality, 7> minorTriads { Quality::minor, Quality::diminished, Quality::major, Quality::minor,
                                               Quality::minor, Quality::major, Quality::major };
    const std::array<Quality, 7> minorSevenths { Quality::minor7, Quality::halfDiminished7, Quality::major7, Quality::minor7,
                                                 Quality::minor7, Quality::major7, Quality::dominant7 };

    const auto& scale = scaleFor (mode);
    const auto& triads = mode == Mode::major ? majorTriads : minorTriads;
    const auto& sevenths = mode == Mode::major ? majorSevenths : minorSevenths;

    for (size_t i = 0; i < 7; ++i)
        choices.push_back ({ { scale[i], triads[i] }, "In the key" });

    for (size_t i = 0; i < 7; ++i)
        choices.push_back ({ { scale[i], sevenths[i] }, "Seventh chords" });

    // Chords from outside the scale that songs in this mode use all the time.
    const std::vector<Chord> borrowed = mode == Mode::major
        ? std::vector<Chord> { { 10, Quality::major }, { 5, Quality::minor }, { 8, Quality::major }, { 3, Quality::major },
                               { 0, Quality::dominant7 }, { 5, Quality::dominant7 }, { 2, Quality::major }, { 4, Quality::dominant7 } }
        : std::vector<Chord> { { 7, Quality::major }, { 7, Quality::dominant7 }, { 5, Quality::major }, { 5, Quality::dominant7 },
                               { 1, Quality::major }, { 0, Quality::major } };

    for (const auto& chord : borrowed)
        choices.push_back ({ chord, "Borrowed" });

    return choices;
}

bool chordFitsKey (Chord chord, Mode mode)
{
    const auto& scale = scaleFor (mode);

    for (auto interval : chordIntervals (chord.quality))
        if (std::find (scale.begin(), scale.end(), wrap (chord.degree + interval)) == scale.end())
            return false;

    return true;
}

const std::vector<ProgressionPreset>& getProgressionPresets()
{
    using Q = Quality;

    static const std::vector<ProgressionPreset> presets {
        { "I - V - vi - IV", Mode::major,
          { slot (0, Q::major), slot (7, Q::major), slot (9, Q::minor), slot (5, Q::major) } },
        { "I - IV - V - IV", Mode::major,
          { slot (0, Q::major), slot (5, Q::major), slot (7, Q::major), slot (5, Q::major) } },
        { "I - vi - IV - V", Mode::major,
          { slot (0, Q::major), slot (9, Q::minor), slot (5, Q::major), slot (7, Q::major) } },
        { "I - bVII - IV - I", Mode::major,
          { slot (0, Q::major), slot (10, Q::major), slot (5, Q::major), slot (0, Q::major) } },
        { "12-bar blues", Mode::major,
          { slot (0, Q::dominant7), slot (5, Q::dominant7), slot (0, Q::dominant7, 8),
            slot (5, Q::dominant7, 8), slot (0, Q::dominant7, 8),
            slot (7, Q::dominant7), slot (5, Q::dominant7), slot (0, Q::dominant7), slot (7, Q::dominant7) } },
        { "ii - V - I", Mode::major,
          { slot (2, Q::minor7), slot (7, Q::dominant7), slot (0, Q::major7, 8) } },
        { "I - vi - ii - V (turnaround)", Mode::major,
          { slot (0, Q::major7), slot (9, Q::minor7), slot (2, Q::minor7), slot (7, Q::dominant7) } },
        { "I - V - vi - iii - IV - I - IV - V", Mode::major,
          { slot (0, Q::major), slot (7, Q::major), slot (9, Q::minor), slot (4, Q::minor),
            slot (5, Q::major), slot (0, Q::major), slot (5, Q::major), slot (7, Q::major) } },
        { "I7 vamp", Mode::major,
          { slot (0, Q::dominant7, 8) } },
        { "I - IV vamp", Mode::major,
          { slot (0, Q::major), slot (5, Q::major) } },

        { "i - VII - VI - VII", Mode::minor,
          { slot (0, Q::minor), slot (10, Q::major), slot (8, Q::major), slot (10, Q::major) } },
        { "i - VI - III - VII", Mode::minor,
          { slot (0, Q::minor), slot (8, Q::major), slot (3, Q::major), slot (10, Q::major) } },
        { "i - iv - V - i", Mode::minor,
          { slot (0, Q::minor), slot (5, Q::minor), slot (7, Q::major), slot (0, Q::minor) } },
        { "i - VII - VI - V", Mode::minor,
          { slot (0, Q::minor), slot (10, Q::major), slot (8, Q::major), slot (7, Q::major) } },
        { "Minor 12-bar blues", Mode::minor,
          { slot (0, Q::minor7, 16), slot (5, Q::minor7, 8), slot (0, Q::minor7, 8),
            slot (8, Q::dominant7), slot (7, Q::dominant7), slot (0, Q::minor7), slot (7, Q::dominant7) } },
        { "ii - V - i", Mode::minor,
          { slot (2, Q::halfDiminished7), slot (7, Q::dominant7), slot (0, Q::minor7, 8) } },
        { "i - IV vamp", Mode::minor,
          { slot (0, Q::minor7), slot (5, Q::dominant7) } },
        { "i - iv vamp", Mode::minor,
          { slot (0, Q::minor), slot (5, Q::minor) } },
        { "i vamp", Mode::minor,
          { slot (0, Q::minor7, 8) } },
    };

    return presets;
}

std::vector<ScaleSuggestion> scalesForKey (Key key, bool includeBlues)
{
    const auto flats = keyUsesFlats (key);
    const auto tonic = noteName (key.root, flats);
    std::vector<ScaleSuggestion> scales;

    if (key.mode == Mode::major)
    {
        scales.push_back ({ tonic + " major pentatonic", joinNotes (key.root, { 0, 2, 4, 7, 9 }, flats) });
        scales.push_back ({ tonic + " major", joinNotes (key.root, { 0, 2, 4, 5, 7, 9, 11 }, flats) });

        if (includeBlues)
        {
            scales.push_back ({ tonic + " major blues", joinNotes (key.root, { 0, 2, 3, 4, 7, 9 }, flats) });
            scales.push_back ({ tonic + " minor pentatonic", joinNotes (key.root, { 0, 3, 5, 7, 10 }, true) });
        }
    }
    else
    {
        scales.push_back ({ tonic + " minor pentatonic", joinNotes (key.root, { 0, 3, 5, 7, 10 }, flats) });
        scales.push_back ({ tonic + " natural minor", joinNotes (key.root, { 0, 2, 3, 5, 7, 8, 10 }, flats) });

        if (includeBlues)
            scales.push_back ({ tonic + " blues", joinNotes (key.root, { 0, 3, 5, 6, 7, 10 }, flats) });
        else
            scales.push_back ({ tonic + " Dorian", joinNotes (key.root, { 0, 2, 3, 5, 7, 9, 10 }, flats) });
    }

    return scales;
}

juce::String progressionToString (const Progression& progression)
{
    juce::StringArray parts;

    for (const auto& s : progression)
        parts.add (juce::String (s.chord.degree) + ":" + infoFor (s.chord.quality).code + ":" + juce::String (s.beats));

    return parts.joinIntoString (" ");
}

Progression progressionFromString (const juce::String& text)
{
    Progression progression;
    int beats = 0;

    for (const auto& part : juce::StringArray::fromTokens (text, " ", {}))
    {
        const auto fields = juce::StringArray::fromTokens (part, ":", {});

        if (fields.size() != 3)
            return {};

        Slot s;
        s.chord.degree = wrap (fields[0].getIntValue());
        s.beats = fields[2].getIntValue();

        const auto info = std::find_if (qualities.begin(), qualities.end(),
                                         [&] (const QualityInfo& q) { return fields[1] == q.code; });

        if (info == qualities.end() || (s.beats != 2 && s.beats != 4 && s.beats != 8 && s.beats != 16))
            return {};

        s.chord.quality = info->quality;
        beats += s.beats;

        if (beats > maxProgressionBeats)
            return {};

        progression.push_back (s);
    }

    return progression;
}

int totalBeats (const Progression& progression)
{
    int beats = 0;

    for (const auto& s : progression)
        beats += s.beats;

    return beats;
}
}
