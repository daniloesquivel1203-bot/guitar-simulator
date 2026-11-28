#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

/** A song read from a tab file, ready to scroll and play.

    Timing is in quarter-note beats. A bar of 4/4 is 4 beats long, a bar of 6/8
    is 3. Positions inside a bar count from the start of that bar.

    Strings count from 0 = the lowest-pitched string, and a track's tuning
    lists its open strings low to high as MIDI note numbers (standard tuning is
    40 45 50 55 59 64).
*/
namespace tab
{
    struct Note
    {
        int string = 0;           // 0 = lowest string; -1 when the track has no strings (drums)
        int fret = 0;
        int midiNote = 0;         // the pitch it sounds, or the drum sound's General MIDI number
        bool tiedFromPrevious = false;   // held over from the previous note: drawn, not replayed
        bool dead = false;        // muted "x" note
        bool palmMuted = false;
    };

    struct Beat
    {
        double position = 0.0;    // beats from the start of the bar
        double length = 1.0;      // beats
        std::vector<Note> notes;  // empty = a rest
    };

    struct Bar
    {
        std::vector<Beat> beats;  // every voice merged, sorted by position
    };

    struct Track
    {
        juce::String name;
        std::vector<int> tuning;  // open strings, low to high; empty for drums
        int capo = 0;
        bool percussion = false;
        int midiProgram = 0;      // General MIDI program, 0 = acoustic piano
        std::vector<Bar> bars;    // one per MasterBar

        bool hasTab() const noexcept   { return ! percussion && ! tuning.empty(); }
    };

    struct TempoChange
    {
        double position = 0.0;    // fraction of the bar, 0 = its start
        double bpm = 120.0;       // quarter notes per minute
    };

    /** Bar-level information shared by every track. */
    struct MasterBar
    {
        int numerator = 4;
        int denominator = 4;
        double length = 4.0;      // beats
        bool repeatStart = false;
        int repeatCount = 0;      // > 0 on the bar that closes a repeat: times the section is played
        int endings = 0;          // bit n set = played on pass n + 1 (volta brackets); 0 = always
        juce::String section;     // rehearsal mark, e.g. "Chorus"
        std::vector<TempoChange> tempoChanges;
    };

    struct Song
    {
        juce::String title, artist;
        double initialBpm = 120.0;
        std::vector<MasterBar> masterBars;
        std::vector<Track> tracks;

        bool isEmpty() const noexcept   { return masterBars.empty() || tracks.empty(); }

        /** The first track that has strings, or 0. */
        int firstTabTrack() const noexcept;
    };

    /** One bar as it is heard: repeats are played out, so a bar inside a
        repeated section appears once for every pass. */
    struct PlayedBar
    {
        int masterBar = 0;
        double startBeat = 0.0;
    };

    /** Bars in playing order, following repeat signs and volta endings. */
    std::vector<PlayedBar> playOrder (const Song&);

    struct TempoPoint
    {
        double beat = 0.0;
        double bpm = 120.0;
    };

    /** Every tempo change on the played timeline, starting with one at beat 0. */
    std::vector<TempoPoint> tempoMap (const Song&, const std::vector<PlayedBar>&);

    /** Total length of the played timeline in beats. */
    double totalBeats (const Song&, const std::vector<PlayedBar>&);

    /** Seconds from the start to a beat, following the tempo map. */
    double beatToSeconds (const std::vector<TempoPoint>&, double beat);

    /** Picks a string and fret for a pitch on a track's tuning, preferring a
        fret close to `nearFret`. Returns false when the pitch is out of range. */
    bool fingerPitch (const std::vector<int>& tuning, int capo, int midiNote, int nearFret,
                      int& string, int& fret);

    /** Note name such as "E" or "A#" for a MIDI note, used for string labels. */
    juce::String noteName (int midiNote);
}
