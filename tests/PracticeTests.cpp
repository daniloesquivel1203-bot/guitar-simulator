#include "TestSignals.h"
#include "engine/Practice.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

namespace
{
constexpr double rate = 48000.0;
constexpr int block = 64;

/** Runs the looper over `input` in blocks, with input and output in separate
    buffers, and returns what it added to the output. */
std::vector<float> runLooper (Looper& looper, const std::vector<float>& input)
{
    std::vector<float> out (input.size(), 0.0f);

    for (size_t offset = 0; offset < input.size(); offset += block)
    {
        const auto count = (int) juce::jmin ((size_t) block, input.size() - offset);
        looper.process (input.data() + offset, input.data() + offset, out.data() + offset, out.data() + offset, count);
    }

    return out;
}

std::vector<float> constant (float value, int numSamples)
{
    return std::vector<float> ((size_t) numSamples, value);
}

/** Indexes where the metronome output jumps from silence into a click. */
std::vector<int> clickStarts (const std::vector<float>& signal)
{
    std::vector<int> starts;
    int quietFor = 1000;

    for (int i = 0; i < (int) signal.size(); ++i)
    {
        if (std::abs (signal[(size_t) i]) > 1.0e-4f)
        {
            if (quietFor > 500)
                starts.push_back (i);

            quietFor = 0;
        }
        else
        {
            ++quietFor;
        }
    }

    return starts;
}
}

//==============================================================================
TEST_CASE ("the metronome clicks on every beat at the set tempo")
{
    Metronome metronome;
    metronome.prepare (rate);
    metronome.setBpm (120.0);
    metronome.setVolumeDb (0.0f);
    metronome.setRunning (true);

    std::vector<float> out ((size_t) rate * 3, 0.0f);

    for (size_t offset = 0; offset < out.size(); offset += block)
        metronome.process (out.data() + offset, out.data() + offset, block);

    const auto starts = clickStarts (out);
    REQUIRE (starts.size() == 6);

    // The first click is right away, then one every half second, without drift.
    for (size_t i = 0; i < starts.size(); ++i)
        REQUIRE (std::abs (starts[i] - (int) i * 24000) <= 1);
}

TEST_CASE ("the first beat of the bar is louder unless the accent is off")
{
    const auto firstTwoPeaks = [] (bool accent)
    {
        Metronome metronome;
        metronome.prepare (rate);
        metronome.setBpm (120.0);
        metronome.setBeatsPerBar (4);
        metronome.setAccentFirstBeat (accent);
        metronome.setRunning (true);

        std::vector<float> out ((size_t) rate, 0.0f);
        metronome.process (out.data(), out.data(), (int) out.size());

        return std::pair { test::peak (std::vector<float> (out.begin(), out.begin() + 20000)),
                           test::peak (out, 24000) };
    };

    const auto [accented, plain] = firstTwoPeaks (true);
    REQUIRE (accented > plain * 1.3f);

    const auto [first, second] = firstTwoPeaks (false);
    REQUIRE_THAT (first, WithinAbs (second, 0.01));
}

TEST_CASE ("a stopped metronome adds nothing and lowering the volume makes it quieter")
{
    Metronome metronome;
    metronome.prepare (rate);

    std::vector<float> out (4800, 0.0f);
    metronome.process (out.data(), out.data(), (int) out.size());
    REQUIRE (juce::exactlyEqual (test::peak (out), 0.0f));
    REQUIRE (metronome.getCurrentBeat() == -1);

    metronome.setRunning (true);
    metronome.setVolumeDb (0.0f);
    metronome.process (out.data(), out.data(), (int) out.size());
    const auto loud = test::peak (out);

    metronome.setRunning (false);
    metronome.process (out.data(), out.data(), (int) out.size());   // let the click die away
    std::fill (out.begin(), out.end(), 0.0f);

    metronome.setVolumeDb (-20.0f);
    metronome.setRunning (true);
    metronome.process (out.data(), out.data(), (int) out.size());
    REQUIRE_THAT (test::peak (out), WithinAbs (loud * 0.1f, 0.01));
    REQUIRE (metronome.getCurrentBeat() == 0);
}

