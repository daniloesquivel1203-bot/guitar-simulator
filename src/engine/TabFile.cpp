#include "TabFile.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace tab
{
namespace
{
    using Xml = juce::XmlElement;

    juce::String childText (const Xml* e, const char* name)
    {
        if (e == nullptr)
            return {};

        if (auto* c = e->getChildByName (name))
            return c->getAllSubText().trim();

        return {};
    }

    std::vector<juce::String> splitIds (const juce::String& text)
    {
        std::vector<juce::String> ids;

        for (auto& t : juce::StringArray::fromTokens (text, " \t\r\n", {}))
            if (t.isNotEmpty())
                ids.push_back (t);

        return ids;
    }

    /** Finds a <Property name="..."> anywhere under an element. */
    const Xml* findProperty (const Xml* e, const juce::String& name)
    {
        if (e == nullptr)
            return nullptr;

        for (auto* c : e->getChildIterator())
        {
            if (c->hasTagName ("Property") && c->getStringAttribute ("name") == name)
                return c;

            if (auto* found = findProperty (c, name))
                return found;
        }

        return nullptr;
    }

    /** First descendant with this tag, depth first. */
    const Xml* findDeep (const Xml* e, const char* tag)
    {
        if (e == nullptr)
            return nullptr;

        for (auto* c : e->getChildIterator())
        {
            if (c->hasTagName (tag))
                return c;

            if (auto* found = findDeep (c, tag))
                return found;
        }

        return nullptr;
    }

    void sortBeats (Bar& bar)
    {
        std::stable_sort (bar.beats.begin(), bar.beats.end(),
                          [] (const Beat& a, const Beat& b) { return a.position < b.position; });

        // Voices that play at the same moment become one beat on the tab.
        std::vector<Beat> merged;

        for (auto& b : bar.beats)
        {
            if (! merged.empty() && std::abs (merged.back().position - b.position) < 1.0e-6)
            {
                auto& m = merged.back();

                if (m.notes.empty())
                    m.length = b.length;
                else if (! b.notes.empty())
                    m.length = juce::jmin (m.length, b.length);

                m.notes.insert (m.notes.end(), b.notes.begin(), b.notes.end());
            }
            else
            {
                merged.push_back (std::move (b));
            }
        }

        bar.beats = std::move (merged);
    }

    //==========================================================================
    // Guitar Pro 7 / 8

    double rhythmLength (const Xml* rhythm)
    {
        if (rhythm == nullptr)
            return 1.0;

        static const std::map<juce::String, double> values {
            { "Long", 16.0 }, { "DoubleWhole", 8.0 }, { "Whole", 4.0 }, { "Half", 2.0 },
            { "Quarter", 1.0 }, { "Eighth", 0.5 }, { "16th", 0.25 }, { "32nd", 0.125 },
            { "64th", 0.0625 }, { "128th", 0.03125 }, { "256th", 0.015625 } };

        double length = 1.0;
        auto it = values.find (childText (rhythm, "NoteValue"));

        if (it != values.end())
            length = it->second;

        if (auto* dot = rhythm->getChildByName ("AugmentationDot"))
        {
            const int dots = dot->getIntAttribute ("count", 0);
            double add = length;

            for (int i = 0; i < dots; ++i)
            {
                add *= 0.5;
                length += add;
            }
        }

        if (auto* tuplet = rhythm->getChildByName ("PrimaryTuplet"))
        {
            const int num = tuplet->getIntAttribute ("num", 1);
            const int den = tuplet->getIntAttribute ("den", 1);

            if (num > 0 && den > 0)
                length = length * den / num;
        }

        return length;
    }

    LoadResult parseGpifElement (const Xml& root)
    {
        LoadResult result;
        auto& song = result.song;

        if (! root.hasTagName ("GPIF"))
        {
            result.error = "This is not a Guitar Pro score.";
            return result;
        }

        if (auto* score = root.getChildByName ("Score"))
        {
            song.title = childText (score, "Title");
            song.artist = childText (score, "Artist");
        }

        auto indexById = [] (const Xml* parent, const char* tag)
        {
            std::map<juce::String, const Xml*> byId;

            if (parent != nullptr)
                for (auto* c : parent->getChildWithTagNameIterator (tag))
                    byId[c->getStringAttribute ("id")] = c;

            return byId;
        };

        const auto bars    = indexById (root.getChildByName ("Bars"), "Bar");
        const auto voices  = indexById (root.getChildByName ("Voices"), "Voice");
        const auto beats   = indexById (root.getChildByName ("Beats"), "Beat");
        const auto notes   = indexById (root.getChildByName ("Notes"), "Note");
        const auto rhythms = indexById (root.getChildByName ("Rhythms"), "Rhythm");

        // Tracks
        std::vector<std::vector<int>> drumMaps;   // articulation index -> MIDI note, per track

        if (auto* tracks = root.getChildByName ("Tracks"))
        {
            for (auto* t : tracks->getChildWithTagNameIterator ("Track"))
            {
                Track track;
                track.name = childText (t, "Name");

                std::vector<int> drumMap;

                if (auto* set = t->getChildByName ("InstrumentSet"))
                {
                    if (childText (set, "Type").containsIgnoreCase ("drum"))
                        track.percussion = true;

                    if (auto* elements = set->getChildByName ("Elements"))
                        for (auto* el : elements->getChildWithTagNameIterator ("Element"))
                            if (auto* arts = el->getChildByName ("Articulations"))
                                for (auto* a : arts->getChildWithTagNameIterator ("Articulation"))
                                    drumMap.push_back (childText (a, "OutputMidiNumber").getIntValue());
                }

                if (auto* tuning = findProperty (t, "Tuning"))
                {
                    for (auto& p : splitIds (childText (tuning, "Pitches")))
                        track.tuning.push_back (p.getIntValue());
                }

                if (auto* capo = findProperty (t, "CapoFret"))
                    track.capo = childText (capo, "Fret").getIntValue();

                if (auto* program = findDeep (t, "Program"))
                    track.midiProgram = juce::jlimit (0, 127, program->getAllSubText().trim().getIntValue());

                if (auto* channel = findDeep (t, "PrimaryChannel"))
                    if (channel->getAllSubText().trim().getIntValue() == 9)
                        track.percussion = true;

                if (track.percussion)
                    track.tuning.clear();

                if (track.name.isEmpty())
                    track.name = "Track " + juce::String (song.tracks.size() + 1);

                song.tracks.push_back (std::move (track));
                drumMaps.push_back (std::move (drumMap));
            }
        }

        if (song.tracks.empty())
        {
            result.error = "The score has no tracks.";
            return result;
        }

        // Master bars and each track's bars
        std::vector<std::vector<juce::String>> barIdsPerMasterBar;

        if (auto* masterBars = root.getChildByName ("MasterBars"))
        {
            for (auto* m : masterBars->getChildWithTagNameIterator ("MasterBar"))
            {
                MasterBar mb;
                const auto time = juce::StringArray::fromTokens (childText (m, "Time"), "/", {});

                if (time.size() == 2)
                {
                    mb.numerator = juce::jmax (1, time[0].getIntValue());
                    mb.denominator = juce::jmax (1, time[1].getIntValue());
                }

                mb.length = mb.numerator * 4.0 / mb.denominator;

                if (auto* repeat = m->getChildByName ("Repeat"))
                {
                    mb.repeatStart = repeat->getStringAttribute ("start").equalsIgnoreCase ("true");

                    if (repeat->getStringAttribute ("end").equalsIgnoreCase ("true"))
                        mb.repeatCount = juce::jmax (2, repeat->getIntAttribute ("count", 2));
                }

                for (auto& e : splitIds (childText (m, "AlternateEndings")))
                    if (const int n = e.getIntValue(); n >= 1 && n <= 30)
                        mb.endings |= 1 << (n - 1);

                if (auto* section = m->getChildByName ("Section"))
                {
                    mb.section = childText (section, "Text");

                    if (mb.section.isEmpty())
                        mb.section = childText (section, "Letter");
                }

                song.masterBars.push_back (mb);
                barIdsPerMasterBar.push_back (splitIds (childText (m, "Bars")));
            }
        }

        if (song.masterBars.empty())
        {
            result.error = "The score has no bars.";
            return result;
        }

        // Tempo
        if (auto* masterTrack = root.getChildByName ("MasterTrack"))
        {
            if (auto* automations = masterTrack->getChildByName ("Automations"))
            {
                static const double unitToQuarters[] = { 1.0, 0.5, 1.0, 1.5, 2.0, 3.0 };

                for (auto* a : automations->getChildWithTagNameIterator ("Automation"))
                {
                    if (childText (a, "Type") != "Tempo")
                        continue;

                    const auto parts = splitIds (childText (a, "Value"));

                    if (parts.empty())
                        continue;

                    int unit = parts.size() > 1 ? parts[1].getIntValue() : 2;

                    if (unit < 1 || unit > 5)
                        unit = 2;

                    const double bpm = parts[0].getDoubleValue() * unitToQuarters[unit];
                    const int barIndex = childText (a, "Bar").getIntValue();
                    const double position = juce::jlimit (0.0, 1.0, childText (a, "Position").getDoubleValue());

                    if (bpm <= 0.0 || barIndex < 0 || barIndex >= (int) song.masterBars.size())
                        continue;

                    if (barIndex == 0 && position <= 0.0)
                        song.initialBpm = bpm;
                    else
                        song.masterBars[(size_t) barIndex].tempoChanges.push_back ({ position, bpm });
                }

                for (auto& mb : song.masterBars)
                    std::sort (mb.tempoChanges.begin(), mb.tempoChanges.end(),
                               [] (const TempoChange& a, const TempoChange& b) { return a.position < b.position; });
            }
        }

        // Notes, bar by bar
        for (size_t ti = 0; ti < song.tracks.size(); ++ti)
        {
            auto& track = song.tracks[ti];
            track.bars.resize (song.masterBars.size());

            for (size_t mi = 0; mi < song.masterBars.size(); ++mi)
            {
                const auto& barIds = barIdsPerMasterBar[mi];

                if (ti >= barIds.size())
                    continue;

                auto barIt = bars.find (barIds[ti]);

                if (barIt == bars.end())
                    continue;

                auto& bar = track.bars[mi];

                for (auto& voiceId : splitIds (childText (barIt->second, "Voices")))
                {
                    auto voiceIt = voices.find (voiceId);

                    if (voiceId == "-1" || voiceIt == voices.end())
                        continue;

                    double position = 0.0;

                    for (auto& beatId : splitIds (childText (voiceIt->second, "Beats")))
                    {
                        auto beatIt = beats.find (beatId);

                        if (beatIt == beats.end())
                            continue;

                        const auto* beatXml = beatIt->second;

                        // Grace notes take no time of their own; leave them out.
                        if (beatXml->getChildByName ("GraceNotes") != nullptr)
                            continue;

                        const Xml* rhythm = nullptr;

                        if (auto* r = beatXml->getChildByName ("Rhythm"))
                            if (auto it = rhythms.find (r->getStringAttribute ("ref")); it != rhythms.end())
                                rhythm = it->second;

                        Beat beat;
                        beat.position = position;
                        beat.length = rhythmLength (rhythm);

                        for (auto& noteId : splitIds (childText (beatXml, "Notes")))
                        {
                            auto noteIt = notes.find (noteId);

                            if (noteIt == notes.end())
                                continue;

                            const auto* n = noteIt->second;
                            Note note;

                            if (auto* tie = n->getChildByName ("Tie"))
                                note.tiedFromPrevious = tie->getStringAttribute ("destination").equalsIgnoreCase ("true");

                            note.dead = findProperty (n, "Muted") != nullptr;
                            note.palmMuted = findProperty (n, "PalmMuted") != nullptr;

                            int midi = -1;

                            if (auto* p = findProperty (n, "Midi"))
                                midi = childText (p, "Number").getIntValue();

                            if (track.percussion)
                            {
                                note.string = -1;

                                const auto artText = childText (n, "InstrumentArticulation");
                                const int art = artText.getIntValue();

                                if (artText.isNotEmpty() && art >= 0 && art < (int) drumMaps[ti].size()
                                     && drumMaps[ti][(size_t) art] > 0)
                                    midi = drumMaps[ti][(size_t) art];

                                note.midiNote = midi > 0 ? midi : 38;
                            }
                            else if (auto* s = findProperty (n, "String"); s != nullptr && track.hasTab())
                            {
                                note.string = juce::jlimit (0, (int) track.tuning.size() - 1,
                                                            childText (s, "String").getIntValue());

                                if (auto* f = findProperty (n, "Fret"))
                                    note.fret = childText (f, "Fret").getIntValue();

                                note.midiNote = track.tuning[(size_t) note.string] + note.fret + track.capo;
                            }
                            else
                            {
                                // Instruments without strings store a pitch class and octave.
                                if (midi < 0)
                                    if (auto* octave = findProperty (n, "Octave"))
                                        midi = 12 * childText (octave, "Number").getIntValue()
                                               + childText (findProperty (n, "Tone"), "Step").getIntValue();

                                if (midi < 0)
                                    continue;

                                note.midiNote = midi;

                                if (track.hasTab()
                                     && ! fingerPitch (track.tuning, track.capo, midi, 5, note.string, note.fret))
                                    note.string = -1;
                                else if (! track.hasTab())
                                    note.string = -1;
                            }

                            beat.notes.push_back (note);
                        }

                        position += beat.length;
                        bar.beats.push_back (std::move (beat));
                    }
                }

                sortBeats (bar);
            }
        }

        return result;
    }

    //==========================================================================
    // MusicXML

    int stepToSemitone (const juce::String& step)
    {
        static const std::map<juce::String, int> steps {
            { "C", 0 }, { "D", 2 }, { "E", 4 }, { "F", 5 }, { "G", 7 }, { "A", 9 }, { "B", 11 } };

        auto it = steps.find (step.trim().toUpperCase());
        return it != steps.end() ? it->second : 0;
    }

    struct PartInfo
    {
        juce::String name;
        int program = 0;
        bool percussion = false;
    };

    LoadResult parseMusicXmlElement (const Xml& root)
    {
        LoadResult result;
        auto& song = result.song;

        if (root.hasTagName ("score-timewise"))
        {
            result.error = "Time-wise MusicXML is not supported. Export it as part-wise MusicXML.";
            return result;
        }

        if (! root.hasTagName ("score-partwise"))
        {
            result.error = "This is not a MusicXML score.";
            return result;
        }

        song.title = childText (root.getChildByName ("work"), "work-title");

        if (song.title.isEmpty())
            song.title = childText (&root, "movement-title");

        if (auto* id = root.getChildByName ("identification"))
            for (auto* c : id->getChildWithTagNameIterator ("creator"))
                if (c->getStringAttribute ("type") == "composer" || song.artist.isEmpty())
                    song.artist = c->getAllSubText().trim();

        std::map<juce::String, PartInfo> partInfo;

        if (auto* list = root.getChildByName ("part-list"))
        {
            for (auto* sp : list->getChildWithTagNameIterator ("score-part"))
            {
                PartInfo info;
                info.name = childText (sp, "part-name");

                if (auto* midi = sp->getChildByName ("midi-instrument"))
                {
                    const auto program = childText (midi, "midi-program");

                    if (program.isNotEmpty())
                        info.program = juce::jlimit (0, 127, program.getIntValue() - 1);

                    info.percussion = childText (midi, "midi-channel").getIntValue() == 10;
                }

                partInfo[sp->getStringAttribute ("id")] = info;
            }
        }

        bool first = true;

        for (auto* part : root.getChildWithTagNameIterator ("part"))
        {
            Track track;
            const auto info = partInfo[part->getStringAttribute ("id")];
            track.name = info.name.isNotEmpty() ? info.name : "Part " + juce::String (song.tracks.size() + 1);
            track.midiProgram = info.program;
            track.percussion = info.percussion;

            double divisions = 1.0;
            int numerator = 4, denominator = 4;
            int tabStaff = 0;       // staff number that holds the tab, 0 = no separate tab staff
            int lastFret = 5;

            // First look for a tab staff, so notes can be taken from it alone
            // when a part shows both standard notation and tab.
            for (auto* measure : part->getChildWithTagNameIterator ("measure"))
            {
                for (auto* attributes : measure->getChildWithTagNameIterator ("attributes"))
                {
                    for (auto* details : attributes->getChildWithTagNameIterator ("staff-details"))
                    {
                        std::vector<std::pair<int, int>> lines;   // line number, pitch

                        for (auto* st : details->getChildWithTagNameIterator ("staff-tuning"))
                        {
                            const int pitch = 12 * (childText (st, "tuning-octave").getIntValue() + 1)
                                              + stepToSemitone (childText (st, "tuning-step"))
                                              + childText (st, "tuning-alter").getIntValue();
                            lines.push_back ({ st->getIntAttribute ("line", (int) lines.size() + 1), pitch });
                        }

                        if (! lines.empty())
                        {
                            std::sort (lines.begin(), lines.end());   // line 1 is the lowest string
                            track.tuning.clear();

                            for (auto& l : lines)
                                track.tuning.push_back (l.second);

                            tabStaff = details->getIntAttribute ("number", 0);
                        }

                        const auto capo = childText (details, "capo");

                        if (capo.isNotEmpty())
                            track.capo = capo.getIntValue();
                    }

                    for (auto* clef : attributes->getChildWithTagNameIterator ("clef"))
                        if (childText (clef, "sign") == "percussion")
                            track.percussion = true;
                }

                if (! track.tuning.empty())
                    break;
            }

            // A guitar part written only as notes still gets tab, on standard tuning.
            if (track.tuning.empty() && ! track.percussion
                 && (track.name.containsIgnoreCase ("guitar") || (track.midiProgram >= 24 && track.midiProgram <= 31)))
                track.tuning = { 40, 45, 50, 55, 59, 64 };

            if (track.tuning.empty() && ! track.percussion
                 && (track.name.containsIgnoreCase ("bass") || (track.midiProgram >= 32 && track.midiProgram <= 39)))
                track.tuning = { 28, 33, 38, 43 };

            if (track.percussion)
                track.tuning.clear();

            int measureIndex = 0;

            for (auto* measure : part->getChildWithTagNameIterator ("measure"))
            {
                if (first)
                    song.masterBars.push_back ({});

                if (measureIndex >= (int) song.masterBars.size())
                    break;

                auto& mb = song.masterBars[(size_t) measureIndex];
                Bar bar;
                double cursor = 0.0, furthest = 0.0, lastStart = 0.0;
                Beat* lastBeat = nullptr;

                for (auto* e : measure->getChildIterator())
                {
                    if (e->hasTagName ("attributes"))
                    {
                        const auto div = childText (e, "divisions");

                        if (div.isNotEmpty())
                            divisions = juce::jmax (1.0, div.getDoubleValue());

                        if (auto* time = e->getChildByName ("time"))
                        {
                            numerator = juce::jmax (1, childText (time, "beats").getIntValue());
                            denominator = juce::jmax (1, childText (time, "beat-type").getIntValue());
                        }
                    }
                    else if (e->hasTagName ("backup"))
                    {
                        cursor = juce::jmax (0.0, cursor - childText (e, "duration").getDoubleValue() / divisions);
                    }
                    else if (e->hasTagName ("forward"))
                    {
                        cursor += childText (e, "duration").getDoubleValue() / divisions;
                        furthest = juce::jmax (furthest, cursor);
                    }
                    else if (e->hasTagName ("barline") && first)
                    {
                        if (auto* repeat = e->getChildByName ("repeat"))
                        {
                            if (repeat->getStringAttribute ("direction") == "forward")
                                mb.repeatStart = true;
                            else
                                mb.repeatCount = juce::jmax (2, repeat->getIntAttribute ("times", 2));
                        }

                        if (auto* ending = e->getChildByName ("ending"))
                            for (auto& n : juce::StringArray::fromTokens (ending->getStringAttribute ("number"), ", ", {}))
                                if (const int v = n.getIntValue(); v >= 1 && v <= 30)
                                    mb.endings |= 1 << (v - 1);
                    }
                    else if ((e->hasTagName ("direction") || e->hasTagName ("sound")) && first)
                    {
                        const auto* sound = e->hasTagName ("sound") ? e : e->getChildByName ("sound");

                        if (sound != nullptr && sound->hasAttribute ("tempo"))
                        {
                            const double bpm = sound->getDoubleAttribute ("tempo");

                            if (bpm > 0.0)
                            {
                                if (measureIndex == 0 && cursor <= 0.0)
                                    song.initialBpm = bpm;
                                else
                                    mb.tempoChanges.push_back ({ cursor, bpm });   // made a fraction below
                            }
                        }

                        if (e->hasTagName ("direction"))
                            for (auto* type : e->getChildWithTagNameIterator ("direction-type"))
                                if (auto* rehearsal = type->getChildByName ("rehearsal"))
                                    mb.section = rehearsal->getAllSubText().trim();
                    }
                    else if (e->hasTagName ("note"))
                    {
                        if (e->getChildByName ("grace") != nullptr)
                            continue;

                        const double duration = childText (e, "duration").getDoubleValue() / divisions;
                        const bool chord = e->getChildByName ("chord") != nullptr;
                        const int staff = childText (e, "staff").getIntValue();

                        const double start = chord ? lastStart : cursor;

                        if (! chord)
                        {
                            cursor += duration;
                            furthest = juce::jmax (furthest, cursor);
                        }

                        lastStart = start;

                        // Skip the notation copy when the part also has a tab staff.
                        if (tabStaff > 0 && staff > 0 && staff != tabStaff)
                            continue;

                        if (! chord || lastBeat == nullptr)
                        {
                            bar.beats.push_back ({ start, juce::jmax (duration, 1.0e-3), {} });
                            lastBeat = &bar.beats.back();
                        }

                        if (e->getChildByName ("rest") != nullptr)
                            continue;

                        Note note;
                        int midi = -1;

                        if (auto* pitch = e->getChildByName ("pitch"))
                            midi = 12 * (childText (pitch, "octave").getIntValue() + 1)
                                   + stepToSemitone (childText (pitch, "step"))
                                   + juce::roundToInt (childText (pitch, "alter").getDoubleValue());

                        if (auto* instrumentPitch = e->getChildByName ("unpitched"))
                            midi = 12 * (childText (instrumentPitch, "display-octave").getIntValue() + 1)
                                   + stepToSemitone (childText (instrumentPitch, "display-step"));

                        for (auto* tie : e->getChildWithTagNameIterator ("tie"))
                            if (tie->getStringAttribute ("type") == "stop")
                                note.tiedFromPrevious = true;

                        if (childText (e, "notehead") == "x")
                            note.dead = true;

                        const Xml* technical = nullptr;

                        if (auto* notations = e->getChildByName ("notations"))
                            technical = notations->getChildByName ("technical");

                        const auto stringText = childText (technical, "string");
                        const auto fretText = childText (technical, "fret");

                        if (track.percussion)
                        {
                            note.string = -1;
                            note.midiNote = midi > 0 ? midi : 38;
                        }
                        else if (track.hasTab() && stringText.isNotEmpty() && fretText.isNotEmpty())
                        {
                            // MusicXML counts strings from 1 = the highest.
                            const int numStrings = (int) track.tuning.size();
                            note.string = juce::jlimit (0, numStrings - 1, numStrings - stringText.getIntValue());
                            note.fret = fretText.getIntValue();
                            note.midiNote = track.tuning[(size_t) note.string] + note.fret + track.capo;
                        }
                        else
                        {
                            if (midi < 0)
                                continue;

                            note.midiNote = midi;
                            note.string = -1;

                            if (track.hasTab() && fingerPitch (track.tuning, track.capo, midi, lastFret, note.string, note.fret))
                                if (note.fret > 0)
                                    lastFret = note.fret;
                        }

                        lastBeat->notes.push_back (note);
                    }
                }

                if (first)
                {
                    mb.numerator = numerator;
                    mb.denominator = denominator;
                    mb.length = numerator * 4.0 / denominator;

                    // A pickup bar is shorter than its time signature says.
                    if (measure->getStringAttribute ("implicit") == "yes" && furthest > 0.0)
                        mb.length = furthest;

                    for (auto& t : mb.tempoChanges)
                        t.position = juce::jlimit (0.0, 1.0, t.position / mb.length);
                }

                sortBeats (bar);
                track.bars.push_back (std::move (bar));
                ++measureIndex;
            }

            track.bars.resize (song.masterBars.size());
            song.tracks.push_back (std::move (track));
            first = false;
        }

        if (song.isEmpty())
            result.error = "The score has no parts or bars.";

        return result;
    }

    juce::String readZipEntry (juce::ZipFile& zip, const juce::String& name)
    {
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            if (auto* entry = zip.getEntry (i); entry != nullptr && entry->filename.replace ("\\", "/").endsWith (name))
            {
                std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (i));

                if (in != nullptr)
                    return in->readEntireStreamAsString();
            }
        }

        return {};
    }

    LoadResult fail (const juce::String& message)
    {
        LoadResult r;
        r.error = message;
        return r;
    }
}

