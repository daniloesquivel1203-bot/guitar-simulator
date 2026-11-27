#include "JamPanel.h"

#include "PracticePanel.h"
#include "Theme.h"

using namespace jam;

namespace
{
const char* const styleKey = "jam.style";
const char* const rootKey = "jam.keyRoot";
const char* const modeKey = "jam.mode";
const char* const progressionKey = "jam.progression";
const char* const countInKey = "jam.countIn";
const char* const masterKey = "jam.masterDb";

juce::String levelKey (int part)   { return "jam.levelDb." + juce::String (part); }
juce::String onKey (int part)      { return "jam.on." + juce::String (part); }

constexpr int rowHeight = 28;
constexpr int bigRowHeight = 40;
constexpr int gap = 8;
constexpr int beatsPerRow = 16;

const std::array<juce::String, numInstruments> partTips {
    "Drums: the beat for the style, with a fill at the end of the loop.",
    "Bass: follows the root of each chord. Jazz walks a line through the changes.",
    "Rhythm guitar: strums, chugs or picks the chords, depending on the style.",
    "Piano: an electric piano playing the chords.",
    "Organ: a held organ chord under the band. Off to start with.",
};

void styleCaption (juce::Label& label)
{
    label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
}

void styleNoteLabel (juce::Label& label)
{
    label.setFont (juce::FontOptions (13.0f));
    label.setJustificationType (juce::Justification::topLeft);
    label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
}

void styleLevelSlider (juce::Slider& slider, double value, double resetValue)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
    slider.setRange (JamTrack::minLevelDb, JamTrack::maxLevelDb, 0.5);
    slider.setTextValueSuffix (" dB");
    slider.setDoubleClickReturnValue (true, resetValue);
    slider.setValue (value, juce::dontSendNotification);
}

int qualityIndex (Quality q)
{
    return (int) q;
}

constexpr std::array<Quality, 9> allQualities { Quality::major, Quality::minor, Quality::dominant7, Quality::major7,
                                                Quality::minor7, Quality::sus4, Quality::power, Quality::diminished,
                                                Quality::halfDiminished7 };

}

//==============================================================================
void JamPanel::applySettings (AudioEngine& engine)
{
    auto& settings = engine.getSettings();
    auto& track = engine.getPractice().getJamTrack();

    for (int i = 0; i < numInstruments; ++i)
    {
        const auto instrument = (Instrument) i;

        if (settings.containsKey (levelKey (i)))
            track.setLevelDb (instrument, (float) settings.getDoubleValue (levelKey (i)));

        if (settings.containsKey (onKey (i)))
            track.setOn (instrument, settings.getBoolValue (onKey (i)));
    }

    if (settings.containsKey (masterKey))
        track.setMasterDb ((float) settings.getDoubleValue (masterKey));

    // The band follows the metronome's tempo, so bring back the last one used.
    if (settings.containsKey ("practice.bpm"))
        engine.getPractice().getMetronome().setBpm (settings.getDoubleValue ("practice.bpm"));
}