TEST_CASE ("tap tempo averages the taps and starts over after a long pause")
{
    TapTempo taps;
    REQUIRE_FALSE (taps.tap (10.0).has_value());
    REQUIRE_THAT (*taps.tap (10.5), WithinAbs (120.0, 0.01));
    REQUIRE_THAT (*taps.tap (11.0), WithinAbs (120.0, 0.01));
    REQUIRE_THAT (*taps.tap (11.6), WithinAbs (60.0 * 3.0 / 1.6, 0.01));

    // After a pause the old taps are forgotten.
    REQUIRE_FALSE (taps.tap (20.0).has_value());
    REQUIRE_THAT (*taps.tap (21.0), WithinAbs (60.0, 0.01));
}

//==============================================================================
TEST_CASE ("the looper plays back what it recorded, over and over")
{
    Looper looper;
    looper.prepare (rate);
    REQUIRE (looper.getState() == Looper::State::empty);

    const auto phrase = test::sine (220.0, 0.5f, rate, 24000);

    looper.pressMain();
    const auto whileRecording = runLooper (looper, phrase);
    REQUIRE (looper.getState() == Looper::State::recording);
    REQUIRE (juce::exactlyEqual (test::peak (whileRecording), 0.0f));   // the live sound is already in the output
    REQUIRE (looper.getLength() == 24000);

    looper.pressMain();
    const auto silence = constant (0.0f, 48000);
    const auto played = runLooper (looper, silence);
    REQUIRE (looper.getState() == Looper::State::playing);
    REQUIRE (looper.getLength() == 24000);

    // Away from the faded ends, both passes match the recording.
    for (int i = 1000; i < 23000; ++i)
    {
        REQUIRE_THAT (played[(size_t) i], WithinAbs (phrase[(size_t) i], 1.0e-6));
        REQUIRE_THAT (played[(size_t) i + 24000], WithinAbs (phrase[(size_t) i], 1.0e-6));
    }

    // The seam is faded so it does not click.
    REQUIRE (std::abs (played[0]) < 1.0e-6f);
    REQUIRE (std::abs (played[23999]) < 0.01f);
}

TEST_CASE ("an overdub layers on the loop and undo takes it back")
{
    Looper looper;
    looper.prepare (rate);

    looper.pressMain();
    runLooper (looper, constant (0.25f, 24000));
    looper.pressMain();
    runLooper (looper, constant (0.0f, 1000));   // playing; position is now 1000

    // Overdub one and a half passes. Each sample is only layered once per pass.
    looper.pressOverdub();
    runLooper (looper, constant (0.1f, 36000));
    REQUIRE (looper.getState() == Looper::State::overdubbing);
    REQUIRE (looper.canUndo());

    looper.pressMain();
    auto played = runLooper (looper, constant (0.0f, 24000));
    REQUIRE (looper.getState() == Looper::State::playing);

    // Position 1000 + 36000 + 1 block: the samples overdubbed twice hold 0.45.
    const auto layered = test::peak (played);
    REQUIRE_THAT (layered, WithinAbs (0.45, 1.0e-5));

    looper.pressUndo();
    runLooper (looper, constant (0.0f, 4800));
    REQUIRE_FALSE (looper.canUndo());

    played = runLooper (looper, constant (0.0f, 24000));
    REQUIRE_THAT (test::peak (played), WithinAbs (0.25, 1.0e-5));
}

TEST_CASE ("undo during an overdub ends it and takes it back")
{
    Looper looper;
    looper.prepare (rate);

    looper.pressMain();
    runLooper (looper, constant (0.25f, 24000));
    looper.pressMain();
    looper.pressOverdub();   // straight into overdub
    runLooper (looper, constant (0.1f, 6000));

    looper.pressUndo();
    runLooper (looper, constant (0.0f, 640));
    REQUIRE (looper.getState() == Looper::State::playing);

    const auto played = runLooper (looper, constant (0.0f, 24000));
    REQUIRE_THAT (test::peak (played), WithinAbs (0.25, 1.0e-5));
}

