#include "engine/JamTrack.h"
#include "engine/Practice.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>

using namespace jam;

namespace
{
constexpr double rate = 48000.0;
constexpr int block = 128;

const ProgressionPreset& preset (const juce::String& name)
{
    for (const auto& p : getProgressionPresets())
        if (p.name == name)
            return p;

    FAIL ("no preset called " << name);
    return getProgressionPresets().front();
}

struct Render
{
    std::vector<float> left, right;

    float peak() const
    {
        float p = 0.0f;

        for (size_t i = 0; i < left.size(); ++i)
            p = std::max ({ p, std::abs (left[i]), std::abs (right[i]) });

        return p;
    }

    float rms (size_t from, size_t to) const
    {
        double sum = 0.0;

        for (auto i = from; i < to && i < left.size(); ++i)
            sum += left[i] * left[i];

        return (float) std::sqrt (sum / (double) juce::jmax ((size_t) 1, to - from));
    }

    bool finite() const
    {
        for (size_t i = 0; i < left.size(); ++i)
            if (! std::isfinite (left[i]) || ! std::isfinite (right[i]))
                return false;

        return true;
    }
};

Render run (JamTrack& track, double seconds, double bpm = 120.0)
{
    Render r;
    const auto total = (size_t) (seconds * rate);
    r.left.assign (total, 0.0f);
    r.right.assign (total, 0.0f);

    for (size_t offset = 0; offset < total; offset += block)
    {
        const auto count = (int) juce::jmin ((size_t) block, total - offset);
        track.process (r.left.data() + offset, r.right.data() + offset, count, bpm);
    }

    return r;
}

Arrangement arrangement (Style style, const juce::String& progression = "I - V - vi - IV", Key key = { 0, Mode::major })
{
    return arrange (preset (progression).slots, key, style);
}
}

//==============================================================================
TEST_CASE ("chord names are spelt to suit the key")
{
    CHECK (chordName ({ 10, Quality::major }, { 9, Mode::minor }) == "G");
    CHECK (chordName ({ 10, Quality::major }, { 5, Mode::major }) == "Eb");
    CHECK (chordName ({ 8, Quality::major }, { 0, Mode::major }) == "Ab");
    CHECK (chordName ({ 9, Quality::minor7 }, { 7, Mode::major }) == "Em7");
    CHECK (chordName ({ 0, Quality::dominant7 }, { 6, Mode::major }) == "F#7");
    CHECK (keyName ({ 3, Mode::minor }) == "D# minor");
    CHECK (keyName ({ 2, Mode::minor }) == "D minor");
    CHECK (keyName ({ 10, Mode::major }) == "Bb major");
}

TEST_CASE ("Roman numerals follow the chord's quality")
{
    CHECK (romanNumeral ({ 2, Quality::minor7 }, Mode::major) == "ii7");
    CHECK (romanNumeral ({ 7, Quality::dominant7 }, Mode::major) == "V7");
    CHECK (romanNumeral ({ 10, Quality::major }, Mode::major) == "bVII");
    CHECK (romanNumeral ({ 10, Quality::major }, Mode::minor) == "VII");
    CHECK (romanNumeral ({ 0, Quality::minor }, Mode::minor) == "i");
}

TEST_CASE ("the chords offered for a key are in that key, apart from the borrowed ones")
{
    for (auto mode : { Mode::major, Mode::minor })
    {
        const auto choices = chordsForKey (mode);
        CHECK (choices.size() >= 14);

        for (const auto& choice : choices)
            CHECK (chordFitsKey (choice.chord, mode) == (choice.group != "Borrowed"));
    }
}

TEST_CASE ("progressions survive the settings file")
{
    for (const auto& p : getProgressionPresets())
    {
        CHECK (! p.slots.empty());
        CHECK (totalBeats (p.slots) <= maxProgressionBeats);
        CHECK (progressionFromString (progressionToString (p.slots)) == p.slots);
    }

    CHECK (progressionFromString ("nonsense").empty());
    CHECK (progressionFromString ("0:maj:3").empty());
    CHECK (preset ("12-bar blues").slots.size() > 0);
    CHECK (totalBeats (preset ("12-bar blues").slots) == 48);
    CHECK (totalBeats (preset ("Minor 12-bar blues").slots) == 48);
}