JamPanel::JamPanel (AudioEngine& engineToUse)
    : TonePanel ("JAM TRACKS"), engine (engineToUse), track (engine.getPractice().getJamTrack())
{
    loadSettings();
    auto& settings = engine.getSettings();

    // Setup
    playButton.setClickingTogglesState (false);
    playButton.setColour (juce::TextButton::buttonOnColourId, Theme::good.darker (0.3f));
    playButton.setTooltip ("Starts or stops the band. It keeps playing while you use the rest of the app.");
    playButton.onClick = [this]
    {
        if (track.isPlaying())
        {
            track.stop();
        }
        else
        {
            rebuild();
            track.start (countIn.getToggleState());
        }

        refreshTransport();
    };
    addAndMakeVisible (playButton);

    countIn.setToggleState (settings.getBoolValue (countInKey, true), juce::dontSendNotification);
    countIn.setTooltip ("Clicks four beats before the band comes in, so you can get ready.");
    countIn.onClick = [this] { engine.getSettings().setValue (countInKey, countIn.getToggleState()); };
    addAndMakeVisible (countIn);

    bpm.setSliderStyle (juce::Slider::LinearHorizontal);
    bpm.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 22);
    bpm.setRange (Metronome::minBpm, Metronome::maxBpm, 1.0);
    bpm.setTextValueSuffix (" BPM");
    bpm.setValue (engine.getPractice().getMetronome().getBpm(), juce::dontSendNotification);
    bpm.setTooltip ("Tempo in beats per minute, shared with the metronome and synced delays. "
                    "Drag, or click the number to type one.");
    bpm.onValueChange = [this] { PracticePanel::setTempo (engine, bpm.getValue()); };
    addAndMakeVisible (bpm);

    tapButton.setTooltip ("Tap along with the beat, four times or more, to set the tempo.");
    tapButton.onClick = [this]
    {
        if (const auto tapped = tapTempo.tap (juce::Time::getMillisecondCounterHiRes() / 1000.0))
            bpm.setValue (std::round (*tapped), juce::sendNotificationSync);
    };
    addAndMakeVisible (tapButton);

    for (auto* label : { &styleLabel, &keyLabel, &progressionLabel })
    {
        styleCaption (*label);
        addAndMakeVisible (*label);
    }

    const auto& styles = getStyles();

    for (int i = 0; i < (int) styles.size(); ++i)
        styleBox.addItem (styles[(size_t) i].name, i + 1);

    styleBox.setTooltip ("How the band plays: the drum beat, bass line and the way the chords are played. "
                         "Picking a style also picks a typical tempo and progression for it, unless you have edited the chords.");
    styleBox.onChange = [this] { chooseStyle (styleBox.getSelectedId() - 1); };
    addAndMakeVisible (styleBox);

    styleNoteLabel (styleNote);
    addAndMakeVisible (styleNote);

    for (int pc = 0; pc < 12; ++pc)
        rootBox.addItem (noteName (pc, false) + (noteName (pc, false) != noteName (pc, true) ? "/" + noteName (pc, true) : juce::String()), pc + 1);

    rootBox.setTooltip ("The key's home note. The chords move with it, so the progression keeps its shape.");
    rootBox.onChange = [this]
    {
        key.root = rootBox.getSelectedId() - 1;
        engine.getSettings().setValue (rootKey, key.root);
        rebuild();
    };
    addAndMakeVisible (rootBox);

    modeBox.addItem ("Major", 1);
    modeBox.addItem ("Minor", 2);
    modeBox.setTooltip ("Major sounds bright, minor sounds darker. Changes the chords that fit the key.");
    modeBox.onChange = [this] { chooseMode (modeBox.getSelectedId() == 2 ? Mode::minor : Mode::major); };
    addAndMakeVisible (modeBox);

    progressionBox.setTooltip ("Common chord progressions for the key. Click a chord in the chart to change it.");
    progressionBox.onChange = [this]
    {
        const auto id = progressionBox.getSelectedId();

        if (id > 0 && id <= (int) presetIndexes.size())
            choosePreset (presetIndexes[(size_t) id - 1]);
    };
    addAndMakeVisible (progressionBox);

    // Chords
    chart.setTooltip ("Click a chord to change it, set its length, add one after it or remove it.");
    addAndMakeVisible (chart);

    nowPlaying.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    nowPlaying.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (nowPlaying);

    styleNoteLabel (scales);
    addAndMakeVisible (scales);

    // Mix
    for (int i = 0; i < numInstruments; ++i)
    {
        const auto instrument = (Instrument) i;
        auto& button = partButtons[(size_t) i];
        auto& level = partLevels[(size_t) i];

        button.setButtonText (instrumentName (instrument));
        button.setToggleState (track.isOn (instrument), juce::dontSendNotification);
        button.setTooltip (partTips[(size_t) i] + " Click to switch it on or off.");
        button.onClick = [this, i, instrument]
        {
            const auto on = partButtons[(size_t) i].getToggleState();
            track.setOn (instrument, on);
            engine.getSettings().setValue (onKey (i), on);
            partLevels[(size_t) i].setEnabled (on);
        };
        addAndMakeVisible (button);

        styleLevelSlider (level, track.getLevelDb (instrument), -4.0);
        level.setEnabled (track.isOn (instrument));
        level.setTooltip ("How loud the " + instrumentName (instrument).toLowerCase() + " is. Double-click to reset.");
        level.onValueChange = [this, i, instrument]
        {
            track.setLevelDb (instrument, (float) partLevels[(size_t) i].getValue());
            engine.getSettings().setValue (levelKey (i), partLevels[(size_t) i].getValue());
        };
        addAndMakeVisible (level);
    }

    styleCaption (masterLabel);
    addAndMakeVisible (masterLabel);
    styleLevelSlider (master, track.getMasterDb(), -6.0);
    master.setTooltip ("How loud the whole band is against your guitar. Double-click to reset.");
    master.onValueChange = [this]
    {
        track.setMasterDb ((float) master.getValue());
        engine.getSettings().setValue (masterKey, master.getValue());
    };
    addAndMakeVisible (master);

    styleNoteLabel (mixNote);
    mixNote.setText ("The band is heard and goes into recorded takes, but not into the looper.",
                     juce::dontSendNotification);
    addAndMakeVisible (mixNote);

    styleBox.setSelectedId ((int) style + 1, juce::dontSendNotification);
    rootBox.setSelectedId (key.root + 1, juce::dontSendNotification);
    modeBox.setSelectedId (key.mode == Mode::minor ? 2 : 1, juce::dontSendNotification);

    rebuild();
    refreshTransport();
    startTimerHz (30);
}

