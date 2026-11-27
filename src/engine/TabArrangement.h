#pragma once

#include "JamArranger.h"
#include "TabSong.h"

/** Turns a tab song into notes for the jam-track band, so a loaded song can be
    played with the built-in instruments. */
namespace tab
{
    /** Which band instrument plays a track, from its General MIDI program. */
    jam::Instrument instrumentFor (const Track&);

    /** The band's drum for a General MIDI percussion note. */
    jam::Drum drumFor (int midiNote);

    struct ArrangeOptions
    {
        std::vector<bool> trackOn;   // one per track; missing entries count as on
        double speed = 1.0;          // 0.5 = half speed
    };

    /** The whole song, played once, with its tempo changes. */
    jam::Arrangement arrangeSong (const Song&, const std::vector<PlayedBar>&, const ArrangeOptions&);
}
