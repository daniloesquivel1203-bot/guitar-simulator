#include "SongPanel.h"

#include "../engine/TabArrangement.h"
#include "../engine/TabFile.h"
#include "Dialogs.h"
#include "Theme.h"

namespace
{
const char* const lastFileKey = "songs.lastFile";
const char* const lastFolderKey = "songs.lastFolder";
const char* const speedKey = "songs.speed";

constexpr int rowHeight = 28;
constexpr int gap = 8;

const int speeds[] = { 50, 60, 70, 75, 80, 90, 100 };

void styleCaption (juce::Label& label)
{
    label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
}

juce::String trackDescription (const tab::Track& t)
{
    if (t.percussion)
        return "drums";

    if (t.hasTab())
        return juce::String ((int) t.tuning.size()) + " strings";

    return "notes only";
}
}

bool SongPanel::isPlaying (AudioEngine& engine, const SongSession& session)
{
    auto& band = engine.getPractice().getJamTrack();
    return session.handedToBand && band.isPlaying()
           && std::abs (band.getLengthBeats() - session.lengthBeats) < 1.0e-6;
}

SongPanel::SongPanel (AudioEngine& engineToUse, SongSession& sessionToUse)
    : TonePanel ("Songs"), engine (engineToUse), session (sessionToUse)
{
    openButton.setTooltip ("Open a Guitar Pro 7 or 8 file (.gp) or a MusicXML file (.musicxml, .xml, .mxl).");
    openButton.onClick = [this] { chooseFile(); };
    addAndMakeVisible (openButton);

    titleLabel.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (titleLabel);

    styleCaption (showLabel);
    addAndMakeVisible (showLabel);
    trackChoice.setTooltip ("Which part's tab to show.");
    trackChoice.onChange = [this]
    {
        const int index = trackChoice.getSelectedId() - 1;

        if (index >= 0 && index < (int) session.song.tracks.size())
        {
            session.shownTrack = index;
            tabView.setTrack (index);
        }
    };
    addAndMakeVisible (trackChoice);

    styleCaption (speedLabel);
    addAndMakeVisible (speedLabel);

    for (int s : speeds)
        speedChoice.addItem (juce::String (s) + "%", s);

    const int savedSpeed = engine.getSettings().getIntValue (speedKey, 100);
    session.speed = savedSpeed / 100.0;
    speedChoice.setSelectedId (savedSpeed, juce::dontSendNotification);

    if (speedChoice.getSelectedId() == 0)
    {
        speedChoice.setSelectedId (100, juce::dontSendNotification);
        session.speed = 1.0;
    }

    speedChoice.setTooltip ("Slow the song down to practise it. The pitch stays the same.");
    speedChoice.onChange = [this]
    {
        session.speed = speedChoice.getSelectedId() / 100.0;
        engine.getSettings().setValue (speedKey, speedChoice.getSelectedId());

        if (isPlaying (engine, session))
            sendToBand (false);
    };
    addAndMakeVisible (speedChoice);

    playButton.setTooltip ("Play the song from the start, after a bar of count-in.");
    playButton.onClick = [this] { togglePlay(); };
    addAndMakeVisible (playButton);

    statusLabel.setFont (juce::FontOptions (13.0f));
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    addAndMakeVisible (statusLabel);

    styleCaption (bandLabel);
    addAndMakeVisible (bandLabel);

    addAndMakeVisible (tabView);

    if (! session.isLoaded())
    {
        const juce::File last (engine.getSettings().getValue (lastFileKey));

        if (last.existsAsFile())
            load (last, true);
    }

    refreshSongControls();

    vblank = std::make_unique<juce::VBlankAttachment> (this, [this]
    {
        if (bandHasSong())
            tabView.setPosition (engine.getPractice().getJamTrack().getPositionBeats());
    });

    startTimerHz (10);
}

