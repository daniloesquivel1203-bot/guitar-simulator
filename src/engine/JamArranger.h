#pragma once

#include "JamTheory.h"

#include <array>
#include <cstdint>
#include <vector>

/** Turns a key, a chord progression and a style into the notes each backing
    instrument plays. Runs on the message thread; the result is handed to the
    audio thread by JamTrack. */
namespace jam
{
enum class Instrument : std::uint8_t { drums, bass, guitar, keys, organ };
constexpr int numInstruments = 5;

juce::String instrumentName (Instrument);

enum class Drum : std::uint8_t
{
    kick, snare, sideStick, closedHat, openHat, pedalHat, ride, rideBell, crash, highTom, midTom, lowTom
};

constexpr int numDrums = 12;

/** How a guitar note is played. */
enum class Articulation : std::uint8_t { ring, palmMute, dead };

enum class Style : std::uint8_t { rock, hardRock, blues, jazz, funk, pop, country, reggae, ballad };
constexpr int numStyles = 9;

struct StyleInfo
{
    Style style;
    juce::String name;
    juce::String description;
    int suggestedBpm;
    juce::String suggestedProgression;   // a name from getProgressionPresets()
    Mode suggestedMode;
    bool bluesScales;
};

const std::vector<StyleInfo>& getStyles();
const StyleInfo& getStyleInfo (Style);

/** One thing an instrument plays: a drum hit, a bass note, or a chord. */
struct NoteEvent
{
    static constexpr int maxNotes = 6;

    double beat = 0.0;            // from the start of the loop
    float lengthBeats = 1.0f;
    float velocity = 0.8f;        // 0 to 1
    Instrument instrument = Instrument::drums;
    std::uint8_t kind = 0;        // the Drum for drums, the Articulation for guitar
    std::uint8_t numNotes = 0;
    bool strumUp = false;         // guitar chords: strummed from the high string down
    std::array<std::int8_t, maxNotes> notes {};   // MIDI note numbers, lowest first
};

/** Everything the player needs for one loop. Events are sorted by beat. */
struct Arrangement
{
    std::vector<NoteEvent> events;
    double lengthBeats = 16.0;
    float guitarDrive = 0.0f;     // 0 is a clean guitar
    bool uprightBass = false;
    bool percussiveOrgan = false;

    /** Jam tracks loop forever. A song with `loops` off plays once and stops. */
    bool loops = true;

    /** Tempo changes as {beat, bpm}, sorted by beat. When empty, the band
        follows the metronome's tempo. */
    struct TempoChange { double beat; double bpm; };
    std::vector<TempoChange> tempoMap;
};

/** Builds the loop. The same inputs and seed always give the same notes. */
Arrangement arrange (const Progression&, Key, Style, std::uint32_t seed = 1);
}
