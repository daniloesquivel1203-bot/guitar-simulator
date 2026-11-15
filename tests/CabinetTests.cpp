#include "TestSignals.h"
#include "engine/Cabinet.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace
{
constexpr int blockSize = 128;
constexpr double sampleRate = 48000.0;

double responseAt (Cabinet& cabinet, double hz)
{
    cabinet.prepare (sampleRate, blockSize);
    const auto out = test::render (cabinet, test::sine (hz, 0.25f, sampleRate, 48000), blockSize);
    return test::amplitudeAt (out, hz, sampleRate, out.size() / 2) / 0.25;
}
}

TEST_CASE ("there are three built-in cabinets with distinct names")
{
    auto names = Cabinet::getBuiltInNames();
    REQUIRE (names.size() == 3);
    names.removeDuplicates (false);
    REQUIRE (names.size() == 3);
}

TEST_CASE ("built-in impulse responses are finite, non-silent and end at zero")
{
    const auto index = GENERATE (0, 1, 2);
    const auto rate = GENERATE (44100.0, 48000.0, 96000.0);

    const auto impulse = Cabinet::makeBuiltInImpulse (index, rate);
    REQUIRE (impulse.getNumChannels() == 1);
    REQUIRE (impulse.getNumSamples() > 1000);

    const std::vector<float> samples (impulse.getReadPointer (0), impulse.getReadPointer (0) + impulse.getNumSamples());
    REQUIRE (test::allFinite (samples));
    REQUIRE (test::peak (samples) > 0.01f);
    REQUIRE (std::abs (samples.back()) < 1.0e-6f);
}

TEST_CASE ("every built-in cabinet removes fizz and rumble like a guitar speaker")
{
    const auto index = GENERATE (0, 1, 2);

    Cabinet cabinet;
    cabinet.selectBuiltIn (index);

    const auto mids = responseAt (cabinet, 1000.0);
    REQUIRE (mids > 0.0);
    REQUIRE (responseAt (cabinet, 12000.0) < 0.05 * mids);
    REQUIRE (responseAt (cabinet, 30.0) < 0.3 * mids);
    REQUIRE (responseAt (cabinet, 2500.0) > 0.5 * mids);
}

TEST_CASE ("the built-in cabinets differ from each other")
{
    Cabinet a, b;
    a.selectBuiltIn (0);
    b.selectBuiltIn (2);

    const auto ratioA = responseAt (a, 3400.0) / responseAt (a, 450.0);
    const auto ratioB = responseAt (b, 3400.0) / responseAt (b, 450.0);
    REQUIRE (std::abs (ratioA - ratioB) > 0.1 * ratioA);
}

TEST_CASE ("the cut filters narrow the response")
{
    Cabinet open, narrowed;
    narrowed.lowCut.set (300.0f);
    narrowed.highCut.set (2000.0f);

    REQUIRE (responseAt (narrowed, 100.0) < 0.3 * responseAt (open, 100.0));
    REQUIRE (responseAt (narrowed, 4000.0) < 0.5 * responseAt (open, 4000.0));
}

TEST_CASE ("a bypassed cabinet passes the signal through untouched")
{
    Cabinet cabinet;
    cabinet.enabled.set (0.0f);
    cabinet.prepare (sampleRate, blockSize);

    const auto in = test::sine (440.0, 0.3f, sampleRate, 4800);
    REQUIRE (test::render (cabinet, in, blockSize) == in);
}

TEST_CASE ("an impulse response file can be loaded and is used")
{
    // Write a file whose response is a plain 6 dB drop, which no built-in cabinet has.
    const auto file = juce::File::createTempFile (".wav");
    {
        juce::AudioBuffer<float> impulse (1, 64);
        impulse.clear();
        impulse.setSample (0, 0, 0.5f);

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
        REQUIRE (stream != nullptr);

        const auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (sampleRate)
                                                                                         .withNumChannels (1)
                                                                                         .withBitsPerSample (24));
        REQUIRE (writer != nullptr);
        REQUIRE (writer->writeFromAudioSampleBuffer (impulse, 0, impulse.getNumSamples()));
    }

    Cabinet cabinet;
    cabinet.lowCut.set (20.0f);
    cabinet.highCut.set (16000.0f);

    REQUIRE (cabinet.loadFile (file).isEmpty());
    REQUIRE (cabinet.getBuiltInIndex() == -1);
    REQUIRE (cabinet.getCurrentName() == file.getFileNameWithoutExtension());

    // A flat response, unlike a speaker: treble passes as strongly as the mids.
    const auto mids = responseAt (cabinet, 1000.0);
    REQUIRE (mids > 0.0);
    REQUIRE (responseAt (cabinet, 6000.0) > 0.8 * mids);

    // The choice survives being saved and restored.
    Cabinet restored;
    restored.restoreFromDescription (cabinet.getSourceDescription());
    REQUIRE (restored.getLoadedFile() == file);

    file.deleteFile();
}

TEST_CASE ("a bad impulse response file is rejected and the current cabinet is kept")
{
    Cabinet cabinet;
    cabinet.selectBuiltIn (1);

    REQUIRE (cabinet.loadFile (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                   .getChildFile ("does-not-exist.wav")).isNotEmpty());

    const auto notAudio = juce::File::createTempFile (".wav");
    notAudio.replaceWithText ("this is not audio");
    REQUIRE (cabinet.loadFile (notAudio).isNotEmpty());
    notAudio.deleteFile();

    REQUIRE (cabinet.getBuiltInIndex() == 1);
}

TEST_CASE ("a saved cabinet whose file has gone missing falls back to a built-in one")
{
    Cabinet cabinet;
    cabinet.restoreFromDescription ("file:/no/such/folder/cab.wav");
    REQUIRE (cabinet.getBuiltInIndex() == 0);

    cabinet.restoreFromDescription ("builtin:2");
    REQUIRE (cabinet.getBuiltInIndex() == 2);
}