JamPanel::~JamPanel() = default;

void JamPanel::loadSettings()
{
    auto& settings = engine.getSettings();
    style = (Style) juce::jlimit (0, numStyles - 1, settings.getIntValue (styleKey, (int) Style::blues));
    const auto& info = getStyleInfo (style);

    key.root = juce::jlimit (0, 11, settings.getIntValue (rootKey, 9));
    key.mode = settings.getIntValue (modeKey, (int) info.suggestedMode) == (int) Mode::minor ? Mode::minor : Mode::major;
    progression = progressionFromString (settings.getValue (progressionKey));

    if (progression.empty())
    {
        for (const auto& p : getProgressionPresets())
            if (p.name == info.suggestedProgression)
                progression = p.slots;

        key.mode = info.suggestedMode;
    }
}

void JamPanel::saveProgression()
{
    auto& settings = engine.getSettings();
    settings.setValue (progressionKey, progressionToString (progression));
    settings.setValue (modeKey, (int) key.mode);
    settings.setValue (styleKey, (int) style);
}

void JamPanel::rebuild()
{
    track.setArrangement (arrange (progression, key, style));
    saveProgression();
    refreshProgressionBox();
    refreshInfo();
    chart.repaint();
}

//==============================================================================
void JamPanel::chooseStyle (int index)
{
    if (! juce::isPositiveAndBelow (index, numStyles))
        return;

    // A stock progression follows the style; one you have edited stays.
    bool stock = false;

    for (const auto& p : getProgressionPresets())
        stock = stock || (p.slots == progression && p.mode == key.mode);

    style = (Style) index;
    const auto& info = getStyleInfo (style);

    if (stock)
    {
        for (const auto& p : getProgressionPresets())
            if (p.name == info.suggestedProgression)
                progression = p.slots;

        key.mode = info.suggestedMode;
        modeBox.setSelectedId (key.mode == Mode::minor ? 2 : 1, juce::dontSendNotification);
    }

    bpm.setValue (info.suggestedBpm, juce::sendNotificationSync);
    rebuild();
}

void JamPanel::chooseMode (Mode mode)
{
    if (mode == key.mode)
        return;

    // A stock progression is swapped for one written for the new mode.
    const auto& presets = getProgressionPresets();
    bool stock = false;

    for (const auto& p : presets)
        stock = stock || (p.slots == progression && p.mode == key.mode);

    key.mode = mode;

    if (stock)
    {
        const auto& info = getStyleInfo (style);
        const ProgressionPreset* replacement = nullptr;

        for (const auto& p : presets)
            if (p.mode == mode && (replacement == nullptr || (info.suggestedMode == mode && p.name == info.suggestedProgression)))
                replacement = &p;

        if (replacement != nullptr)
            progression = replacement->slots;
    }

    rebuild();
}

