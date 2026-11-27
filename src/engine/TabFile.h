#pragma once

#include "TabSong.h"

/** Reads tab files into a tab::Song.

    Supported:
    - Guitar Pro 7 and 8 (.gp): a zip holding Content/score.gpif, an XML file.
    - MusicXML (.musicxml, .xml) and compressed MusicXML (.mxl). Parts written
      as tab keep their strings and frets; parts written only as notes get a
      string and fret picked for each note.

    Not yet: Guitar Pro 3 to 6 (.gp3, .gp4, .gp5, .gpx), which use older
    binary formats.
*/
namespace tab
{
    struct LoadResult
    {
        Song song;
        juce::String error;   // empty on success

        bool ok() const noexcept   { return error.isEmpty(); }
    };

    /** Reads any supported file, choosing the reader from its contents. */
    LoadResult loadFile (const juce::File&);

    /** The wildcard for a file chooser, e.g. "*.gp;*.musicxml;*.xml;*.mxl". */
    juce::String supportedWildcard();

    /** Reads the text of a score.gpif file. */
    LoadResult parseGpif (const juce::String& xml);

    /** Reads the text of an uncompressed MusicXML file (score-partwise). */
    LoadResult parseMusicXml (const juce::String& xml);
}