TEST_CASE ("each style's suggested progression exists and fits its mode")
{
    for (const auto& style : getStyles())
    {
        const auto& p = preset (style.suggestedProgression);
        CHECK (p.mode == style.suggestedMode);
    }
}

//==============================================================================
TEST_CASE ("every style gives every instrument something to play")
{
    for (const auto& style : getStyles())
    {
        INFO (style.name);
        const auto a = arrangement (style.style);
        CHECK (juce::exactlyEqual (a.lengthBeats, 16.0));

        std::array<int, numInstruments> counts {};

        for (size_t i = 0; i < a.events.size(); ++i)
        {
            const auto& e = a.events[i];
            ++counts[(size_t) e.instrument];

            CHECK (e.beat >= 0.0);
            CHECK (e.beat < a.lengthBeats);
            CHECK (e.lengthBeats > 0.0f);

            if (i > 0)
                CHECK (a.events[i - 1].beat <= e.beat);

            for (int n = 0; n < e.numNotes; ++n)
            {
                CHECK (e.notes[(size_t) n] >= 24);
                CHECK (e.notes[(size_t) n] <= 96);
            }
        }

        for (auto count : counts)
            CHECK (count > 0);
    }
}

TEST_CASE ("the bass plays the root on the first beat of each chord")
{
    const auto a = arrangement (Style::rock, "I - V - vi - IV", { 9, Mode::major });   // A E F#m D
    const std::array<int, 4> roots { 9, 4, 6, 2 };

    for (int bar = 0; bar < 4; ++bar)
    {
        bool found = false;

        for (const auto& e : a.events)
            if (e.instrument == Instrument::bass && std::abs (e.beat - bar * 4.0) < 0.05)
                found = found || (e.notes[0] % 12 == roots[(size_t) bar]);

        CHECK (found);
    }
}

TEST_CASE ("the same inputs always give the same arrangement")
{
    const auto a = arrangement (Style::jazz, "ii - V - I");
    const auto b = arrangement (Style::jazz, "ii - V - I");
    REQUIRE (a.events.size() == b.events.size());

    for (size_t i = 0; i < a.events.size(); ++i)
    {
        CHECK (juce::exactlyEqual (a.events[i].beat, b.events[i].beat));
        CHECK (a.events[i].notes == b.events[i].notes);
    }
}

TEST_CASE ("jazz walks the bass in quarter notes")
{
    const auto a = arrangement (Style::jazz, "ii - V - I");
    int bassNotes = 0;

    for (const auto& e : a.events)
        if (e.instrument == Instrument::bass)
            ++bassNotes;

    CHECK (bassNotes == 16);
}

//==============================================================================
TEST_CASE ("the band plays after start and goes quiet after stop")
{
    JamTrack track;
    track.prepare (rate);
    track.setArrangement (arrangement (Style::rock));

    auto silent = run (track, 0.5);
    CHECK (silent.peak() <= 0.0f);

    track.start (false);
    auto playing = run (track, 4.0);
    CHECK (playing.finite());
    CHECK (playing.peak() > 0.05f);
    CHECK (playing.peak() < 1.0f);
    CHECK (track.isPlaying());
    CHECK (track.getPositionBeats() > 7.5);

    track.stop();
    run (track, 0.2);
    auto after = run (track, 2.0);
    CHECK (after.rms (after.left.size() / 2, after.left.size()) < 1.0e-3f);
    CHECK (! track.isPlaying());
}

TEST_CASE ("every style plays cleanly at its own tempo")
{
    for (const auto& style : getStyles())
    {
        INFO (style.name);
        JamTrack track;
        track.prepare (rate);
        track.setArrangement (arrange (preset (style.suggestedProgression).slots, { 4, style.suggestedMode }, style.style));

        for (auto i : { Instrument::drums, Instrument::bass, Instrument::guitar, Instrument::keys, Instrument::organ })
            track.setOn (i, true);

        track.start (false);
        const auto r = run (track, 6.0, style.suggestedBpm);
        CHECK (r.finite());
        CHECK (r.peak() > 0.05f);
        CHECK (r.peak() < 1.0f);
    }
}

