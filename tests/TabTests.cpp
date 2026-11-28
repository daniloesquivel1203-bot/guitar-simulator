#include "engine/TabArrangement.h"
#include "engine/TabFile.h"
#include "engine/JamTrack.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

namespace
{
/** A small Guitar Pro 7 score: a guitar and a drum kit, three bars.
    Bars 1-2 repeat once; the tempo starts at 120 and drops to 90 (written as a
    dotted-quarter tempo of 60) at the start of bar 3. */
const char* const gpif = R"(<?xml version="1.0" encoding="utf-8"?>
<GPIF>
  <Score><Title><![CDATA[Test Song]]></Title><Artist><![CDATA[Nobody]]></Artist></Score>
  <MasterTrack>
    <Tracks>0 1</Tracks>
    <Automations>
      <Automation><Type>Tempo</Type><Linear>false</Linear><Bar>0</Bar><Position>0</Position><Value>120 2</Value></Automation>
      <Automation><Type>Tempo</Type><Linear>false</Linear><Bar>2</Bar><Position>0</Position><Value>60 3</Value></Automation>
    </Automations>
  </MasterTrack>
  <Tracks>
    <Track id="0">
      <Name><![CDATA[Lead Guitar]]></Name>
      <Staves><Staff><Properties>
        <Property name="Tuning"><Pitches>40 45 50 55 59 64</Pitches></Property>
        <Property name="CapoFret"><Fret>2</Fret></Property>
      </Properties></Staff></Staves>
      <Sounds><Sound><MIDI><Program>29</Program></MIDI></Sound></Sounds>
      <MidiConnection><PrimaryChannel>0</PrimaryChannel></MidiConnection>
    </Track>
    <Track id="1">
      <Name><![CDATA[Drums]]></Name>
      <InstrumentSet><Type>drumKit</Type><Elements>
        <Element><Name>Kick</Name><Articulations><Articulation><OutputMidiNumber>36</OutputMidiNumber></Articulation></Articulations></Element>
        <Element><Name>Snare</Name><Articulations>
          <Articulation><OutputMidiNumber>38</OutputMidiNumber></Articulation>
          <Articulation><OutputMidiNumber>37</OutputMidiNumber></Articulation>
        </Articulations></Element>
      </Elements></InstrumentSet>
      <MidiConnection><PrimaryChannel>9</PrimaryChannel></MidiConnection>
    </Track>
  </Tracks>
  <MasterBars>
    <MasterBar><Time>4/4</Time><Repeat start="true" end="false" count="0"/><Section><Letter>A</Letter><Text>Intro</Text></Section><Bars>0 3</Bars></MasterBar>
    <MasterBar><Time>4/4</Time><Repeat start="false" end="true" count="2"/><Bars>1 3</Bars></MasterBar>
    <MasterBar><Time>3/4</Time><Bars>2 4</Bars></MasterBar>
  </MasterBars>
  <Bars>
    <Bar id="0"><Voices>0 -1 -1 -1</Voices></Bar>
    <Bar id="1"><Voices>1 -1 -1 -1</Voices></Bar>
    <Bar id="2"><Voices>2 -1 -1 -1</Voices></Bar>
    <Bar id="3"><Voices>3 -1 -1 -1</Voices></Bar>
    <Bar id="4"><Voices>4 -1 -1 -1</Voices></Bar>
  </Bars>
  <Voices>
    <Voice id="0"><Beats>0 1 2 3</Beats></Voice>
    <Voice id="1"><Beats>4 5</Beats></Voice>
    <Voice id="2"><Beats>6 7 7 7</Beats></Voice>
    <Voice id="3"><Beats>8 9 8 9</Beats></Voice>
    <Voice id="4"><Beats>8 9 8</Beats></Voice>
  </Voices>
  <Beats>
    <Beat id="0"><Rhythm ref="q"/><Notes>0 1</Notes></Beat>
    <Beat id="1"><Rhythm ref="dotted8"/><Notes>2</Notes></Beat>
    <Beat id="2"><Rhythm ref="16"/><Notes>3</Notes></Beat>
    <Beat id="3"><Rhythm ref="h"/></Beat>
    <Beat id="4"><Rhythm ref="h"/><Notes>4</Notes></Beat>
    <Beat id="5"><Rhythm ref="h"/><Notes>5</Notes></Beat>
    <Beat id="6"><GraceNotes>BeforeBeat</GraceNotes><Rhythm ref="16"/><Notes>2</Notes></Beat>
    <Beat id="7"><Rhythm ref="trip"/><Notes>2</Notes></Beat>
    <Beat id="8"><Rhythm ref="q"/><Notes>10</Notes></Beat>
    <Beat id="9"><Rhythm ref="q"/><Notes>11</Notes></Beat>
  </Beats>
  <Notes>
    <Note id="0"><Properties><Property name="String"><String>0</String></Property><Property name="Fret"><Fret>3</Fret></Property></Properties></Note>
    <Note id="1"><Properties><Property name="String"><String>5</String></Property><Property name="Fret"><Fret>0</Fret></Property></Properties></Note>
    <Note id="2"><Properties><Property name="String"><String>1</String></Property><Property name="Fret"><Fret>5</Fret></Property><Property name="PalmMuted"><Enable/></Property></Properties></Note>
    <Note id="3"><Properties><Property name="String"><String>2</String></Property><Property name="Fret"><Fret>7</Fret></Property><Property name="Muted"><Enable/></Property></Properties></Note>
    <Note id="4"><Properties><Property name="String"><String>3</String></Property><Property name="Fret"><Fret>2</Fret></Property></Properties></Note>
    <Note id="5"><Tie origin="false" destination="true"/><Properties><Property name="String"><String>3</String></Property><Property name="Fret"><Fret>2</Fret></Property></Properties></Note>
    <Note id="10"><InstrumentArticulation>0</InstrumentArticulation></Note>
    <Note id="11"><InstrumentArticulation>2</InstrumentArticulation></Note>
  </Notes>
  <Rhythms>
    <Rhythm id="q"><NoteValue>Quarter</NoteValue></Rhythm>
    <Rhythm id="h"><NoteValue>Half</NoteValue></Rhythm>
    <Rhythm id="16"><NoteValue>16th</NoteValue></Rhythm>
    <Rhythm id="dotted8"><NoteValue>Eighth</NoteValue><AugmentationDot count="1"/></Rhythm>
    <Rhythm id="trip"><NoteValue>Quarter</NoteValue><PrimaryTuplet num="3" den="2"/></Rhythm>
  </Rhythms>
</GPIF>)";

