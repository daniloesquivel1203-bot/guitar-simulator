#include "TabView.h"

#include "Theme.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr int labelWidth = 34;
constexpr float topMargin = 46.0f;    // room for bar numbers and section names
constexpr float stemLength = 18.0f;
constexpr float stemGap = 10.0f;

constexpr double eps = 1.0e-6;

bool isBeamable (const tab::Beat& b)   { return ! b.notes.empty() && b.length <= 0.5 + eps; }

/** Eighths and shorter are beamed together within each quarter-note beat. */
bool beamedTogether (const tab::Beat& a, const tab::Beat& b)
{
    return isBeamable (a) && isBeamable (b)
           && (int) std::floor (a.position + eps) == (int) std::floor (b.position + eps);
}
}

TabView::TabView()
{
    setOpaque (true);
}

void TabView::setSong (const tab::Song* newSong, const std::vector<tab::PlayedBar>* newOrder, int trackIndex)
{
    song = newSong;
    order = newOrder;
    track = trackIndex;
    position = 0.0;
    chooseScale();
    repaint();
}

void TabView::setTrack (int trackIndex)
{
    track = trackIndex;
    chooseScale();
    repaint();
}

void TabView::setPosition (double beats)
{
    if (std::abs (beats - position) > 1.0e-6)
    {
        position = beats;
        repaint();
    }
}

void TabView::setZoom (float newZoom)
{
    zoom = juce::jlimit (0.4f, 3.0f, newZoom);
    repaint();
}

void TabView::chooseScale()
{
    pixelsPerBeat = 80.0f;

    if (song == nullptr || track < 0 || track >= (int) song->tracks.size())
        return;

    // Size the bars so the busiest common passages stay readable: the shortest
    // tenth of the notes get about 22 pixels each.
    std::vector<double> lengths;

    for (const auto& bar : song->tracks[(size_t) track].bars)
        for (const auto& beat : bar.beats)
            if (! beat.notes.empty())
                lengths.push_back (beat.length);

    if (lengths.empty())
        return;

    const auto tenth = lengths.begin() + (std::ptrdiff_t) (lengths.size() / 10);
    std::nth_element (lengths.begin(), tenth, lengths.end());

    pixelsPerBeat = juce::jlimit (60.0f, 200.0f, (float) (22.0 / juce::jmax (0.0625, *tenth)));
}