TEST_CASE ("the count-in clicks for a bar before the band comes in")
{
    JamTrack track;
    track.prepare (rate);
    track.setArrangement (arrangement (Style::pop));
    track.start (true);

    // At 120 BPM the count-in is two seconds.
    run (track, 0.1);
    CHECK (track.getPositionBeats() < 0.0);

    const auto r = run (track, 2.5);
    CHECK (r.peak() > 0.01f);
    CHECK (track.getPositionBeats() > 0.0);
}

TEST_CASE ("each instrument can be switched off")
{
    for (auto only : { Instrument::drums, Instrument::bass, Instrument::guitar, Instrument::keys, Instrument::organ })
    {
        INFO (instrumentName (only));
        JamTrack track;
        track.prepare (rate);
        track.setArrangement (arrangement (Style::pop));

        for (auto i : { Instrument::drums, Instrument::bass, Instrument::guitar, Instrument::keys, Instrument::organ })
            track.setOn (i, i == only);

        track.start (false);
        CHECK (run (track, 3.0).peak() > 0.01f);

        track.setOn (only, false);
        run (track, 0.5);   // gains ramp down, but notes already playing are not fired again
        track.stop();
        run (track, 1.0);   // the room's echo dies away
        CHECK (run (track, 1.0).peak() < 1.0e-3f);
    }
}

TEST_CASE ("a new arrangement takes over while playing, at the same place")
{
    JamTrack track;
    track.prepare (rate);
    track.setArrangement (arrangement (Style::rock));
    track.start (false);
    run (track, 3.0);
    const auto before = track.getPositionBeats();

    track.setArrangement (arrangement (Style::jazz, "ii - V - I", { 2, Mode::major }));
    const auto r = run (track, 2.0);
    CHECK (r.finite());
    CHECK (track.isPlaying());
    CHECK (track.getPositionBeats() > before);
    track.collectGarbage();
}

TEST_CASE ("the loop wraps round to the start")
{
    JamTrack track;
    track.prepare (rate);
    track.setArrangement (arrangement (Style::rock));   // 16 beats = 8 s at 120
    track.start (false);
    run (track, 9.0);
    CHECK (track.getPositionBeats() > 1.5);
    CHECK (track.getPositionBeats() < 2.5);
}

TEST_CASE ("a song that does not loop stops at its end and follows its tempo map")
{
    JamTrack track;
    track.prepare (rate);
    auto song = arrangement (Style::pop);
    song.loops = false;
    song.tempoMap = { { 0.0, 240.0 } };   // 16 beats in 4 seconds
    track.setArrangement (song);
    track.start (false);

    run (track, 3.0, 60.0);
    CHECK (track.isPlaying());
    CHECK (track.getPositionBeats() > 11.0);

    run (track, 1.5, 60.0);
    CHECK (! track.isPlaying());
    CHECK (juce::exactlyEqual (track.getPositionBeats(), 16.0));
}

TEST_CASE ("the practice tools mix the band in, but the looper does not record it")
{
    PracticeTools tools;
    tools.prepare (rate, block);
    tools.getJamTrack().setArrangement (arrangement (Style::rock));
    tools.getJamTrack().start (false);

    std::vector<float> left (block), right (block);
    float peak = 0.0f;

    for (int i = 0; i < 400; ++i)
    {
        std::fill (left.begin(), left.end(), 0.0f);
        std::fill (right.begin(), right.end(), 0.0f);
        tools.process (nullptr, left.data(), right.data(), block, false);

        for (auto s : left)
            peak = std::max (peak, std::abs (s));
    }

    CHECK (peak > 0.05f);
}

TEST_CASE ("the band costs little processing time")
{
    JamTrack track;
    track.prepare (rate);
    track.setArrangement (arrangement (Style::funk));

    for (auto i : { Instrument::drums, Instrument::bass, Instrument::guitar, Instrument::keys, Instrument::organ })
        track.setOn (i, true);

    track.start (false);

    const auto started = std::chrono::steady_clock::now();
    run (track, 20.0, 100.0);
    const auto seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count();

    // Twenty seconds of music should take well under a second to make.
    CHECK (seconds < 2.0);
    WARN ("20 s of the full band took " << seconds << " s");
}