void JamPanel::choosePreset (int index)
{
    const auto& presets = getProgressionPresets();

    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return;

    progression = presets[(size_t) index].slots;
    key.mode = presets[(size_t) index].mode;
    modeBox.setSelectedId (key.mode == Mode::minor ? 2 : 1, juce::dontSendNotification);
    rebuild();
}

void JamPanel::refreshProgressionBox()
{
    progressionBox.clear (juce::dontSendNotification);
    presetIndexes.clear();

    const auto& presets = getProgressionPresets();
    int selected = 0;

    for (int i = 0; i < (int) presets.size(); ++i)
    {
        if (presets[(size_t) i].mode != key.mode)
            continue;

        presetIndexes.push_back (i);
        progressionBox.addItem (presets[(size_t) i].name, (int) presetIndexes.size());

        if (presets[(size_t) i].slots == progression)
            selected = (int) presetIndexes.size();
    }

    if (selected > 0)
        progressionBox.setSelectedId (selected, juce::dontSendNotification);
    else
        progressionBox.setText ("Your own chords", juce::dontSendNotification);
}

//==============================================================================
void JamPanel::showChordMenu (int slot, juce::Rectangle<int> screenArea)
{
    if (! juce::isPositiveAndBelow (slot, (int) progression.size()))
        return;

    const auto current = progression[(size_t) slot];
    const auto choices = chordsForKey (key.mode);

    juce::PopupMenu menu;
    juce::String group;

    for (int i = 0; i < (int) choices.size(); ++i)
    {
        const auto& choice = choices[(size_t) i];

        if (choice.group != group)
        {
            group = choice.group;
            menu.addSectionHeader (group == "In the key" ? "Chords in " + keyName (key) : group);
        }

        menu.addItem (1000 + i, chordName (choice.chord, key) + "    " + romanNumeral (choice.chord, key.mode),
                      true, choice.chord == current.chord);
    }

    juce::PopupMenu anyChord;

    for (int pc = 0; pc < 12; ++pc)
    {
        juce::PopupMenu forRoot;
        const auto degree = ((pc - key.root) % 12 + 12) % 12;

        for (auto q : allQualities)
        {
            const Chord chord { degree, q };
            forRoot.addItem (2000 + degree * 16 + qualityIndex (q),
                             chordName (chord, key) + "  (" + qualityName (q) + ")", true, chord == current.chord);
        }

        anyChord.addSubMenu (noteName (pc, keyUsesFlats (key)), forRoot);
    }

    menu.addSeparator();
    menu.addSubMenu ("Any chord", anyChord);

    juce::PopupMenu length;
    const auto room = maxProgressionBeats - totalBeats (progression) + current.beats;

    for (auto [beats, name] : { std::pair { 2, "Half a bar" }, std::pair { 4, "1 bar" }, std::pair { 8, "2 bars" }, std::pair { 16, "4 bars" } })
        length.addItem (3000 + beats, name, beats <= room, beats == current.beats);

    menu.addSubMenu ("Length", length);
    menu.addSeparator();
    menu.addItem (4000, "Add a chord after this one", totalBeats (progression) + 4 <= maxProgressionBeats);
    menu.addItem (4001, "Remove this chord", progression.size() > 1);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (screenArea),
                        [safe = juce::Component::SafePointer<JamPanel> (this), slot, choices] (int result)
                        {
                            if (safe == nullptr || result == 0 || ! juce::isPositiveAndBelow (slot, (int) safe->progression.size()))
                                return;

                            auto& p = safe->progression;

                            if (result >= 1000 && result < 2000)
                                p[(size_t) slot].chord = choices[(size_t) (result - 1000)].chord;
                            else if (result >= 2000 && result < 3000)
                                p[(size_t) slot].chord = { (result - 2000) / 16, (Quality) ((result - 2000) % 16) };
                            else if (result >= 3000 && result < 4000)
                                p[(size_t) slot].beats = result - 3000;
                            else if (result == 4000)
                                p.insert (p.begin() + slot + 1, Slot { p[(size_t) slot].chord, 4 });
                            else if (result == 4001 && p.size() > 1)
                                p.erase (p.begin() + slot);

                            safe->rebuild();
                        });
}