TEST_CASE ("stop, play again from the start, and clear")
{
    Looper looper;
    looper.prepare (rate);

    looper.pressMain();
    runLooper (looper, constant (0.25f, 24000));
    looper.pressStop();
    REQUIRE (juce::exactlyEqual (test::peak (runLooper (looper, constant (0.0f, 4800))), 0.0f));
    REQUIRE (looper.getState() == Looper::State::stopped);
    REQUIRE (looper.getLength() == 24000);

    looper.pressMain();
    const auto played = runLooper (looper, constant (0.0f, 4800));
    REQUIRE (looper.getState() == Looper::State::playing);
    REQUIRE_THAT (played[1000], WithinAbs (0.25, 1.0e-6));

    looper.pressClear();
    REQUIRE (juce::exactlyEqual (test::peak (runLooper (looper, constant (0.0f, 4800))), 0.0f));
    REQUIRE (looper.getState() == Looper::State::empty);
    REQUIRE (looper.getLength() == 0);
}

TEST_CASE ("a very short loop is thrown away, as after a double press")
{
    Looper looper;
    looper.prepare (rate);

    looper.pressMain();
    runLooper (looper, constant (0.25f, 640));
    looper.pressMain();
    runLooper (looper, constant (0.0f, 640));

    REQUIRE (looper.getState() == Looper::State::empty);
}

TEST_CASE ("recording stops by itself at the longest loop and starts playing")
{
    Looper looper;
    looper.prepare (8000.0);   // a low rate keeps the test quick

    const auto capacity = (int) (Looper::maxLoopSeconds * 8000.0);
    looper.pressMain();
    runLooper (looper, constant (0.25f, capacity + 1000));

    REQUIRE (looper.getState() == Looper::State::playing);
    REQUIRE (looper.getLength() == capacity);
}

TEST_CASE ("the looper works in place on a single mono output")
{
    Looper looper;
    looper.prepare (rate);

    std::vector<float> buffer (24000, 0.3f);
    looper.pressMain();

    for (size_t offset = 0; offset < buffer.size(); offset += block)
        looper.process (buffer.data() + offset, buffer.data() + offset, buffer.data() + offset, buffer.data() + offset, block);

    looper.pressMain();
    std::vector<float> live (24000, 0.1f);

    for (size_t offset = 0; offset < live.size(); offset += block)
        looper.process (live.data() + offset, live.data() + offset, live.data() + offset, live.data() + offset, block);

    REQUIRE_THAT (live[12000], WithinAbs (0.4, 1.0e-6));
}

TEST_CASE ("the looper keeps a picture of each layer")
{
    Looper looper;
    looper.prepare (rate);
    REQUIRE (looper.getNumLayers() == 0);

    // The first pass fills the whole first lane.
    looper.pressMain();
    runLooper (looper, constant (0.25f, 24000));
    REQUIRE (looper.getNumLayerLanes() == 1);
    looper.pressMain();
    runLooper (looper, constant (0.0f, 12000));   // playing; halfway through
    REQUIRE (looper.getNumLayerBinsFilled() == Looper::layerBins);

    for (int bin = 0; bin < Looper::layerBins; ++bin)
        REQUIRE_THAT (looper.getLayerPeak (0, bin), WithinAbs (0.25, 1.0e-6));

    // An overdub over the second half only marks that half of its own lane.
    looper.pressOverdub();
    runLooper (looper, constant (0.5f, 12000));
    looper.pressMain();
    runLooper (looper, constant (0.0f, 640));
    REQUIRE (looper.getState() == Looper::State::playing);
    REQUIRE (looper.getNumLayers() == 2);
    REQUIRE (juce::exactlyEqual (looper.getLayerPeak (1, Looper::layerBins / 4), 0.0f));
    REQUIRE_THAT (looper.getLayerPeak (1, Looper::layerBins * 3 / 4), WithinAbs (0.5, 1.0e-6));
    REQUIRE_THAT (looper.getLayerPeak (0, Looper::layerBins * 3 / 4), WithinAbs (0.25, 1.0e-6));

    // Undo removes that lane, and clear removes them all.
    looper.pressUndo();
    runLooper (looper, constant (0.0f, 4800));
    REQUIRE (looper.getNumLayers() == 1);

    looper.pressClear();
    runLooper (looper, constant (0.0f, 640));
    REQUIRE (looper.getNumLayers() == 0);
    REQUIRE (looper.getNumLayerBinsFilled() == 0);
}