/** MusicXML with a guitar part written on notation and tab staves (drop D),
    and a bass part written only as notes. */
const char* const musicXml = R"(<?xml version="1.0" encoding="UTF-8"?>
<score-partwise version="4.0">
  <work><work-title>Little Riff</work-title></work>
  <part-list>
    <score-part id="P1"><part-name>Guitar</part-name><midi-instrument id="P1-I1"><midi-channel>1</midi-channel><midi-program>26</midi-program></midi-instrument></score-part>
    <score-part id="P2"><part-name>Bass</part-name><midi-instrument id="P2-I1"><midi-channel>2</midi-channel><midi-program>34</midi-program></midi-instrument></score-part>
  </part-list>
  <part id="P1">
    <measure number="1">
      <attributes>
        <divisions>2</divisions>
        <time><beats>4</beats><beat-type>4</beat-type></time>
        <staves>2</staves>
        <staff-details number="2">
          <staff-lines>6</staff-lines>
          <staff-tuning line="1"><tuning-step>D</tuning-step><tuning-octave>2</tuning-octave></staff-tuning>
          <staff-tuning line="2"><tuning-step>A</tuning-step><tuning-octave>2</tuning-octave></staff-tuning>
          <staff-tuning line="3"><tuning-step>D</tuning-step><tuning-octave>3</tuning-octave></staff-tuning>
          <staff-tuning line="4"><tuning-step>G</tuning-step><tuning-octave>3</tuning-octave></staff-tuning>
          <staff-tuning line="5"><tuning-step>B</tuning-step><tuning-octave>3</tuning-octave></staff-tuning>
          <staff-tuning line="6"><tuning-step>E</tuning-step><tuning-octave>4</tuning-octave></staff-tuning>
        </staff-details>
      </attributes>
      <direction><sound tempo="100"/></direction>
      <note><pitch><step>D</step><octave>2</octave></pitch><duration>2</duration><staff>1</staff></note>
      <note><chord/><pitch><step>A</step><octave>2</octave></pitch><duration>2</duration><staff>1</staff></note>
      <note><rest/><duration>6</duration><staff>1</staff></note>
      <backup><duration>8</duration></backup>
      <note><pitch><step>D</step><octave>2</octave></pitch><duration>2</duration><staff>2</staff>
        <notations><technical><string>6</string><fret>0</fret></technical></notations></note>
      <note><chord/><pitch><step>A</step><octave>2</octave></pitch><duration>2</duration><staff>2</staff>
        <notations><technical><string>5</string><fret>0</fret></technical></notations></note>
      <note><rest/><duration>6</duration><staff>2</staff></note>
    </measure>
    <measure number="2">
      <barline location="right"><repeat direction="backward"/></barline>
      <note><pitch><step>F</step><alter>1</alter><octave>3</octave></pitch><duration>8</duration><staff>2</staff>
        <notations><technical><string>4</string><fret>4</fret></technical></notations></note>
    </measure>
  </part>
  <part id="P2">
    <measure number="1">
      <attributes><divisions>1</divisions></attributes>
      <note><pitch><step>D</step><octave>2</octave></pitch><duration>4</duration></note>
    </measure>
    <measure number="2">
      <note><pitch><step>A</step><octave>1</octave></pitch><duration>4</duration><tie type="start"/></note>
    </measure>
  </part>