void JamPanel::addChord()
{
    if (totalBeats (progression) + 4 > maxProgressionBeats)
        return;

    progression.push_back ({ { 0, key.mode == Mode::minor ? Quality::minor : Quality::major }, 4 });
    rebuild();

    // Open the menu on the new chord straight away, so it can be picked.
    for (const auto& piece : chart.layoutPieces())
        if (piece.slot == (int) progression.size() - 1)
            showChordMenu (piece.slot, chart.localAreaToGlobal (piece.area.toNearestInt()));
}

//==============================================================================
std::vector<JamPanel::ChordChart::Piece> JamPanel::ChordChart::layoutPieces() const
{
    const auto& progression = owner.progression;
    const auto total = totalBeats (progression) + 4;   // with room for the add box
    const auto rows = juce::jmax (1, (total + beatsPerRow - 1) / beatsPerRow);
    const auto bounds = getLocalBounds().toFloat();
    const auto rowHeightPx = juce::jmin (72.0f, bounds.getHeight() / (float) rows);
    const auto beatWidth = bounds.getWidth() / (float) beatsPerRow;

    std::vector<Piece> pieces;
    int beat = 0;

    const auto place = [&] (int slot, int beats)
    {
        bool first = true;

        while (beats > 0)
        {
            const auto row = beat / beatsPerRow;
            const auto inRow = beat % beatsPerRow;
            const auto take = juce::jmin (beats, beatsPerRow - inRow);
            pieces.push_back ({ slot, { bounds.getX() + inRow * beatWidth, bounds.getY() + row * rowHeightPx,
                                        take * beatWidth, rowHeightPx }, first });
            first = false;
            beat += take;
            beats -= take;
        }
    };

    for (int i = 0; i < (int) progression.size(); ++i)
        place (i, progression[(size_t) i].beats);

    if (totalBeats (progression) + 4 <= maxProgressionBeats)
        place (-1, juce::jmin (4, beatsPerRow - beat % beatsPerRow));

    return pieces;
}

int JamPanel::ChordChart::slotAt (juce::Point<float> position) const
{
    for (const auto& piece : layoutPieces())
        if (piece.area.contains (position))
            return piece.slot;

    return -2;
}

void JamPanel::ChordChart::paint (juce::Graphics& g)
{
    const auto& progression = owner.progression;
    const auto key = owner.key;

    for (const auto& piece : layoutPieces())
    {
        const auto box = piece.area.reduced (3.0f);

        if (piece.slot < 0)
        {
            g.setColour (juce::Colours::white.withAlpha (hoverSlot == -1 ? 0.5f : 0.25f));
            g.drawRoundedRectangle (box, 6.0f, 1.5f);
            g.setFont (juce::FontOptions (22.0f));
            g.drawText ("+", box, juce::Justification::centred);
            continue;
        }

        const auto& slot = progression[(size_t) piece.slot];
        const bool playing = piece.slot == playingSlot;
        // Sevenths are judged by their triad, so a blues I7 or IV7 counts as in the key.
        auto triad = slot.chord;

        if (triad.quality == Quality::dominant7 || triad.quality == Quality::major7)  triad.quality = Quality::major;
        if (triad.quality == Quality::minor7)                                          triad.quality = Quality::minor;
        if (triad.quality == Quality::halfDiminished7)                                 triad.quality = Quality::diminished;

        const bool fits = chordFitsKey (triad, key.mode);

        g.setColour (playing ? Theme::accent.darker (0.2f) : piece.slot == hoverSlot ? Theme::raised.brighter (0.15f) : Theme::raised);
        g.fillRoundedRectangle (box, 6.0f);

        // Chords from outside the key get a coloured edge.
        if (! fits)
        {
            g.setColour (Theme::warning.withAlpha (0.7f));
            g.drawRoundedRectangle (box.reduced (0.5f), 6.0f, 1.5f);
        }

        // Beat ticks along the bottom, and how far through the chord the band is.
        const auto beatWidth = getWidth() / (float) beatsPerRow;
        const auto beatsHere = juce::roundToInt (piece.area.getWidth() / beatWidth);

        for (int b = 0; b < beatsHere; ++b)
        {
            const auto tick = juce::Rectangle<float> (box.getX() + 8.0f + b * beatWidth, box.getBottom() - 8.0f, 4.0f, 4.0f);
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.fillEllipse (tick);
        }

        if (playing && playingBeat >= 0.0)
        {
            const auto rowStart = piece.area.getX() / beatWidth;
            const auto rowOfPiece = (int) (piece.area.getY() / juce::jmax (1.0f, piece.area.getHeight()) + 0.5f);
            const auto beatInRow = playingBeat - rowOfPiece * beatsPerRow;

            if (beatInRow >= rowStart && beatInRow < rowStart + beatsHere)
            {
                const auto x = (float) (piece.area.getX() + (beatInRow - rowStart) * beatWidth);
                g.setColour (juce::Colours::white.withAlpha (0.8f));
                g.fillRect (juce::Rectangle<float> (x + 3.0f, box.getY() + 4.0f, 2.0f, box.getHeight() - 8.0f));
            }
        }

        auto text = box.reduced (10.0f, 6.0f);

        if (! piece.first)
        {
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.setFont (juce::FontOptions (18.0f));
            g.drawText ("%", text, juce::Justification::centredLeft);
            continue;
        }

        const auto big = juce::jlimit (13.0f, 22.0f, box.getHeight() * 0.36f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (big, juce::Font::bold));
        g.drawFittedText (chordName (slot.chord, key), text.removeFromTop (big + 4.0f).toNearestInt(),
                          juce::Justification::topLeft, 1, 0.7f);

        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText (romanNumeral (slot.chord, key.mode), text.toNearestInt(), juce::Justification::topLeft, 1, 0.7f);
    }
}