SongPanel::~SongPanel()
{
    vblank.reset();
    tabView.setSong (nullptr, nullptr, 0);
}

bool SongPanel::bandHasSong() const
{
    auto& band = engine.getPractice().getJamTrack();
    return session.handedToBand && std::abs (band.getLengthBeats() - session.lengthBeats) < 1.0e-6;
}

void SongPanel::chooseFile()
{
    juce::File folder (engine.getSettings().getValue (lastFolderKey));

    if (! folder.isDirectory())
        folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    chooser = std::make_unique<juce::FileChooser> ("Open a song", folder, tab::supportedWildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();

                              if (file.existsAsFile())
                                  load (file, false);
                          });
}

void SongPanel::load (const juce::File& file, bool quiet)
{
    auto result = tab::loadFile (file);

    if (! result.ok())
    {
        if (! quiet)
            dialogs::showMessage ("Couldn't open the song", result.error);

        return;
    }

    if (isPlaying (engine, session))
        engine.getPractice().getJamTrack().stop();

    tabView.setSong (nullptr, nullptr, 0);

    session.song = std::move (result.song);
    session.order = tab::playOrder (session.song);
    session.file = file;
    session.shownTrack = session.song.firstTabTrack();
    session.trackOn.assign (session.song.tracks.size(), true);
    session.handedToBand = false;
    session.lengthBeats = tab::totalBeats (session.song, session.order);

    engine.getSettings().setValue (lastFileKey, file.getFullPathName());
    engine.getSettings().setValue (lastFolderKey, file.getParentDirectory().getFullPathName());

    refreshSongControls();
}

void SongPanel::refreshSongControls()
{
    const bool loaded = session.isLoaded();

    if (loaded)
    {
        auto heading = session.song.title;

        if (session.song.artist.isNotEmpty())
            heading << "  -  " << session.song.artist;

        titleLabel.setText (heading, juce::dontSendNotification);
        tabView.setSong (&session.song, &session.order, session.shownTrack);

        if (bandHasSong())
            tabView.setPosition (engine.getPractice().getJamTrack().getPositionBeats());
    }
    else
    {
        titleLabel.setText ("No song loaded", juce::dontSendNotification);
        tabView.setSong (nullptr, nullptr, 0);
    }

    trackChoice.clear (juce::dontSendNotification);

    for (size_t i = 0; i < session.song.tracks.size(); ++i)
    {
        const auto& t = session.song.tracks[i];
        trackChoice.addItem (t.name + "  (" + trackDescription (t) + ")", (int) i + 1);
        trackChoice.setItemEnabled ((int) i + 1, t.hasTab());
    }

    trackChoice.setSelectedId (session.shownTrack + 1, juce::dontSendNotification);
    trackChoice.setEnabled (loaded);
    playButton.setEnabled (loaded);

    rebuildTrackToggles();
    timerCallback();
    resized();
}

void SongPanel::rebuildTrackToggles()
{
    trackToggles.clear();
    toggleTrackIndex.clear();

    for (size_t i = 0; i < session.song.tracks.size(); ++i)
    {
        const auto& t = session.song.tracks[i];
        auto* toggle = trackToggles.add (new juce::ToggleButton (t.name));
        toggle->setToggleState (i >= session.trackOn.size() || session.trackOn[i], juce::dontSendNotification);
        toggle->setTooltip ("Play this part with the built-in "
                            + jam::instrumentName (tab::instrumentFor (t)).toLowerCase()
                            + ". Switch off the part you are playing yourself.");
        toggle->onClick = [this, i, toggle]
        {
            if (i < session.trackOn.size())
                session.trackOn[i] = toggle->getToggleState();

            if (isPlaying (engine, session))
                sendToBand (false);
        };

        addAndMakeVisible (toggle);
        toggleTrackIndex.push_back ((int) i);
    }

    bandLabel.setVisible (! trackToggles.isEmpty());
}

