#include "JamArranger.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace jam
{
namespace
{
constexpr int stepsPerBar = 16;
constexpr int stepsPerBeat = 4;

enum class GuitarVoicing { power, full, small, shell };

/** The parts each style plays, one character per sixteenth note.

    For every part, "." is a rest and "-" holds the note before. Upper case
    plays harder than lower case. Drums: x hit, X accent, o ghost, O open
    hi-hat, B ride bell. Bass: R root, 3 5 6 7 chord notes, 8 octave, a a note
    leading into the next chord, or "W" for a walking line. Guitar: D/d strum
    down, U/u strum up, M palm-muted, x muted scratch, B S T two-note boogie
    shapes, b f single bass notes, 1 to 6 picked strings (left ringing).
    Keys: C/c chord with the left hand, S/s chord without it, 1 to 4 picked
    notes, or "J" for jazz comping. Organ: C/c chord. */
struct StylePatterns
{
    Style style;
    const char* name;
    const char* description;
    int bpm;
    const char* progression;
    Mode mode;
    bool blues;

    float swing;     // 0 straight, 1 a full triplet feel
    bool swing16;    // swing the sixteenths instead of the eighths

    const char* kick;
    const char* snare;
    const char* sideStick;
    const char* hat;
    const char* hatOddBars;
    const char* ride;
    const char* pedalHat;
    float crashVelocity;
    bool jazzFill;

    const char* bass;
    bool uprightBass;

    const char* guitar;
    GuitarVoicing guitarVoicing;
    float guitarDrive;

    const char* keys;
    const char* organ;
    bool percussiveOrgan;
};

const std::array<StylePatterns, numStyles> styles { {
    { Style::rock, "Rock", "Straight eighths, palm-muted power chords on a crunchy guitar.", 110,
      "I - IV - V - IV", Mode::major, false, 0.0f, false,
      "x.......x.x.....", "....X.......X...", nullptr, "X.x.x.x.X.x.x.x.", "X.x.x.x.X.x.x.O.", nullptr, nullptr, 0.8f, false,
      "R.R.R.R.R.R.R.Ra", false,
      "D-M.M.M.D-M.M.M.", GuitarVoicing::power, 0.6f,
      "c---------------", "c---------------", false },

    { Style::hardRock, "Hard rock", "Driving gallop on the ride, with a heavier guitar.", 140,
      "i - VI - III - VII", Mode::minor, false, 0.0f, false,
      "X.x.....X.x.x...", "....X.......X...", nullptr, nullptr, nullptr, "x.x.x.x.x.x.x.x.", nullptr, 0.9f, false,
      "R.RRR.RRR.RRR.RR", false,
      "D.MMM.MMD.MMM.MM", GuitarVoicing::power, 1.0f,
      "c---------------", "C---------------", false },

    { Style::blues, "Blues shuffle", "Swung triplet feel, walking boogie bass and guitar.", 90,
      "12-bar blues", Mode::major, true, 1.0f, false,
      "x.......x.......", "....X.......X...", nullptr, "X.x.X.x.X.x.X.x.", nullptr, nullptr, nullptr, 0.7f, true,
      "R.3.5.6.8.6.5.3.", false,
      "B.B.S.S.B.B.S.S.", GuitarVoicing::full, 0.3f,
      "..c...c...c...c.", "C---------------", true },

    { Style::jazz, "Jazz swing", "Ride cymbal, walking upright bass and piano comping. Chords become sevenths.", 140,
      "ii - V - I", Mode::major, false, 1.0f, false,
      "o.......o.......", "..........o.....", nullptr, nullptr, nullptr, "X...x.x.X...x.x.", "....x.......x...", 0.4f, true,
      "W", true,
      "c...c...c...c...", GuitarVoicing::shell, 0.0f,
      "J", "c---------------", false },

    { Style::funk, "Funk", "Busy sixteenths, scratchy guitar and short keyboard stabs.", 100,
      "i - IV vamp", Mode::minor, false, 0.3f, true,
      "X.....x...x..x..", "....X..o.o..X..o", nullptr, "XxxxXxxxXxxxXxOx", nullptr, nullptr, nullptr, 0.7f, false,
      "R..8..R...R.7.85", false,
      "D.xUx.DxU.xDx.Ux", GuitarVoicing::small, 0.0f,
      ".S..S..S.S..S...", "c---------------", false },

    { Style::pop, "Pop", "Steady beat with strummed acoustic-style chords and piano.", 100,
      "I - V - vi - IV", Mode::major, false, 0.0f, false,
      "x.....x.x.......", "....X.......X...", nullptr, "x.x.x.x.x.x.x.x.", nullptr, nullptr, nullptr, 0.7f, false,
      "R--.R-R-R--.R-5-", false,
      "D---D-U---U-D-U-", GuitarVoicing::full, 0.0f,
      "C-------C-------", "c---------------", false },

    { Style::country, "Country", "Two-beat bass and a boom-chick guitar.", 120,
      "I - IV - V - IV", Mode::major, false, 0.0f, false,
      "x.......x.......", "....X.......X...", nullptr, "x.x.x.x.x.x.x.x.", nullptr, nullptr, nullptr, 0.6f, false,
      "R---5---R---5---", false,
      "b...D.u.f...D.u.", GuitarVoicing::full, 0.0f,
      "..c...c...c...c.", "c---------------", false },

    { Style::reggae, "Reggae", "One-drop beat, offbeat guitar chops and a bubbling organ.", 75,
      "i - iv vamp", Mode::minor, false, 0.25f, false,
      "........X.......", nullptr, "........x.......", "x.x.x.x.x.x.x.x.", nullptr, nullptr, nullptr, 0.5f, false,
      "R--.R.5.R--.3.5.", false,
      "....U.......U...", GuitarVoicing::small, 0.0f,
      "....S.......S...", ".c.c.c.c.c.c.c.c", true },

    { Style::ballad, "Ballad", "Slow and open, with picked guitar and soft cross-stick.", 70,
      "I - vi - IV - V", Mode::major, false, 0.0f, false,
      "x.......x.x.....", nullptr, "....x.......x...", "x.o.x.o.x.o.x.o.", nullptr, nullptr, nullptr, 0.6f, false,
      "R-------5---R---", false,
      "1-2-3-4-5-4-3-2-", GuitarVoicing::full, 0.0f,
      "C---------------", "c---------------", false },
} };

const StylePatterns& patternsFor (Style style)
{
    for (const auto& s : styles)
        if (s.style == style)
            return s;

    return styles[0];
}

int wrap12 (int pitch)
{
    return ((pitch % 12) + 12) % 12;
}

/** The note with pitch class `pc` that is nearest to `reference`. */
int nearest (int pc, int reference)
{
    const auto low = reference - 6;
    return low + wrap12 (pc - low);
}

/** The note with pitch class `pc` at or above `floor`, within an octave. */
int above (int pc, int floor)
{
    return floor + wrap12 (pc - floor);
}

int clampToRange (int note, int low, int high)
{
    while (note < low)  note += 12;
    while (note > high) note -= 12;
    return note;
}

/** Some styles play every chord with more colour than written. */
Quality colourFor (Style style, Chord chord)
{
    const auto q = chord.quality;

    switch (style)
    {
        case Style::jazz:
            if (q == Quality::major)       return chord.degree == 7 ? Quality::dominant7 : Quality::major7;
            if (q == Quality::minor)       return Quality::minor7;
            if (q == Quality::diminished)  return Quality::halfDiminished7;
            return q;

        case Style::blues:
            if (q == Quality::major)  return Quality::dominant7;
            if (q == Quality::minor)  return Quality::minor7;
            return q;

        case Style::funk:
            if (q == Quality::major)  return Quality::dominant7;
            if (q == Quality::minor)  return Quality::minor7;
            return q;

        case Style::rock:
        case Style::hardRock:
        case Style::pop:
        case Style::country:
        case Style::reggae:
        case Style::ballad:
            break;
    }

    return q;
}

/** Everything about the chord one part needs, in absolute pitch. */
struct ChordNotes
{
    int rootPc = 0;
    Quality quality = Quality::major;
    std::vector<int> intervals;

    int interval (int index, int fallback) const
    {
        return index < (int) intervals.size() ? intervals[(size_t) index] : fallback;
    }

    int third() const
    {
        if (quality == Quality::power)  return 7;
        if (quality == Quality::sus4)   return 5;
        return intervals[1];
    }

    int fifth() const    { return quality == Quality::power ? 7 : intervals[2]; }
    bool hasSeventh() const   { return intervals.size() > 3; }
    int seventh() const  { return hasSeventh() ? intervals[3] : 10; }
};

ChordNotes notesFor (Chord chord, Key key, Style style)
{
    ChordNotes notes;
    notes.rootPc = wrap12 (key.root + chord.degree);
    notes.quality = colourFor (style, chord);
    notes.intervals = chordIntervals (notes.quality);
    return notes;
}

/** Close-position notes of the chord, all within an octave around `centre`. */
std::vector<int> closeVoicing (const std::vector<int>& pitchClasses, int centre)
{
    std::vector<int> notes;

    for (auto pc : pitchClasses)
        notes.push_back (centre - 6 + wrap12 (pc - (centre - 6)));

    std::sort (notes.begin(), notes.end());
    notes.erase (std::unique (notes.begin(), notes.end()), notes.end());
    return notes;
}

std::vector<int> guitarVoicing (const ChordNotes& c, GuitarVoicing type)
{
    const auto root = above (c.rootPc, 40);   // on the low E or A string

    switch (type)
    {
        case GuitarVoicing::power:
            return { root, root + 7, root + 12 };

        case GuitarVoicing::shell:
        {
            const auto low = above (c.rootPc, 40);
            return { low, low + c.seventh(), low + c.third() + 12 };
        }

        case GuitarVoicing::small:
        {
            // Three or four notes on the top strings, with a ninth on seventh chords.
            std::vector<int> pcs { c.rootPc + c.third(), c.rootPc + c.fifth() };

            if (c.hasSeventh())
            {
                pcs.push_back (c.rootPc + c.seventh());

                if (c.quality == Quality::dominant7 || c.quality == Quality::minor7)
                    pcs.push_back (c.rootPc + 2);
            }
            else
            {
                pcs.push_back (c.rootPc);
            }

            return closeVoicing (pcs, 67);
        }

        case GuitarVoicing::full:
            break;
    }

    // A barre chord: root, fifth, octave, then the third, fifth and root above.
    switch (c.quality)
    {
        case Quality::major:           return { root, root + 7, root + 12, root + 16, root + 19, root + 24 };
        case Quality::minor:           return { root, root + 7, root + 12, root + 15, root + 19, root + 24 };
        case Quality::dominant7:       return { root, root + 7, root + 10, root + 16, root + 19, root + 24 };
        case Quality::major7:          return { root, root + 11, root + 16, root + 19 };
        case Quality::minor7:          return { root, root + 7, root + 10, root + 15, root + 19, root + 24 };
        case Quality::halfDiminished7: return { root, root + 10, root + 15, root + 18 };
        case Quality::diminished:      return { root, root + 6, root + 12, root + 15 };
        case Quality::sus4:            return { root, root + 7, root + 12, root + 17, root + 19, root + 24 };
        case Quality::power:           return { root, root + 7, root + 12 };
    }

    return { root, root + 7, root + 12 };
}

/** The right hand of the piano. Jazz leaves the root to the bass. */
std::vector<int> keysVoicing (const ChordNotes& c, Style style)
{
    std::vector<int> pcs;

    if (style == Style::jazz && c.hasSeventh())
    {
        pcs = { c.rootPc + c.third(), c.rootPc + c.fifth(), c.rootPc + c.seventh() };

        if (c.quality != Quality::halfDiminished7 && c.quality != Quality::diminished)
            pcs.push_back (c.rootPc + 2);

        return closeVoicing (pcs, 62);
    }

    for (auto interval : c.intervals)
        pcs.push_back (c.rootPc + interval);

    return closeVoicing (pcs, 66);
}

std::vector<int> organVoicing (const ChordNotes& c)
{
    std::vector<int> pcs;

    for (auto interval : c.intervals)
        pcs.push_back (c.rootPc + interval);

    return closeVoicing (pcs, 60);
}

int bassRoot (const ChordNotes& c)
{
    return above (c.rootPc, 28);   // from the low E string up
}

//==============================================================================
class Builder
{
public:
    Builder (const Progression& p, Key k, Style s, std::uint32_t seed)
        : progression (p), key (k), style (s), patterns (patternsFor (s)), random ((juce::int64) seed)
    {
        int beat = 0;

        for (const auto& slot : progression)
        {
            slotStarts.push_back (beat);
            beat += slot.beats;
        }

        totalBeats = beat;
        numBars = (totalBeats + 3) / 4;
        totalSteps = numBars * stepsPerBar;
    }

    Arrangement build()
    {
        Arrangement result;
        result.lengthBeats = numBars * 4.0;
        result.guitarDrive = patterns.guitarDrive;
        result.uprightBass = patterns.uprightBass;
        result.percussiveOrgan = patterns.percussiveOrgan;

        if (progression.empty())
            return result;

        buildDrums();
        buildBass();
        buildGuitar();
        buildKeys();
        buildPart (Instrument::organ, patterns.organ);

        std::stable_sort (events.begin(), events.end(),
                          [] (const NoteEvent& a, const NoteEvent& b) { return a.beat < b.beat; });

        result.events = std::move (events);
        return result;
    }

private:
    //==============================================================================
    int slotAtStep (int step) const
    {
        // Bars are padded to a whole bar; the padding keeps the last chord.
        const auto beat = juce::jmin (totalBeats - 1, step / stepsPerBeat);
        const auto it = std::upper_bound (slotStarts.begin(), slotStarts.end(), beat);
        return (int) std::distance (slotStarts.begin(), it) - 1;
    }

    ChordNotes chordAtStep (int step) const
    {
        const auto wrapped = ((step % totalSteps) + totalSteps) % totalSteps;
        return notesFor (progression[(size_t) slotAtStep (wrapped)].chord, key, style);
    }

    /** Where a step falls, in beats, with the style's swing. */
    double stepToBeat (int step) const
    {
        auto beat = step / (double) stepsPerBeat;
        const auto inBeat = step % stepsPerBeat;

        if (patterns.swing > 0.0f)
        {
            if (! patterns.swing16 && inBeat == 2)
                beat += patterns.swing / 6.0;
            else if (patterns.swing16 && (inBeat % 2) == 1)
                beat += patterns.swing / 12.0;
        }

        return beat;
    }

    float jitter (float amount)
    {
        return (random.nextFloat() * 2.0f - 1.0f) * amount;
    }

    static float velocityFor (char symbol, float soft, float normal, float loud)
    {
        if (symbol == 'o')
            return soft;

        return juce::CharacterFunctions::isUpperCase ((juce::juce_wchar) symbol) ? loud : normal;
    }

    void add (Instrument instrument, int step, int endStep, float velocity, const std::vector<int>& notes,
              std::uint8_t kind = 0, bool strumUp = false, float gate = 0.95f)
    {
        NoteEvent e;
        e.instrument = instrument;
        e.kind = kind;
        e.strumUp = strumUp;

        const auto timing = instrument == Instrument::drums ? 0.004f : 0.01f;
        e.beat = juce::jmax (0.0, stepToBeat (step) + jitter (timing));
        const auto end = stepToBeat (endStep);
        e.lengthBeats = (float) juce::jmax (0.05, (end - stepToBeat (step)) * gate);
        e.velocity = juce::jlimit (0.05f, 1.0f, velocity + jitter (0.05f));

        for (auto note : notes)
            if (e.numNotes < NoteEvent::maxNotes && note >= 0 && note < 128)
                e.notes[(size_t) e.numNotes++] = (std::int8_t) note;

        events.push_back (e);
    }

    void addDrum (Drum drum, int step, float velocity)
    {
        add (Instrument::drums, step, step + 1, velocity, {}, (std::uint8_t) drum);
    }

    //==============================================================================
    bool isFillBar (int bar) const
    {
        return numBars >= 2 && (bar == numBars - 1 || bar % 8 == 7);
    }

    void buildDrums()
    {
        const auto line = [&] (const char* pattern, Drum drum, int bar, int fromStep, int toStep)
        {
            if (pattern == nullptr)
                return;

            for (int s = fromStep; s < toStep; ++s)
            {
                const auto c = pattern[s];

                if (c == '.' || c == '-')
                    continue;

                auto piece = drum;

                if (drum == Drum::closedHat && c == 'O')    piece = Drum::openHat;
                if (drum == Drum::ride && c == 'B')         piece = Drum::rideBell;

                addDrum (piece, bar * stepsPerBar + s, velocityFor (c, 0.35f, 0.75f, 0.95f));
            }
        };

        for (int bar = 0; bar < numBars; ++bar)
        {
            const auto fill = isFillBar (bar);
            const auto grooveEnd = fill ? 8 : stepsPerBar;
            const auto* hat = (bar % 2 == 1 && patterns.hatOddBars != nullptr) ? patterns.hatOddBars : patterns.hat;

            line (patterns.kick, Drum::kick, bar, 0, fill ? 9 : stepsPerBar);
            line (patterns.snare, Drum::snare, bar, 0, grooveEnd);
            line (patterns.sideStick, Drum::sideStick, bar, 0, grooveEnd);
            line (hat, Drum::closedHat, bar, 0, grooveEnd);
            line (patterns.ride, Drum::ride, bar, 0, patterns.jazzFill ? stepsPerBar : grooveEnd);
            line (patterns.pedalHat, Drum::pedalHat, bar, 0, stepsPerBar);

            // A crash to mark the top of the loop, and every eight bars.
            if (bar % 8 == 0)
                addDrum (Drum::crash, bar * stepsPerBar, patterns.crashVelocity);

            if (fill)
                addFill (bar);
        }
    }

    void addFill (int bar)
    {
        const auto at = bar * stepsPerBar;

        if (patterns.jazzFill)
        {
            addDrum (Drum::snare, at + 10, 0.45f);
            addDrum (Drum::snare, at + 12, 0.7f);
            addDrum (Drum::snare, at + 14, 0.55f);
            addDrum (Drum::kick, at + 14, 0.5f);
            return;
        }

        const auto soft = style == Style::ballad || style == Style::reggae ? 0.6f : 0.85f;
        addDrum (Drum::snare, at + 8, soft);
        addDrum (Drum::snare, at + 10, soft * 0.8f);
        addDrum (Drum::snare, at + 11, soft * 0.7f);
        addDrum (Drum::highTom, at + 12, soft);
        addDrum (Drum::highTom, at + 13, soft * 0.8f);
        addDrum (Drum::midTom, at + 14, soft);
        addDrum (Drum::lowTom, at + 15, soft);
    }

    //==============================================================================
    /** Runs a melodic part's pattern over the whole loop and calls `play` for
        each note with its symbol, start step and end step. A chord change in
        the middle of a held note plays the note again on the new chord. */
    template <typename Play>
    void walkPattern (const char* pattern, Play&& play)
    {
        if (pattern == nullptr)
            return;

        const auto length = (int) std::strlen (pattern);
        char current = 0;
        int start = 0;

        const auto finish = [&] (int end)
        {
            if (current != 0)
                play (current, start, end);

            current = 0;
        };

        for (int step = 0; step < totalSteps; ++step)
        {
            const auto c = pattern[step % length];
            const bool chordChanged = step > 0 && slotAtStep (step) != slotAtStep (step - 1);

            if (c == '.')
            {
                finish (step);
            }
            else if (c == '-')
            {
                if (chordChanged && current != 0)
                {
                    const auto again = current;
                    finish (step);
                    current = again;
                    start = step;
                }
            }
            else
            {
                finish (step);
                current = c;
                start = step;
            }
        }

        finish (totalSteps);
    }

    //==============================================================================
    void buildBass()
    {
        if (std::string (patterns.bass) == "W")
            return buildWalkingBass();

        walkPattern (patterns.bass, [this] (char c, int start, int end)
        {
            const auto chord = chordAtStep (start);
            const auto root = bassRoot (chord);
            int note = root;

            switch (c)
            {
                case '3': note = root + chord.third(); break;
                case '5': note = root + chord.fifth(); break;
                case '6': note = root + 9; break;
                case '7': note = root + chord.seventh(); break;
                case '8': note = root + 12; break;
                case 'a':
                {
                    // Leads into the next chord from a semitone below, or plays the fifth.
                    const auto next = chordAtStep (start + 2);
                    note = next.rootPc != chord.rootPc ? nearest (next.rootPc, root) - 1 : root + 7;
                    break;
                }
                default: break;
            }

            add (Instrument::bass, start, end, velocityFor (c, 0.5f, 0.7f, 0.85f),
                 { clampToRange (note, 28, 52) }, 0, false, 0.9f);
        });
    }

    void buildWalkingBass()
    {
        const auto totalLoopBeats = numBars * 4;
        auto previous = bassRoot (chordAtStep (0)) + 12;

        for (int beat = 0; beat < totalLoopBeats; ++beat)
        {
            const auto step = beat * stepsPerBeat;
            const auto slot = slotAtStep (step);
            const auto chord = chordAtStep (step);
            const auto slotStart = beat < totalBeats ? slotStarts[(size_t) slot] : beat;
            const auto nextChord = chordAtStep (step + stepsPerBeat);
            const bool changeNext = slotAtStep ((step + stepsPerBeat) % totalSteps) != slot || beat + 1 == totalLoopBeats;

            int note;

            if (beat == slotStart || beat % 4 == 0)
            {
                note = nearest (chord.rootPc, previous);
            }
            else if (changeNext)
            {
                // A chromatic step into the next chord's root.
                const auto target = nearest (nextChord.rootPc, previous);
                note = target + (random.nextBool() ? -1 : 1);
            }
            else
            {
                const std::array<int, 4> choices { chord.third(), chord.fifth(), chord.seventh(), 2 };
                note = nearest (chord.rootPc + choices[(size_t) random.nextInt (4)], previous);

                if (note == previous)
                    note = nearest (chord.rootPc + chord.fifth(), previous + 3);
            }

            note = clampToRange (note, 28, 50);
            add (Instrument::bass, step, step + stepsPerBeat, beat % 2 == 0 ? 0.8f : 0.7f, { note }, 0, false, 0.92f);
            previous = note;
        }
    }

    //==============================================================================
    void buildGuitar()
    {
        walkPattern (patterns.guitar, [this] (char c, int start, int end)
        {
            const auto chord = chordAtStep (start);
            const auto voicing = guitarVoicing (chord, patterns.guitarVoicing);
            const auto root = above (chord.rootPc, 40);
            const auto upper = juce::CharacterFunctions::isUpperCase ((juce::juce_wchar) c);
            const auto velocity = upper ? 0.85f : 0.6f;

            std::vector<int> notes = voicing;
            auto articulation = Articulation::ring;
            bool strumUp = false;
            float gate = 0.95f;

            switch (c)
            {
                case 'D': case 'd': break;

                case 'U': case 'u':
                    strumUp = true;
                    if (notes.size() > 4)
                        notes.erase (notes.begin(), notes.end() - 4);
                    break;

                case 'M':
                    articulation = Articulation::palmMute;
                    notes.resize (juce::jmin ((size_t) 3, notes.size()));
                    break;

                case 'x': articulation = Articulation::dead; gate = 0.5f; break;
                case 'c': gate = 0.6f; break;
                case 'B': notes = { root, root + 7 };  articulation = Articulation::palmMute; break;
                case 'S': notes = { root, root + 9 };  articulation = Articulation::palmMute; break;
                case 'T': notes = { root, root + 10 }; articulation = Articulation::palmMute; break;
                case 'b': notes = { root }; break;
                case 'f': notes = { root + 7 > 52 ? root - 5 : root + 7 }; break;

                default:
                    if (c >= '1' && c <= '6')
                    {
                        // Picked strings ring on until the chord changes.
                        notes = { voicing[(size_t) juce::jmin ((int) voicing.size() - 1, c - '1')] };
                        end = start + 1;

                        while (end < totalSteps && slotAtStep (end) == slotAtStep (start) && end - start < 8)
                            ++end;
                    }
                    break;
            }

            add (Instrument::guitar, start, end, velocity, notes, (std::uint8_t) articulation, strumUp, gate);
        });
    }

    //==============================================================================
    void buildKeys()
    {
        if (std::string (patterns.keys) == "J")
            return buildJazzComping();

        walkPattern (patterns.keys, [this] (char c, int start, int end)
        {
            const auto chord = chordAtStep (start);
            auto right = keysVoicing (chord, style);
            const auto upper = juce::CharacterFunctions::isUpperCase ((juce::juce_wchar) c);

            if (c >= '1' && c <= '4')
            {
                add (Instrument::keys, start, end, 0.6f, { right[(size_t) juce::jmin ((int) right.size() - 1, c - '1')] });
                return;
            }

            if (c == 'C' || c == 'c')
                right.insert (right.begin(), above (chord.rootPc, 36));

            add (Instrument::keys, start, end, upper ? 0.75f : 0.5f, right);
        });
    }

    void buildJazzComping()
    {
        // Short rhythmic figures, a different one each bar, as a pianist would play.
        struct Hit { int step, length; };
        static const std::vector<std::vector<Hit>> figures {
            { { 0, 6 }, { 6, 2 } },
            { { 6, 4 }, { 14, 2 } },
            { { 0, 4 }, { 10, 2 } },
            { { 4, 2 }, { 10, 4 } },
            { { 2, 2 }, { 8, 6 } },
            { { 0, 2 }, { 6, 2 }, { 12, 4 } },
        };

        for (int bar = 0; bar < numBars; ++bar)
        {
            const auto& figure = figures[(size_t) random.nextInt ((int) figures.size())];

            for (const auto& hit : figure)
            {
                const auto step = bar * stepsPerBar + hit.step;
                add (Instrument::keys, step, step + hit.length, 0.5f + random.nextFloat() * 0.15f,
                     keysVoicing (chordAtStep (step), style), 0, false, 0.85f);
            }
        }
    }

    //==============================================================================
    void buildPart (Instrument instrument, const char* pattern)
    {
        walkPattern (pattern, [this, instrument] (char c, int start, int end)
        {
            const auto upper = juce::CharacterFunctions::isUpperCase ((juce::juce_wchar) c);
            add (instrument, start, end, upper ? 0.7f : 0.5f, organVoicing (chordAtStep (start)), 0, false,
                 end - start <= 1 ? 0.6f : 0.98f);
        });
    }

    const Progression& progression;
    Key key;
    Style style;
    const StylePatterns& patterns;
    juce::Random random;

    std::vector<int> slotStarts;
    int totalBeats = 0;
    int numBars = 0;
    int totalSteps = 0;
    std::vector<NoteEvent> events;
};
}

//==============================================================================
juce::String instrumentName (Instrument instrument)
{
    switch (instrument)
    {
        case Instrument::drums:   return "Drums";
        case Instrument::bass:    return "Bass";
        case Instrument::guitar:  return "Rhythm guitar";
        case Instrument::keys:    return "Piano";
        case Instrument::organ:   return "Organ";
    }

    return {};
}

const std::vector<StyleInfo>& getStyles()
{
    static const std::vector<StyleInfo> infos = []
    {
        std::vector<StyleInfo> list;

        for (const auto& s : styles)
            list.push_back ({ s.style, s.name, s.description, s.bpm, s.progression, s.mode, s.blues });

        return list;
    }();

    return infos;
}

const StyleInfo& getStyleInfo (Style style)
{
    for (const auto& info : getStyles())
        if (info.style == style)
            return info;

    return getStyles().front();
}

Arrangement arrange (const Progression& progression, Key key, Style style, std::uint32_t seed)
{
    return Builder (progression, key, style, seed).build();
}
}