void TabView::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.fillAll (Theme::sunken);

    if (song == nullptr || order == nullptr || order->empty() || track < 0 || track >= (int) song->tracks.size())
    {
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (juce::FontOptions (15.0f));
        g.drawFittedText ("Open a Guitar Pro (.gp) or MusicXML file to see its tab here.",
                          getLocalBounds().reduced (20), juce::Justification::centred, 2);
        return;
    }

    const auto& t = song->tracks[(size_t) track];
    const int numStrings = t.hasTab() ? (int) t.tuning.size() : 1;
    const float ppb = pixelsPerBeat * zoom;
    const float playheadX = labelWidth + (bounds.getWidth() - labelWidth) * 0.25f;
    // Spread the strings out when there is room, and centre the staff.
    const float stringGap = juce::jlimit (18.0f, 26.0f, (bounds.getHeight() - 110.0f) / (float) juce::jmax (1, numStrings - 1));
    const float blockHeight = topMargin + stringGap * (float) (numStrings - 1) + stemGap + stemLength;
    const float staffTop = juce::jmax (topMargin, (bounds.getHeight() - blockHeight) * 0.5f + topMargin);
    const float staffBottom = staffTop + stringGap * (float) (numStrings - 1);

    auto xForBeat = [&] (double beat) { return playheadX + (float) ((beat - position) * ppb); };
    auto yForString = [&] (int string) { return staffTop + stringGap * (float) (numStrings - 1 - string); };

    const double firstVisible = position - (playheadX - labelWidth) / ppb;
    const double lastVisible = position + (bounds.getWidth() - playheadX) / ppb;

    // Strings
    g.setColour (juce::Colours::white.withAlpha (0.28f));

    for (int s = 0; s < numStrings; ++s)
        g.drawHorizontalLine (juce::roundToInt (yForString (s)), (float) labelWidth, bounds.getRight());

    // Bars that are on screen
    auto firstBar = std::upper_bound (order->begin(), order->end(), firstVisible,
                                      [] (double beat, const tab::PlayedBar& b) { return beat < b.startBeat; });

    if (firstBar != order->begin())
        --firstBar;

    const juce::FontOptions fretFont (14.0f, juce::Font::bold);
    const juce::FontOptions smallFont (11.0f);
    int lastMasterBar = -1;

    for (auto it = firstBar; it != order->end() && it->startBeat <= lastVisible; ++it)
    {
        const auto& mb = song->masterBars[(size_t) it->masterBar];
        const float barX = xForBeat (it->startBeat);

        // Bar line, number and section name
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.drawVerticalLine (juce::roundToInt (barX), staffTop, staffBottom + 1.0f);

        g.setFont (smallFont);
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.drawText (juce::String (it->masterBar + 1), juce::Rectangle<float> (barX + 3.0f, staffTop - 18.0f, 40.0f, 14.0f),
                    juce::Justification::centredLeft);

        if (mb.section.isNotEmpty() && it->masterBar != lastMasterBar)
        {
            g.setColour (Theme::warning);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (mb.section, juce::Rectangle<float> (barX + 3.0f, staffTop - 40.0f, 200.0f, 18.0f),
                        juce::Justification::centredLeft);
        }

        lastMasterBar = it->masterBar;

        if (mb.repeatStart)
        {
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.fillRect (barX + 2.0f, staffTop, 2.0f, staffBottom - staffTop);
        }

        if ((size_t) it->masterBar >= t.bars.size())
            continue;

        const auto& barBeats = t.bars[(size_t) it->masterBar].beats;

        for (size_t bi = 0; bi < barBeats.size(); ++bi)
        {
            const auto& beat = barBeats[bi];
            const double beatStart = it->startBeat + beat.position;
            const float x = xForBeat (beatStart);

            if (x < labelWidth - 20.0f || x > bounds.getRight() + 20.0f || beat.notes.empty())
                continue;

            const bool now = position >= beatStart && position < beatStart + beat.length;
            const bool past = position >= beatStart + beat.length;
            const auto ink = now ? Theme::accent.brighter (0.4f)
                                 : juce::Colours::white.withAlpha (past ? 0.45f : 0.95f);

            if (now)
            {
                g.setColour (Theme::accent.withAlpha (0.18f));
                g.fillRect (juce::Rectangle<float> (x - 12.0f, staffTop - 10.0f, 24.0f, staffBottom - staffTop + 20.0f));
            }

            bool palmMuted = false;

            for (const auto& note : beat.notes)
            {
                palmMuted = palmMuted || note.palmMuted;

                if (! t.hasTab() || note.string < 0)
                    continue;

                juce::String text = note.dead ? juce::String ("x") : juce::String (note.fret);

                if (note.tiedFromPrevious)
                    text = "(" + text + ")";

                g.setFont (fretFont);
                const float w = juce::GlyphArrangement::getStringWidth (juce::Font (fretFont), text) + 4.0f;
                const auto box = juce::Rectangle<float> (w, 15.0f).withCentre ({ x, yForString (note.string) });

                g.setColour (Theme::sunken);
                g.fillRect (box);
                g.setColour (ink);
                g.drawText (text, box, juce::Justification::centred, false);
            }

            if (palmMuted)
            {
                g.setFont (smallFont);
                g.setColour (juce::Colours::white.withAlpha (0.55f));
                g.drawText ("PM", juce::Rectangle<float> (x - 12.0f, staffTop - 32.0f, 24.0f, 12.0f),
                            juce::Justification::centred);
            }

            // Rhythm stem
            if (beat.length < 4.0 - eps)
            {
                const float stemTop = staffBottom + stemGap;
                const float stemEnd = stemTop + stemLength;
                g.setColour (ink.withMultipliedAlpha (0.8f));
                g.drawLine (x, stemTop, x, stemEnd, beat.length >= 2.0 - eps ? 1.0f : 1.6f);

                const bool toNext = bi + 1 < barBeats.size() && beamedTogether (beat, barBeats[bi + 1]);
                const bool fromPrevious = bi > 0 && beamedTogether (barBeats[bi - 1], beat);
                const int beams = beat.length <= 0.25 + eps ? 2 : beat.length <= 0.5 + eps ? 1 : 0;

                if (toNext)
                {
                    const auto& next = barBeats[bi + 1];
                    const float nextX = xForBeat (it->startBeat + next.position);
                    const int shared = juce::jmin (beams, next.length <= 0.25 + eps ? 2 : 1);

                    for (int b = 0; b < shared; ++b)
                        g.fillRect (juce::Rectangle<float> (x, stemEnd - 3.0f - (float) b * 5.0f, nextX - x, 3.0f));

                    // A sixteenth next to an eighth gets a short second beam.
                    if (beams > shared && ! fromPrevious)
                        g.fillRect (juce::Rectangle<float> (x, stemEnd - 8.0f, 7.0f, 3.0f));
                }
                else if (fromPrevious)
                {
                    const auto& previous = barBeats[bi - 1];

                    if (beams == 2 && previous.length > 0.25 + eps)
                        g.fillRect (juce::Rectangle<float> (x - 7.0f, stemEnd - 8.0f, 7.0f, 3.0f));
                }
                else
                {
                    for (int f = 0; f < beams; ++f)
                    {
                        const float fy = stemEnd - (float) f * 5.0f;
                        g.drawLine (x, fy, x + 7.0f, fy - 6.0f, 1.6f);
                    }
                }
            }
        }
    }

    // Playhead
    g.setColour (Theme::accent);
    g.fillRect (juce::Rectangle<float> (playheadX - 1.0f, staffTop - 14.0f, 2.0f, staffBottom - staffTop + 28.0f + stemLength));

    // String names, over anything scrolled underneath them
    g.setColour (Theme::sunken);
    g.fillRect (0.0f, 0.0f, (float) labelWidth, bounds.getHeight());

    if (t.hasTab())
    {
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.setColour (juce::Colours::white.withAlpha (0.65f));

        for (int s = 0; s < numStrings; ++s)
            g.drawText (tab::noteName (t.tuning[(size_t) s] + t.capo),
                        juce::Rectangle<float> (4.0f, yForString (s) - 8.0f, (float) labelWidth - 10.0f, 16.0f),
                        juce::Justification::centredRight);
    }

    if (t.capo > 0)
    {
        g.setFont (smallFont);
        g.setColour (Theme::warning);
        g.drawText ("Capo " + juce::String (t.capo), juce::Rectangle<float> (4.0f, 4.0f, 120.0f, 14.0f),
                    juce::Justification::centredLeft);
    }
}