void JamPanel::ChordChart::mouseDown (const juce::MouseEvent& e)
{
    const auto slot = slotAt (e.position);

    if (slot == -1)
    {
        owner.addChord();
        return;
    }

    for (const auto& piece : layoutPieces())
        if (piece.slot == slot && piece.slot >= 0)
            return owner.showChordMenu (slot, localAreaToGlobal (piece.area.toNearestInt()));
}

void JamPanel::ChordChart::mouseMove (const juce::MouseEvent& e)
{
    const auto slot = slotAt (e.position);

    if (slot != hoverSlot)
    {
        hoverSlot = slot;
        repaint();
    }
}

void JamPanel::ChordChart::mouseExit (const juce::MouseEvent&)
{
    hoverSlot = -2;
    repaint();
}

//==============================================================================
void JamPanel::refreshTransport()
{
    const auto playing = track.isPlaying();
    playButton.setToggleState (playing, juce::dontSendNotification);
    playButton.setButtonText (playing ? "Stop" : "Play");
    playButton.setColour (juce::TextButton::buttonColourId, playing ? Theme::good.darker (0.3f) : Theme::accent.darker (0.3f));
}

void JamPanel::refreshInfo()
{
    const auto& info = getStyleInfo (style);
    styleNote.setText (info.description, juce::dontSendNotification);

    juce::String text = "Scales to play over this:\n";

    for (const auto& scale : scalesForKey (key, info.bluesScales))
        text << scale.name << ":  " << scale.notes << "\n";

    scales.setText (text.trimEnd(), juce::dontSendNotification);
}

void JamPanel::timerCallback()
{
    track.collectGarbage();

    const auto tempo = engine.getPractice().getMetronome().getBpm();

    if (! bpm.isMouseButtonDown() && ! juce::exactlyEqual (bpm.getValue(), tempo))
        bpm.setValue (tempo, juce::dontSendNotification);

    refreshTransport();

    // Which chord the band is on, from its place in the loop.
    const auto playing = track.isPlaying();
    const auto position = track.getPositionBeats();
    int slot = -1;

    if (playing && position >= 0.0)
    {
        int beat = 0;

        for (int i = 0; i < (int) progression.size(); ++i)
        {
            if (position < beat + progression[(size_t) i].beats)
            {
                slot = i;
                break;
            }

            beat += progression[(size_t) i].beats;
        }

        // The loop is padded to whole bars; the padding keeps the last chord.
        if (slot < 0 && ! progression.empty())
            slot = (int) progression.size() - 1;
    }

    chart.playingSlot = slot;
    chart.playingBeat = playing ? position : -1.0;
    chart.repaint();

    juce::String now;

    if (playing && position < 0.0)
    {
        now = "Count-in: " + juce::String ((int) std::floor (position) + JamTrack::countInBeats + 1);
    }
    else if (slot >= 0)
    {
        const auto& chord = progression[(size_t) slot].chord;
        const auto& next = progression[(size_t) ((slot + 1) % (int) progression.size())].chord;
        juce::StringArray notes;

        for (auto pc : chordPitchClasses (chord, key))
            notes.add (noteName (pc, keyUsesFlats (key)));

        now = "Now: " + chordName (chord, key) + "  (" + notes.joinIntoString (" ") + ")      Next: " + chordName (next, key);
    }
    else
    {
        now = keyName (key) + ", " + juce::String (totalBeats (progression) / 4.0, totalBeats (progression) % 4 == 0 ? 0 : 1)
              + " bars. Press Play to start the band.";
    }

    if (nowPlaying.getText() != now)
        nowPlaying.setText (now, juce::dontSendNotification);

    if (slot != shownSlot)
        shownSlot = slot;
}