TEST_CASE ("an overdub that recorded nothing adds no layer and keeps the last undo")
{
    Looper looper;
    looper.prepare (rate);

    looper.pressMain();
    runLooper (looper, constant (0.25f, 24000));
    looper.pressMain();
    looper.pressOverdub();
    runLooper (looper, constant (0.1f, 6000));
    looper.pressMain();
    runLooper (looper, constant (0.0f, 640));
    REQUIRE (looper.getNumLayers() == 2);
    REQUIRE (looper.canUndo());

    // Overdub on and off inside one audio block.
    looper.pressOverdub();
    looper.pressMain();
    runLooper (looper, constant (0.0f, 640));
    REQUIRE (looper.getState() == Looper::State::playing);
    REQUIRE (looper.getNumLayers() == 2);
    REQUIRE (looper.canUndo());

    looper.pressUndo();
    runLooper (looper, constant (0.0f, 4800));
    REQUIRE (looper.getNumLayers() == 1);

    const auto played = runLooper (looper, constant (0.0f, 24000));
    REQUIRE_THAT (test::peak (played), WithinAbs (0.25, 1.0e-5));
}

TEST_CASE ("once every lane is in use, the oldest overdub folds into the first")
{
    Looper looper;
    looper.prepare (rate);

    looper.pressMain();
    runLooper (looper, constant (0.1f, 24000));
    looper.pressMain();

    const auto overdubs = Looper::maxLayerLanes + 2;

    for (int layer = 0; layer < overdubs; ++layer)
    {
        looper.pressOverdub();
        runLooper (looper, constant (0.01f * (float) (layer + 1), 2400));
        looper.pressMain();
        runLooper (looper, constant (0.0f, 640));
    }

    REQUIRE (looper.getNumLayerLanes() == Looper::maxLayerLanes);
    REQUIRE (looper.getNumFoldedLayers() == 3);
    REQUIRE (looper.getNumLayers() == overdubs + 1);

    // Undo still takes back the newest layer.
    looper.pressUndo();
    runLooper (looper, constant (0.0f, 4800));
    REQUIRE (looper.getNumLayers() == overdubs);
    REQUIRE (looper.getNumLayerLanes() == Looper::maxLayerLanes - 1);
}

namespace
{
// A loop of 0.25, with full-pass overdubs at the given levels.
void recordLayers (Looper& looper, std::initializer_list<float> overdubs)
{
    looper.prepare (rate);
    looper.pressMain();
    runLooper (looper, constant (0.25f, 24000));
    looper.pressMain();

    for (auto overdub : overdubs)
    {
        looper.pressOverdub();
        runLooper (looper, constant (overdub, 24000));
        looper.pressMain();
    }

    runLooper (looper, constant (0.0f, 640));
}

float loopPeak (Looper& looper)
{
    runLooper (looper, constant (0.0f, 4800));   // let any removed layer fade out
    return test::peak (runLooper (looper, constant (0.0f, 24000)));
}
}

