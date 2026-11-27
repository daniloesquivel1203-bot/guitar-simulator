#include "TabArrangement.h"

#include <algorithm>
#include <map>

namespace tab
{
    jam::Instrument instrumentFor (const Track& track)
    {
        if (track.percussion)
            return jam::Instrument::drums;

        const int p = track.midiProgram;

        if (p >= 24 && p <= 31)  return jam::Instrument::guitar;
        if (p >= 32 && p <= 39)  return jam::Instrument::bass;
        if (p >= 16 && p <= 23)  return jam::Instrument::organ;

        // Strings, choirs, brass, reeds, pipes and pads hold their notes, which
        // the organ does better than the piano.
        if ((p >= 40 && p <= 79) || (p >= 88 && p <= 95))
            return jam::Instrument::organ;

        // Tracks with strings but an odd program (some files leave it at 0)
        // are still guitars or basses.
        if (track.hasTab() && p == 0)
            return track.tuning.front() < 36 ? jam::Instrument::bass : jam::Instrument::guitar;

        return jam::Instrument::keys;
    }

    jam::Drum drumFor (int midiNote)
    {
        using D = jam::Drum;

        switch (midiNote)
        {
            case 35: case 36:                     return D::kick;
            case 37:                              return D::sideStick;
            case 38: case 39: case 40:            return D::snare;
            case 41: case 43:                     return D::lowTom;
            case 45: case 47:                     return D::midTom;
            case 48: case 50:                     return D::highTom;
            case 42:                              return D::closedHat;
            case 44:                              return D::pedalHat;
            case 46:                              return D::openHat;
            case 49: case 52: case 55: case 57:   return D::crash;
            case 51: case 59:                     return D::ride;
            case 53: case 56:                     return D::rideBell;
            default:                              break;
        }

        // Shakers, tambourines and other hand percussion: a quiet hat fits best.
        return D::closedHat;
    }

    jam::Arrangement arrangeSong (const Song& song, const std::vector<PlayedBar>& order, const ArrangeOptions& options)
    {
        jam::Arrangement arrangement;
        arrangement.loops = false;
        arrangement.lengthBeats = juce::jmax (1.0, totalBeats (song, order));

        const double speed = juce::jlimit (0.1, 4.0, options.speed);

        for (auto& point : tempoMap (song, order))
            arrangement.tempoMap.push_back ({ point.beat, point.bpm * speed });

        for (size_t ti = 0; ti < song.tracks.size(); ++ti)
        {
            if (ti < options.trackOn.size() && ! options.trackOn[ti])
                continue;

            const auto& track = song.tracks[ti];
            const auto instrument = instrumentFor (track);

            if (instrument == jam::Instrument::guitar && (track.midiProgram == 29 || track.midiProgram == 30))
                arrangement.guitarDrive = 0.7f;

            if (instrument == jam::Instrument::bass && track.midiProgram == 32)
                arrangement.uprightBass = true;

            if (instrument == jam::Instrument::organ && track.midiProgram == 17)
                arrangement.percussiveOrgan = true;

            // Where each sounding pitch's event is, so a tied note can lengthen it.
            std::map<int, size_t> lastEventForPitch;

            for (const auto& played : order)
            {
                if ((size_t) played.masterBar >= track.bars.size())
                    continue;

                const auto& bar = track.bars[(size_t) played.masterBar];

                for (const auto& beat : bar.beats)
                {
                    const double at = played.startBeat + beat.position;

                    if (instrument == jam::Instrument::drums)
                    {
                        for (const auto& note : beat.notes)
                        {
                            jam::NoteEvent e;
                            e.beat = at;
                            e.lengthBeats = (float) beat.length;
                            e.velocity = 0.8f;
                            e.instrument = instrument;
                            e.kind = (std::uint8_t) drumFor (note.midiNote);
                            arrangement.events.push_back (e);
                        }

                        continue;
                    }

                    std::vector<int> pitches;
                    bool dead = false, palmMuted = false;

                    for (const auto& note : beat.notes)
                    {
                        if (note.tiedFromPrevious)
                        {
                            if (auto it = lastEventForPitch.find (note.midiNote); it != lastEventForPitch.end())
                            {
                                auto& held = arrangement.events[it->second];
                                held.lengthBeats = (float) juce::jmax ((double) held.lengthBeats,
                                                                       at + beat.length - held.beat);
                                continue;
                            }
                        }

                        pitches.push_back (juce::jlimit (0, 127, note.midiNote));
                        dead = dead || note.dead;
                        palmMuted = palmMuted || note.palmMuted;
                    }

                    if (pitches.empty())
                        continue;

                    std::sort (pitches.begin(), pitches.end());
                    pitches.erase (std::unique (pitches.begin(), pitches.end()), pitches.end());

                    // The band plays at most six notes at once; keep the bass note
                    // and the highest ones.
                    while ((int) pitches.size() > jam::NoteEvent::maxNotes)
                        pitches.erase (pitches.begin() + 1);

                    jam::NoteEvent e;
                    e.beat = at;
                    e.lengthBeats = (float) beat.length;
                    e.velocity = dead ? 0.6f : 0.8f;
                    e.instrument = instrument;
                    e.kind = (std::uint8_t) (dead ? jam::Articulation::dead
                                                  : palmMuted ? jam::Articulation::palmMute
                                                              : jam::Articulation::ring);
                    e.numNotes = (std::uint8_t) pitches.size();

                    for (size_t i = 0; i < pitches.size() && i < e.notes.size(); ++i)
                        e.notes[i] = (std::int8_t) pitches[i];

                    const size_t index = arrangement.events.size();
                    arrangement.events.push_back (e);

                    for (int p : pitches)
                        lastEventForPitch[p] = index;
                }
            }
        }

        std::stable_sort (arrangement.events.begin(), arrangement.events.end(),
                          [] (const jam::NoteEvent& a, const jam::NoteEvent& b) { return a.beat < b.beat; });

        return arrangement;
    }
}