//==============================================================================
void JamPanel::resized()
{
    auto content = getContentArea();
    const auto sideWidth = (content.getWidth() - 2 * 16) * 26 / 100;

    setupArea = content.removeFromLeft (sideWidth);
    content.removeFromLeft (16);
    mixArea = content.removeFromRight (sideWidth);
    content.removeFromRight (16);
    chartArea = content;

    const auto inner = [] (juce::Rectangle<int> column) { return column.reduced (12).withTrimmedTop (22); };

    // Setup
    {
        auto area = inner (setupArea);
        auto row = area.removeFromTop (bigRowHeight);
        tapButton.setBounds (row.removeFromRight (row.getWidth() / 3));
        row.removeFromRight (gap);
        playButton.setBounds (row);
        area.removeFromTop (gap);
        bpm.setBounds (area.removeFromTop (rowHeight));
        area.removeFromTop (4);
        countIn.setBounds (area.removeFromTop (rowHeight - 4));
        area.removeFromTop (gap);

        // Each choice has its caption above it, so the boxes get the full width.
        const auto labelled = [&] (juce::Label& label, juce::Component& box, juce::Component* second)
        {
            label.setBounds (area.removeFromTop (18));
            auto line = area.removeFromTop (rowHeight - 2);

            if (second != nullptr)
            {
                const auto half = (line.getWidth() - gap) / 2;
                box.setBounds (line.removeFromLeft (half));
                line.removeFromLeft (gap);
                second->setBounds (line);
            }
            else
            {
                box.setBounds (line);
            }

            area.removeFromTop (gap);
        };

        labelled (styleLabel, styleBox, nullptr);
        labelled (keyLabel, rootBox, &modeBox);
        labelled (progressionLabel, progressionBox, nullptr);
        styleNote.setBounds (area);
    }

    // Chords
    {
        auto area = inner (chartArea);
        scales.setBounds (area.removeFromBottom (78));
        area.removeFromBottom (gap);
        nowPlaying.setBounds (area.removeFromBottom (24));
        area.removeFromBottom (gap);
        chart.setBounds (area);
    }

    // Mix
    {
        auto area = inner (mixArea);

        for (int i = 0; i < numInstruments; ++i)
        {
            partButtons[(size_t) i].setBounds (area.removeFromTop (rowHeight - 6));
            partLevels[(size_t) i].setBounds (area.removeFromTop (rowHeight - 4));
            area.removeFromTop (4);
        }

        area.removeFromTop (gap);
        auto row = area.removeFromTop (rowHeight);
        masterLabel.setBounds (row.removeFromLeft (48));
        master.setBounds (row);
        area.removeFromTop (gap);
        mixNote.setBounds (area);
    }
}

void JamPanel::paint (juce::Graphics& g)
{
    TonePanel::paint (g);

    for (auto [column, heading] : { std::pair { setupArea, "BAND" },
                                    std::pair { chartArea, "CHORDS" },
                                    std::pair { mixArea, "MIX" } })
    {
        g.setColour (Theme::sunken.withAlpha (0.6f));
        g.fillRoundedRectangle (column.toFloat(), 6.0f);

        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (heading, column.reduced (12, 8).removeFromTop (16), juce::Justification::centredLeft);
    }
}
