#include "TabSong.h"

#include <algorithm>
#include <cmath>

namespace tab
{
    int Song::firstTabTrack() const noexcept
    {
        for (size_t i = 0; i < tracks.size(); ++i)
            if (tracks[i].hasTab())
                return (int) i;

        return 0;
    }

    std::vector<PlayedBar> playOrder (const Song& song)
    {
        std::vector<PlayedBar> order;
        const int numBars = (int) song.masterBars.size();

        if (numBars == 0)
            return order;

        // Volta endings are counted per repeated section, so remember where the
        // current section starts and which pass we are on.
        int sectionStart = 0;
        int pass = 1;
        double beat = 0.0;
        int bar = 0;
        bool inEnding = false;

        // A badly formed file could loop forever; a real song never needs
        // anywhere near this many bars.
        const int safetyLimit = numBars * 64 + 1024;

        while (bar < numBars && (int) order.size() < safetyLimit)
        {
            const auto& mb = song.masterBars[(size_t) bar];

            // Leaving the last volta ending closes the section, even though its
            // repeat sign (on an earlier ending) was skipped on this pass.
            const bool leftFinalEnding = inEnding && mb.endings == 0;
            inEnding = false;

            if ((mb.repeatStart && bar != sectionStart) || leftFinalEnding)
            {
                sectionStart = bar;
                pass = 1;
            }

            // Skip a volta bracket that is not for this pass.
            if (mb.endings != 0 && (mb.endings & (1 << (pass - 1))) == 0)
            {
                // If this bar closes a repeat, skipping it must not loop back.
                ++bar;
                continue;
            }

            order.push_back ({ bar, beat });
            beat += mb.length;
            inEnding = mb.endings != 0;

            if (mb.repeatCount > 0 && pass < mb.repeatCount)
            {
                ++pass;
                bar = sectionStart;
                inEnding = false;
                continue;
            }

            if (mb.repeatCount > 0)
            {
                // Section finished; the next repeat starts fresh.
                sectionStart = bar + 1;
                pass = 1;
            }

            ++bar;
        }

        return order;
    }

    std::vector<TempoPoint> tempoMap (const Song& song, const std::vector<PlayedBar>& order)
    {
        std::vector<TempoPoint> map { { 0.0, song.initialBpm } };

        for (const auto& played : order)
        {
            const auto& mb = song.masterBars[(size_t) played.masterBar];

            for (const auto& change : mb.tempoChanges)
            {
                const double beat = played.startBeat + change.position * mb.length;

                if (beat <= map.back().beat + 1.0e-9)
                    map.back().bpm = change.bpm;
                else if (std::abs (change.bpm - map.back().bpm) > 1.0e-6)
                    map.push_back ({ beat, change.bpm });
            }
        }

        return map;
    }

    double totalBeats (const Song& song, const std::vector<PlayedBar>& order)
    {
        if (order.empty())
            return 0.0;

        return order.back().startBeat + song.masterBars[(size_t) order.back().masterBar].length;
    }

    double beatToSeconds (const std::vector<TempoPoint>& map, double beat)
    {
        double seconds = 0.0;
        double fromBeat = 0.0;
        double bpm = map.empty() ? 120.0 : map.front().bpm;

        for (size_t i = 1; i < map.size() && map[i].beat < beat; ++i)
        {
            seconds += (map[i].beat - fromBeat) * 60.0 / bpm;
            fromBeat = map[i].beat;
            bpm = map[i].bpm;
        }

        return seconds + (beat - fromBeat) * 60.0 / juce::jmax (1.0, bpm);
    }

    bool fingerPitch (const std::vector<int>& tuning, int capo, int midiNote, int nearFret,
                      int& string, int& fret)
    {
        constexpr int maxFret = 24;
        int bestScore = std::numeric_limits<int>::max();
        bool found = false;

        for (int s = 0; s < (int) tuning.size(); ++s)
        {
            const int f = midiNote - tuning[(size_t) s] - capo;

            if (f < 0 || f > maxFret)
                continue;

            // Open strings are always easy; otherwise stay near the hand.
            const int score = f == 0 ? 0 : std::abs (f - nearFret) + 1;

            if (score < bestScore)
            {
                bestScore = score;
                string = s;
                fret = f;
                found = true;
            }
        }

        return found;
    }

    juce::String noteName (int midiNote)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return names[((midiNote % 12) + 12) % 12];
    }
}