</score-partwise>)";
}

TEST_CASE ("Guitar Pro scores are read with strings, frets, rhythm and tempo", "[tab]")
{
    const auto result = tab::parseGpif (gpif);
    REQUIRE (result.ok());
    const auto& song = result.song;

    CHECK (song.title == "Test Song");
    CHECK (song.artist == "Nobody");
    REQUIRE (song.tracks.size() == 2);
    REQUIRE (song.masterBars.size() == 3);

    const auto& guitar = song.tracks[0];
    CHECK (guitar.name == "Lead Guitar");
    CHECK (guitar.hasTab());
    CHECK (guitar.capo == 2);
    CHECK (guitar.midiProgram == 29);

    const auto& drums = song.tracks[1];
    CHECK (drums.percussion);
    CHECK_FALSE (drums.hasTab());

    // Bar 1: a two-note chord, a dotted eighth, a sixteenth and a half-note rest.
    const auto& bar1 = guitar.bars[0].beats;
    REQUIRE (bar1.size() == 4);
    REQUIRE (bar1[0].notes.size() == 2);
    CHECK (bar1[0].notes[0].string == 0);
    CHECK (bar1[0].notes[0].fret == 3);
    CHECK (bar1[0].notes[0].midiNote == 40 + 3 + 2);   // low E, fret 3, capo 2
    CHECK (bar1[0].notes[1].midiNote == 64 + 2);
    CHECK_THAT (bar1[1].position, WithinAbs (1.0, 1e-9));
    CHECK_THAT (bar1[1].length, WithinAbs (0.75, 1e-9));
    CHECK (bar1[1].notes[0].palmMuted);
    CHECK_THAT (bar1[2].position, WithinAbs (1.75, 1e-9));
    CHECK (bar1[2].notes[0].dead);
    CHECK (bar1[3].notes.empty());

    // Bar 2: the second note is tied to the first.
    CHECK (guitar.bars[1].beats[1].notes[0].tiedFromPrevious);

    // Bar 3: the grace note is left out, triplets take two thirds of a beat.
    const auto& bar3 = guitar.bars[2].beats;
    REQUIRE (bar3.size() == 3);
    CHECK_THAT (bar3[2].position, WithinAbs (4.0 / 3.0, 1e-9));
    CHECK_THAT (song.masterBars[2].length, WithinAbs (3.0, 1e-9));

    // Drum articulations map to General MIDI sounds.
    REQUIRE (drums.bars[0].beats.size() == 4);
    CHECK (drums.bars[0].beats[0].notes[0].midiNote == 36);
    CHECK (drums.bars[0].beats[1].notes[0].midiNote == 37);

    CHECK (song.masterBars[0].section == "Intro");
    CHECK_THAT (song.initialBpm, WithinAbs (120.0, 1e-9));
    REQUIRE (song.masterBars[2].tempoChanges.size() == 1);
    CHECK_THAT (song.masterBars[2].tempoChanges[0].bpm, WithinAbs (90.0, 1e-9));
}