TEST_CASE ("overdub is its own button")
{
    Looper looper;
    looper.prepare (rate);

    SECTION ("it does nothing before there is a loop")
    {
        looper.pressOverdub();
        runLooper (looper, constant (0.1f, 640));
        REQUIRE (looper.getState() == Looper::State::empty);
    }

    SECTION ("pressing Play again does not start an overdub")
    {
        looper.pressMain();
        runLooper (looper, constant (0.25f, 24000));
        looper.pressMain();
        looper.pressMain();
        runLooper (looper, constant (0.1f, 640));
        REQUIRE (looper.getState() == Looper::State::playing);
        REQUIRE (looper.getNumLayers() == 1);
    }

    SECTION ("during the first pass it closes the loop and starts overdubbing")
    {
        looper.pressMain();
        runLooper (looper, constant (0.25f, 24000));
        looper.pressOverdub();
        runLooper (looper, constant (0.1f, 640));
        REQUIRE (looper.getState() == Looper::State::overdubbing);
        REQUIRE (looper.getLength() == 24000);
        REQUIRE (looper.getNumLayers() == 2);
    }

    SECTION ("a first pass too short to keep is not overdubbed")
    {
        looper.pressMain();
        runLooper (looper, constant (0.25f, 640));
        looper.pressOverdub();
        runLooper (looper, constant (0.1f, 640));
        REQUIRE (looper.getState() == Looper::State::empty);
        REQUIRE (looper.getNumLayers() == 0);
    }

    SECTION ("after a stop it plays from the start and overdubs")
    {
        looper.pressMain();
        runLooper (looper, constant (0.25f, 24000));
        looper.pressStop();
        runLooper (looper, constant (0.0f, 640));
        looper.pressOverdub();
        runLooper (looper, constant (0.1f, 2400));
        REQUIRE (looper.getState() == Looper::State::overdubbing);
        REQUIRE (looper.getPosition() == 2400);
    }

    SECTION ("pressing it again keeps the layer")
    {
        looper.pressMain();
        runLooper (looper, constant (0.25f, 24000));
        looper.pressMain();
        looper.pressOverdub();
        runLooper (looper, constant (0.1f, 24000));
        looper.pressOverdub();
        runLooper (looper, constant (0.0f, 640));
        REQUIRE (looper.getState() == Looper::State::playing);
        REQUIRE (looper.getNumLayers() == 2);
    }
}

TEST_CASE ("any layer can be removed and the others keep playing")
{
    Looper looper;
    recordLayers (looper, { 0.1f, 0.05f });
    REQUIRE_THAT (loopPeak (looper), WithinAbs (0.40, 1.0e-5));

    SECTION ("the middle one")
    {
        const auto keptId = looper.getLayerId (2);
        looper.removeLayer (looper.getLayerId (1));
        REQUIRE_THAT (loopPeak (looper), WithinAbs (0.30, 1.0e-5));
        REQUIRE (looper.getNumLayers() == 2);
        REQUIRE (looper.getLayerId (1) == keptId);
    }

    SECTION ("the first recording")
    {
        looper.removeLayer (looper.getLayerId (0));
        REQUIRE_THAT (loopPeak (looper), WithinAbs (0.15, 1.0e-5));
        REQUIRE (looper.getNumLayers() == 2);
        REQUIRE (looper.getLength() == 24000);
    }

    SECTION ("while stopped")
    {
        looper.pressStop();
        runLooper (looper, constant (0.0f, 640));
        looper.removeLayer (looper.getLayerId (1));
        runLooper (looper, constant (0.0f, 640));
        REQUIRE (looper.getState() == Looper::State::stopped);
        looper.pressMain();
        REQUIRE_THAT (loopPeak (looper), WithinAbs (0.30, 1.0e-5));
    }

    SECTION ("undo goes back one layer at a time")
    {
        looper.pressUndo();
        REQUIRE_THAT (loopPeak (looper), WithinAbs (0.35, 1.0e-5));
        REQUIRE (looper.canUndo());
        looper.pressUndo();
        REQUIRE_THAT (loopPeak (looper), WithinAbs (0.25, 1.0e-5));
        REQUIRE_FALSE (looper.canUndo());
    }
}

