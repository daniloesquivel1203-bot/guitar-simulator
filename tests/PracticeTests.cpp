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
    looper.pressMain();
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
    looper.pressMain();   // straight into overdub
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