TEST_CASE ("Repeats are played out and the tempo map follows them", "[tab]")
{
    const auto song = tab::parseGpif (gpif).song;
    const auto order = tab::playOrder (song);

    REQUIRE (order.size() == 5);
    CHECK (order[0].masterBar == 0);
    CHECK (order[1].masterBar == 1);
    CHECK (order[2].masterBar == 0);
    CHECK (order[3].masterBar == 1);
    CHECK (order[4].masterBar == 2);
    CHECK_THAT (order[4].startBeat, WithinAbs (16.0, 1e-9));
    CHECK_THAT (tab::totalBeats (song, order), WithinAbs (19.0, 1e-9));

    const auto map = tab::tempoMap (song, order);
    REQUIRE (map.size() == 2);
    CHECK_THAT (map[1].beat, WithinAbs (16.0, 1e-9));
    CHECK_THAT (map[1].bpm, WithinAbs (90.0, 1e-9));

    // 16 beats at 120 = 8 s, then 3 beats at 90 = 2 s.
    CHECK_THAT (tab::beatToSeconds (map, 19.0), WithinAbs (10.0, 1e-9));
}

TEST_CASE ("Volta endings are taken on the right pass", "[tab]")
{
    tab::Song song;
    song.tracks.resize (1);
    song.masterBars.resize (6);

    // | A | B |1. C :|2. D | E |: F :|
    song.masterBars[0].repeatStart = true;
    song.masterBars[2].endings = 1;
    song.masterBars[2].repeatCount = 2;
    song.masterBars[3].endings = 2;
    song.masterBars[5].repeatStart = true;
    song.masterBars[5].repeatCount = 2;

    std::vector<int> bars;

    for (auto& p : tab::playOrder (song))
        bars.push_back (p.masterBar);

    CHECK (bars == std::vector<int> { 0, 1, 2, 0, 1, 3, 4, 5, 5 });
}

TEST_CASE ("A repeat with no start sign goes back to the end of the previous one", "[tab]")
{
    tab::Song song;
    song.tracks.resize (1);
    song.masterBars.resize (4);
    song.masterBars[1].repeatCount = 2;
    song.masterBars[3].repeatCount = 3;

    std::vector<int> bars;

    for (auto& p : tab::playOrder (song))
        bars.push_back (p.masterBar);

    CHECK (bars == std::vector<int> { 0, 1, 0, 1, 2, 3, 2, 3, 2, 3 });
}

TEST_CASE ("MusicXML tab staves keep their strings and notation-only parts get fingered", "[tab]")
{
    const auto result = tab::parseMusicXml (musicXml);
    REQUIRE (result.ok());
    const auto& song = result.song;

    CHECK (song.title == "Little Riff");
    CHECK_THAT (song.initialBpm, WithinAbs (100.0, 1e-9));
    REQUIRE (song.tracks.size() == 2);
    REQUIRE (song.masterBars.size() == 2);
    CHECK (song.masterBars[1].repeatCount == 2);

    const auto& guitar = song.tracks[0];
    CHECK (guitar.midiProgram == 25);
    REQUIRE (guitar.tuning == std::vector<int> { 38, 45, 50, 55, 59, 64 });

    // Only the tab staff's copy of each note is kept.
    const auto& bar1 = guitar.bars[0].beats;
    REQUIRE (bar1.size() == 2);
    REQUIRE (bar1[0].notes.size() == 2);
    CHECK (bar1[0].notes[0].string == 0);
    CHECK (bar1[0].notes[0].fret == 0);
    CHECK (bar1[0].notes[1].string == 1);
    CHECK_THAT (bar1[1].position, WithinAbs (1.0, 1e-9));

    const auto& bar2 = guitar.bars[1].beats;
    REQUIRE (bar2.size() == 1);
    CHECK (bar2[0].notes[0].string == 2);
    CHECK (bar2[0].notes[0].fret == 4);
    CHECK (bar2[0].notes[0].midiNote == 54);

    // The bass had no tab, so it gets standard bass tuning and picked frets.
    const auto& bass = song.tracks[1];
    REQUIRE (bass.hasTab());
    CHECK (bass.tuning.size() == 4);
    REQUIRE (bass.bars[1].beats.size() == 1);
    const auto& a = bass.bars[1].beats[0].notes[0];
    CHECK (a.midiNote == 33);
    CHECK (a.string == 1);
    CHECK (a.fret == 0);
}