TEST_CASE ("removing a layer fades it out instead of clicking")
{
    Looper looper;
    recordLayers (looper, { 0.5f });

    looper.removeLayer (looper.getLayerId (1));
    const auto played = runLooper (looper, constant (0.0f, 2400));

    for (size_t i = 1; i < played.size(); ++i)
        REQUIRE (std::abs (played[i] - played[i - 1]) < 0.01f);

    REQUIRE_THAT (played.back(), WithinAbs (0.25, 1.0e-5));
}

TEST_CASE ("removing the layer being recorded ends the overdub")
{
    Looper looper;
    recordLayers (looper, { 0.1f });

    looper.pressOverdub();
    runLooper (looper, constant (0.2f, 6000));
    REQUIRE (looper.getState() == Looper::State::overdubbing);

    looper.removeLayer (looper.getLayerId (2));
    runLooper (looper, constant (0.2f, 640));
    REQUIRE (looper.getState() == Looper::State::playing);
    REQUIRE (looper.getNumLayers() == 2);
    REQUIRE_THAT (loopPeak (looper), WithinAbs (0.35, 1.0e-5));
}

TEST_CASE ("the last layer stays, and an old layer id does nothing")
{
    Looper looper;
    recordLayers (looper, { 0.1f });

    const auto removedId = looper.getLayerId (1);
    looper.removeLayer (removedId);
    runLooper (looper, constant (0.0f, 4800));
    looper.removeLayer (removedId);
    runLooper (looper, constant (0.0f, 640));
    REQUIRE (looper.getNumLayers() == 1);

    looper.removeLayer (looper.getLayerId (0));
    REQUIRE_THAT (loopPeak (looper), WithinAbs (0.25, 1.0e-5));
    REQUIRE (looper.getNumLayers() == 1);
}

TEST_CASE ("a removed layer's memory is clean when it is used again")
{
    Looper looper;
    recordLayers (looper, { 0.5f });

    looper.removeLayer (looper.getLayerId (1));
    runLooper (looper, constant (0.0f, 4800));

    // A short overdub in the reused memory must not bring the old layer back.
    looper.pressOverdub();
    runLooper (looper, constant (0.1f, 2400));
    looper.pressMain();
    REQUIRE_THAT (loopPeak (looper), WithinAbs (0.35, 1.0e-5));
}

TEST_CASE ("folding the oldest overdub away does not change the sound")
{
    Looper looper;
    looper.prepare (rate);
    looper.pressMain();
    runLooper (looper, constant (0.1f, 24000));
    looper.pressMain();

    auto expected = 0.1f;

    for (int layer = 0; layer < Looper::maxLayerLanes + 2; ++layer)
    {
        looper.pressOverdub();
        runLooper (looper, constant (0.02f, 24000));
        looper.pressMain();
        runLooper (looper, constant (0.0f, 640));
        expected += 0.02f;
        REQUIRE_THAT (loopPeak (looper), WithinAbs (expected, 1.0e-4));
    }

    REQUIRE (looper.getNumFoldedLayers() == 3);
}

