#pragma once

#include "../Parameter.h"

#include <cmath>
#include <vector>

/** Lets a delay's time follow a tempo instead of its Time knob.

    Each delay has a Sync choice. "Off" uses the Time knob. The other options
    are note values, and the time is that note at the shared tempo, which is
    the metronome's. A note too long or too short for the pedal is halved or
    doubled until it fits, so the echoes still land on the beat.
*/
namespace tempo_sync
{
/** The Sync options, in the order they are stored. Only add to the end:
    presets store the index. */
inline const std::vector<const char*>& choiceNames()
{
    static const std::vector<const char*> names { "Off", "1/4", "Dotted 1/8", "1/8", "1/8 triplet", "1/16", "1/2" };
    return names;
}

/** Length of the chosen note in beats (quarter notes), or 0 for "Off". */
inline double beatsFor (int choice) noexcept
{
    switch (choice)
    {
        case 1:  return 1.0;
        case 2:  return 0.75;
        case 3:  return 0.5;
        case 4:  return 1.0 / 3.0;
        case 5:  return 0.25;
        case 6:  return 2.0;
        default: return 0.0;
    }
}

inline bool isSynced (const Parameter& sync) noexcept   { return beatsFor (sync.getIndex()) > 0.0; }

/** The note's length in milliseconds at `bpm`, folded by octaves into
    [minMs, maxMs]. */
inline float noteMs (double bpm, int choice, float minMs, float maxMs) noexcept
{
    const auto beats = beatsFor (choice);

    if (beats <= 0.0 || ! (bpm > 0.0))
        return minMs;

    auto ms = 60000.0 / bpm * beats;

    while (ms > (double) maxMs && ms * 0.5 >= (double) minMs)
        ms *= 0.5;

    while (ms < (double) minMs && ms * 2.0 <= (double) maxMs)
        ms *= 2.0;

    return (float) juce::jlimit ((double) minMs, (double) maxMs, ms);
}

/** The time a delay should use right now, in milliseconds. */
inline float timeMs (const Parameter& time, const Parameter& sync, double bpm) noexcept
{
    return isSynced (sync) ? noteMs (bpm, sync.getIndex(), time.minValue, time.maxValue) : time.get();
}
} // namespace tempo_sync
