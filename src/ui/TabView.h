#pragma once

#include "../engine/TabSong.h"

#include <juce_gui_basics/juce_gui_basics.h>

/** One track of a song drawn as tab, scrolling past a fixed playhead.

    The strings run left to right with the highest string at the top, as in
    printed tab. The playhead sits a quarter of the way in, so the bars coming
    up are always in view. Below the strings, a short stem shows each note's
    length: open for half notes, filled for quarters, one flag for eighths,
    two for sixteenths.
*/
class TabView final : public juce::Component
{
public:
    TabView();

    /** The song stays owned by the caller and must outlive this view or be
        replaced with nullptr first. */
    void setSong (const tab::Song*, const std::vector<tab::PlayedBar>*, int trackIndex);
    void setTrack (int trackIndex);

    /** Beats from the start of the song; negative during a count-in. */
    void setPosition (double beats);
    double getPosition() const noexcept   { return position; }

    /** Horizontal zoom; 1 is the automatic size. */
    void setZoom (float);

    void paint (juce::Graphics&) override;

    /** Pixels per beat at the current zoom, for tests and layout. */
    float getPixelsPerBeat() const noexcept   { return pixelsPerBeat * zoom; }

private:
    void chooseScale();

    const tab::Song* song = nullptr;
    const std::vector<tab::PlayedBar>* order = nullptr;
    int track = 0;
    double position = 0.0;
    float pixelsPerBeat = 80.0f;
    float zoom = 1.0f;
};