//==============================================================================
TEST_CASE ("the take recorder writes the sound and the dry guitar to WAV files")
{
    const auto folder = juce::File::createTempFile ("takes");
    REQUIRE (folder.createDirectory());

    {
        TakeRecorder recorder;
        recorder.prepare (rate, block);
        REQUIRE (recorder.start (folder, "Take").isEmpty());
        REQUIRE (recorder.isRecording());

        const auto dry = test::sine (110.0, 0.5f, rate, 48000);
        const auto left = constant (0.25f, 48000);
        const auto right = constant (-0.25f, 48000);

        for (int offset = 0; offset < 48000; offset += block)
            recorder.write (dry.data() + offset, left.data() + offset, right.data() + offset, block);

        REQUIRE_THAT (recorder.getSecondsRecorded(), WithinAbs (1.0, 0.01));

        const auto take = recorder.stop();
        REQUIRE_FALSE (recorder.isRecording());

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> processed (formats.createReaderFor (take.processed));
        std::unique_ptr<juce::AudioFormatReader> dryReader (formats.createReaderFor (take.dry));
        REQUIRE (processed != nullptr);
        REQUIRE (dryReader != nullptr);

        REQUIRE (processed->numChannels == 2);
        REQUIRE (dryReader->numChannels == 1);
        REQUIRE (processed->lengthInSamples == 48000);
        REQUIRE (dryReader->lengthInSamples == 48000);
        REQUIRE (juce::exactlyEqual (processed->sampleRate, rate));

        juce::AudioBuffer<float> sound (2, 48000), guitar (1, 48000);
        processed->read (&sound, 0, 48000, 0, true, true);
        dryReader->read (&guitar, 0, 48000, 0, true, true);

        REQUIRE_THAT (sound.getSample (0, 1000), WithinAbs (0.25, 1.0e-4));
        REQUIRE_THAT (sound.getSample (1, 1000), WithinAbs (-0.25, 1.0e-4));

        for (int i = 0; i < 48000; i += 997)
            REQUIRE_THAT (guitar.getSample (0, i), WithinAbs (dry[(size_t) i], 1.0e-4));
    }

    folder.deleteRecursively();
}

TEST_CASE ("a new take never overwrites an old one")
{
    const auto folder = juce::File::createTempFile ("takes");
    REQUIRE (folder.createDirectory());

    {
        TakeRecorder recorder;
        recorder.prepare (rate, block);

        REQUIRE (recorder.start (folder, "Take").isEmpty());
        const auto first = recorder.stop();
        REQUIRE (recorder.start (folder, "Take").isEmpty());
        const auto second = recorder.stop();

        REQUIRE (first.processed != second.processed);
        REQUIRE (first.dry != second.dry);
        REQUIRE (second.processed.existsAsFile());
    }

    folder.deleteRecursively();
}

TEST_CASE ("a take stops being written if the sample rate changes")
{
    const auto folder = juce::File::createTempFile ("takes");
    REQUIRE (folder.createDirectory());

    {
        TakeRecorder recorder;
        recorder.prepare (rate, block);
        REQUIRE (recorder.start (folder, "Take").isEmpty());

        const auto signal = constant (0.1f, block);
        recorder.write (signal.data(), signal.data(), signal.data(), block);

        recorder.prepare (44100.0, block);
        recorder.write (signal.data(), signal.data(), signal.data(), block);

        REQUIRE (recorder.wasInterrupted());
        REQUIRE_THAT (recorder.getSecondsRecorded(), WithinAbs (block / rate, 1.0e-9));
        recorder.stop();
    }

    folder.deleteRecursively();
}

//==============================================================================
TEST_CASE ("practice tools keep the output inside full scale and go silent when muted")
{
    PracticeTools tools;
    tools.prepare (rate, block);

    auto& looper = tools.getLooper();
    looper.pressMain();

    std::vector<float> left (24000, 0.9f), right (24000, 0.9f);

    for (size_t offset = 0; offset < left.size(); offset += block)
        tools.process (nullptr, left.data() + offset, right.data() + offset, block, false);

    looper.pressMain();
    std::fill (left.begin(), left.end(), 0.9f);
    std::fill (right.begin(), right.end(), 0.9f);

    for (size_t offset = 0; offset < left.size(); offset += block)
        tools.process (nullptr, left.data() + offset, right.data() + offset, block, false);

    // Live sound plus the loop would be 1.8.
    REQUIRE (test::peak (left) <= 1.0f);
    REQUIRE (test::peak (right) <= 1.0f);
    REQUIRE_THAT (left[12000], WithinAbs (1.0, 1.0e-6));

    tools.getMetronome().setRunning (true);

    for (size_t offset = 0; offset < left.size(); offset += block)
        tools.process (nullptr, left.data() + offset, right.data() + offset, block, true);

    REQUIRE (juce::exactlyEqual (test::peak (left), 0.0f));
    REQUIRE (juce::exactlyEqual (test::peak (right), 0.0f));
}