void SongPanel::sendToBand (bool restart)
{
    tab::ArrangeOptions options;
    options.trackOn = session.trackOn;
    options.speed = session.speed;

    auto& band = engine.getPractice().getJamTrack();
    band.setArrangement (tab::arrangeSong (session.song, session.order, options));
    session.handedToBand = true;

    if (restart)
        band.start (true);
}

void SongPanel::togglePlay()
{
    if (! session.isLoaded())
        return;

    if (isPlaying (engine, session))
        engine.getPractice().getJamTrack().stop();
    else
        sendToBand (true);

    timerCallback();
}

void SongPanel::timerCallback()
{
    engine.getPractice().getJamTrack().collectGarbage();

    const bool playing = isPlaying (engine, session);
    playButton.setButtonText (playing ? "Stop" : "Play");
    playButton.setColour (juce::TextButton::buttonColourId, playing ? Theme::danger : Theme::good.darker (0.3f));

    if (! session.isLoaded())
    {
        statusLabel.setText ("Supports Guitar Pro 7 and 8 (.gp) and MusicXML. Older Guitar Pro files can be "
                             "saved as .gp or MusicXML from Guitar Pro, TuxGuitar or MuseScore.",
                             juce::dontSendNotification);
        return;
    }

    const double beat = tabView.getPosition();
    const auto map = tab::tempoMap (session.song, session.order);
    double bpm = map.front().bpm;

    for (const auto& p : map)
        if (p.beat <= beat)
            bpm = p.bpm;

    int barNumber = 0;

    for (const auto& b : session.order)
        if (b.startBeat <= beat)
            barNumber = b.masterBar + 1;

    juce::String status;

    if (beat < 0.0)
        status << "Count-in...   ";
    else
        status << "Bar " << barNumber << " of " << (int) session.song.masterBars.size() << "   ";

    status << juce::String (juce::roundToInt (bpm * session.speed)) << " BPM";

    if (session.speed < 0.999)
        status << " (" << juce::roundToInt (bpm) << " at full speed)";

    statusLabel.setText (status, juce::dontSendNotification);
}

void SongPanel::paint (juce::Graphics& g)
{
    TonePanel::paint (g);
}

void SongPanel::resized()
{
    auto area = getContentArea();

    auto top = area.removeFromTop (rowHeight);
    openButton.setBounds (top.removeFromLeft (130));
    top.removeFromLeft (gap * 2);
    playButton.setBounds (top.removeFromRight (90));
    top.removeFromRight (gap * 2);
    speedChoice.setBounds (top.removeFromRight (80));
    speedLabel.setBounds (top.removeFromRight (48));
    top.removeFromRight (gap);
    trackChoice.setBounds (top.removeFromRight (240));
    showLabel.setBounds (top.removeFromRight (44));
    top.removeFromRight (gap);
    titleLabel.setBounds (top);

    area.removeFromTop (gap);
    statusLabel.setBounds (area.removeFromTop (22));
    area.removeFromTop (gap / 2);

    // Track switches along the bottom, wrapping onto a second row if needed.
    const int toggleWidth = 170;
    const int perRow = juce::jmax (1, (area.getWidth() - 100) / toggleWidth);
    const int rows = trackToggles.isEmpty() ? 0 : (trackToggles.size() + perRow - 1) / perRow;
    auto bottom = area.removeFromBottom (rows * (rowHeight + 2));

    if (rows > 0)
        area.removeFromBottom (gap);

    auto labelColumn = bottom.removeFromLeft (100);
    bandLabel.setBounds (labelColumn.removeFromTop (rowHeight));

    for (int i = 0; i < trackToggles.size(); ++i)
    {
        const int row = i / perRow, col = i % perRow;
        trackToggles[i]->setBounds (bottom.getX() + col * toggleWidth, bottom.getY() + row * (rowHeight + 2),
                                    toggleWidth - gap, rowHeight);
    }

    tabView.setBounds (area);
}