TEST_CASE ("Guitar Pro files are read from their zip", "[tab]")
{
    const auto dir = juce::File::createTempFile ("tabtest");
    dir.createDirectory();
    const auto gpifFile = dir.getChildFile ("score.gpif");
    gpifFile.replaceWithText (gpif);

    const auto gpFile = dir.getChildFile ("Test.gp");

    {
        juce::ZipFile::Builder builder;
        builder.addFile (gpifFile, 9, "Content/score.gpif");
        juce::FileOutputStream out (gpFile);
        REQUIRE (out.openedOk());
        REQUIRE (builder.writeToStream (out, nullptr));
    }

    const auto result = tab::loadFile (gpFile);
    CHECK (result.ok());
    CHECK (result.song.title == "Test Song");

    const auto old = dir.getChildFile ("Old.gp5");
    old.replaceWithText ("not really");
    CHECK_FALSE (tab::loadFile (old).ok());

    dir.deleteRecursively();
}

TEST_CASE ("A song becomes band notes with drums, ties, muted parts and speed", "[tab]")
{
    const auto song = tab::parseGpif (gpif).song;
    const auto order = tab::playOrder (song);

    auto arrangement = tab::arrangeSong (song, order, {});
    CHECK_FALSE (arrangement.loops);
    CHECK_THAT (arrangement.lengthBeats, WithinAbs (19.0, 1e-9));
    REQUIRE (arrangement.tempoMap.size() == 2);
    CHECK (arrangement.guitarDrive > 0.0f);

    int drums = 0, guitar = 0;

    for (auto& e : arrangement.events)
    {
        if (e.instrument == jam::Instrument::drums)
            ++drums;
        else if (e.instrument == jam::Instrument::guitar)
            ++guitar;
    }

    CHECK (drums == 4 + 4 + 4 + 4 + 3);
    // Per pass: 3 sounding beats in bar 1, 1 in bar 2 (the tie extends it); then 3 in bar 3.
    CHECK (guitar == 3 + 1 + 3 + 1 + 3);

    // The tied half note became one whole note.
    bool foundTiedNote = false;

    for (auto& e : arrangement.events)
        if (e.instrument == jam::Instrument::guitar && std::abs (e.beat - 4.0) < 1e-9)
            foundTiedNote = std::abs (e.lengthBeats - 4.0f) < 1e-6f;

    CHECK (foundTiedNote);

    // Events are in time order.
    for (size_t i = 1; i < arrangement.events.size(); ++i)
        CHECK (arrangement.events[i - 1].beat <= arrangement.events[i].beat);

    CHECK (tab::drumFor (36) == jam::Drum::kick);
    CHECK (tab::drumFor (42) == jam::Drum::closedHat);

    tab::ArrangeOptions options;
    options.trackOn = { false, true };
    options.speed = 0.5;
    arrangement = tab::arrangeSong (song, order, options);

    for (auto& e : arrangement.events)
        CHECK (e.instrument == jam::Instrument::drums);

    CHECK_THAT (arrangement.tempoMap[0].bpm, WithinAbs (60.0, 1e-9));
}

TEST_CASE ("The band plays a song once at the song's own tempo and stops at the end", "[tab]")
{
    constexpr double rate = 48000.0;
    constexpr int block = 256;

    const auto song = tab::parseGpif (gpif).song;
    const auto order = tab::playOrder (song);

    JamTrack band;
    band.prepare (rate);
    band.setArrangement (tab::arrangeSong (song, order, {}));
    band.start (false);

    std::vector<float> left (block), right (block);
    float peak = 0.0f;
    bool finite = true;

    // The song lasts 10 s (16 beats at 120, then 3 at 90). The metronome
    // tempo passed in is ignored because the song has its own tempo map.
    auto render = [&] (double seconds)
    {
        for (int done = 0; done < (int) (seconds * rate); done += block)
        {
            std::fill (left.begin(), left.end(), 0.0f);
            std::fill (right.begin(), right.end(), 0.0f);
            band.process (left.data(), right.data(), block, 200.0);

            for (int i = 0; i < block; ++i)
            {
                finite = finite && std::isfinite (left[(size_t) i]);
                peak = juce::jmax (peak, std::abs (left[(size_t) i]));
            }
        }
    };

    render (4.0);
    CHECK_THAT (band.getPositionBeats(), WithinAbs (8.0, 0.1));   // 120 BPM, not 200
    CHECK (band.isPlaying());

    render (7.0);
    CHECK_FALSE (band.isPlaying());
    CHECK_THAT (band.getPositionBeats(), WithinAbs (19.0, 1e-6));
    CHECK (finite);
    CHECK (peak > 0.01f);
    CHECK (peak < 1.0f);
    band.collectGarbage();
}