//==============================================================================
LoadResult parseGpif (const juce::String& xml)
{
    auto root = juce::parseXML (xml);

    if (root == nullptr)
        return fail ("The Guitar Pro score could not be read.");

    return parseGpifElement (*root);
}

LoadResult parseMusicXml (const juce::String& xml)
{
    auto root = juce::parseXML (xml);

    if (root == nullptr)
        return fail ("The MusicXML file could not be read.");

    return parseMusicXmlElement (*root);
}

juce::String supportedWildcard()
{
    return "*.gp;*.musicxml;*.xml;*.mxl";
}

LoadResult loadFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return fail ("The file was not found.");

    const auto ext = file.getFileExtension().toLowerCase();

    if (ext == ".gp3" || ext == ".gp4" || ext == ".gp5" || ext == ".gpx")
        return fail ("Guitar Pro " + ext.substring (3).toUpperCase()
                     + " files aren't supported yet. Open the file in Guitar Pro, TuxGuitar or MuseScore and save it as .gp or MusicXML.");

    juce::MemoryBlock data;

    if (! file.loadFileAsData (data) || data.getSize() < 4)
        return fail ("The file could not be read.");

    const auto* bytes = static_cast<const char*> (data.getData());
    const bool isZip = bytes[0] == 'P' && bytes[1] == 'K';

    LoadResult result;

    if (isZip)
    {
        juce::MemoryInputStream stream (data, false);
        juce::ZipFile zip (stream);

        if (auto gpif = readZipEntry (zip, "score.gpif"); gpif.isNotEmpty())
        {
            result = parseGpif (gpif);
        }
        else
        {
            // Compressed MusicXML: META-INF/container.xml names the score inside.
            juce::String scorePath;

            if (auto container = juce::parseXML (readZipEntry (zip, "META-INF/container.xml")))
                if (auto* rootfile = findDeep (container.get(), "rootfile"))
                    scorePath = rootfile->getStringAttribute ("full-path");

            juce::String scoreXml;

            if (scorePath.isNotEmpty())
                scoreXml = readZipEntry (zip, scorePath);

            for (int i = 0; scoreXml.isEmpty() && i < zip.getNumEntries(); ++i)
            {
                const auto name = zip.getEntry (i)->filename;

                if (! name.startsWithIgnoreCase ("META-INF") && (name.endsWithIgnoreCase (".xml") || name.endsWithIgnoreCase (".musicxml")))
                    scoreXml = readZipEntry (zip, name);
            }

            if (scoreXml.isEmpty())
                return fail ("No Guitar Pro or MusicXML score was found inside this file.");

            result = parseMusicXml (scoreXml);
        }
    }
    else
    {
        const auto text = data.toString();

        if (text.startsWith ("BCFZ") || text.startsWith ("BCFS"))
            return fail ("Guitar Pro 6 files aren't supported yet. Save it as .gp or MusicXML.");

        if (text.contains ("<GPIF"))
            result = parseGpif (text);
        else if (text.contains ("score-partwise") || text.contains ("score-timewise"))
            result = parseMusicXml (text);
        else
            return fail ("This doesn't look like a Guitar Pro 7/8 or MusicXML file. "
                         "Older Guitar Pro files (.gp3, .gp4, .gp5, .gpx) aren't supported yet.");
    }

    if (result.ok() && result.song.title.isEmpty())
        result.song.title = file.getFileNameWithoutExtension();

    return result;
}
}