//==============================================================================
// Not run by default. Writes a short WAV of every style to the folder in
// JAM_DEMO_DIR, to listen to the band:  GuitarSimulatorTests "[.demo]"
TEST_CASE ("render a demo of each style", "[.demo]")
{
    const auto folder = juce::File (juce::SystemStats::getEnvironmentVariable ("JAM_DEMO_DIR", juce::File::getCurrentWorkingDirectory().getFullPathName()));
    folder.createDirectory();

    int number = 1;

    for (const auto& style : getStyles())
    {
        JamTrack track;
        track.prepare (rate);
        track.setArrangement (arrange (preset (style.suggestedProgression).slots, { style.suggestedMode == Mode::minor ? 9 : 4, style.suggestedMode }, style.style));
        // JAM_DEMO_SOLO=n plays only instrument n (0 drums ... 4 organ).
        const auto solo = juce::SystemStats::getEnvironmentVariable ("JAM_DEMO_SOLO", {});

        if (solo.isNotEmpty())
            for (int i = 0; i < numInstruments; ++i)
                track.setOn ((Instrument) i, i == solo.getIntValue());

        track.start (false);

        const auto beats = juce::jmax (16.0, track.getLengthBeats());
        const auto seconds = juce::jmin (24.0, beats * 60.0 / style.suggestedBpm);
        const auto r = run (track, seconds, style.suggestedBpm);

        juce::AudioBuffer<float> buffer (2, (int) r.left.size());
        buffer.copyFrom (0, 0, r.left.data(), (int) r.left.size());
        buffer.copyFrom (1, 0, r.right.data(), (int) r.right.size());

        const auto name = juce::String (number++).paddedLeft ('0', 2) + "-" + style.name.toLowerCase().replace (" ", "-") + ".wav";
        auto file = folder.getChildFile (name);
        file.deleteFile();

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (24));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    }
}

//==============================================================================
namespace
{
/** The strongest pitch in `signal` between 35 and 1000 Hz, by autocorrelation. */
double pitchOf (const std::vector<float>& signal)
{
    const auto minLag = (int) (rate / 1000.0), maxLag = (int) (rate / 35.0);
    const auto n = (int) signal.size() - maxLag - 2;
    std::vector<double> scores ((size_t) maxLag + 2, 0.0);

    for (int lag = minLag; lag <= maxLag + 1; ++lag)
        for (int i = 0; i < n; ++i)
            scores[(size_t) lag] += signal[(size_t) i] * signal[(size_t) (i + lag)];

    auto best = minLag;

    for (int lag = minLag; lag <= maxLag; ++lag)
        if (scores[(size_t) lag] > scores[(size_t) best])
            best = lag;

    // Parabolic interpolation around the peak.
    const auto a = scores[(size_t) best - 1], b = scores[(size_t) best], c = scores[(size_t) best + 1];
    const auto shift = 0.5 * (a - c) / (a - 2.0 * b + c);
    return rate / (best + shift);
}

double centsBetween (double hz, double reference)
{
    return 1200.0 * std::log2 (hz / reference);
}
}

TEST_CASE ("the guitar and bass play in tune")
{
    for (auto note : { 40, 45, 52, 57, 64 })
    {
        INFO ("note " << note);
        synth::Guitar guitar;
        guitar.prepare (rate);

        NoteEvent e;
        e.instrument = Instrument::guitar;
        e.numNotes = 1;
        e.notes[0] = (std::int8_t) note;
        e.velocity = 0.8f;
        guitar.play (e, (int) rate);

        std::vector<float> out ((size_t) rate / 2, 0.0f);
        guitar.render (out.data(), (int) out.size());
        out.erase (out.begin(), out.begin() + 4800);   // past the pick

        CHECK (std::abs (centsBetween (pitchOf (out), synth::midiToHz (note))) < 8.0);
    }

    for (auto note : { 28, 33, 40 })
    {
        INFO ("bass note " << note);
        synth::Bass bass;
        bass.prepare (rate);
        bass.noteOn (note, 0.8f, (int) rate);

        std::vector<float> out ((size_t) rate / 2, 0.0f);
        bass.render (out.data(), (int) out.size());
        out.erase (out.begin(), out.begin() + 4800);

        CHECK (std::abs (centsBetween (pitchOf (out), synth::midiToHz (note))) < 8.0);
    }
}
