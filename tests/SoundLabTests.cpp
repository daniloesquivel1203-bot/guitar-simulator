#include "TestSignals.h"
#include "engine/SignalChain.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <map>

/** Listening-style checks: whole-chain renders of a guitar-like riff, used to
    make sure that where a pedal sits on the board changes the sound the way
    it would on a real board.

    Two hidden tests help with tuning by ear:
      GuitarSimulatorTests "[soundlab]"   prints a level and order report
      GuitarSimulatorTests "[listen]"     writes WAV files of each comparison
                                          to the folder in GS_LISTEN_DIR
*/

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 64;

struct Setup
{
    std::vector<const char*> before, after;
    std::map<juce::String, float> values;   // parameter id to value
    int voicing = 0;                        // the amp's voicing
    float ampGain = 4.0f;
};

/** Plays the riff through a chain with the given pedals, all switched on. */
std::vector<float> render (const Setup& setup, const std::vector<float>& riff)
{
    SignalChain chain;
    Pedalboard::PedalList before, after;

    const auto makeList = [] (const std::vector<const char*>& keys, Pedalboard::PedalList& list)
    {
        for (const auto* key : keys)
        {
            const auto* type = findPedalType (key);
            REQUIRE (type != nullptr);
            auto pedal = std::make_shared<Pedal> (*type);
            pedal->getEffect().enabled.set (1.0f);
            list.push_back (pedal);
        }
    };

    makeList (setup.before, before);
    makeList (setup.after, after);
    chain.getBoard().setPedals (before, after);

    chain.getAmp().voicing.set ((float) setup.voicing);
    chain.getAmp().gain.set (setup.ampGain);

    for (auto* parameter : chain.getAllParameters())
        if (auto found = setup.values.find (parameter->id); found != setup.values.end())
            parameter->set (found->second);

    chain.prepare (sampleRate, blockSize);

    std::vector<float> out (riff.size()), right (riff.size());

    for (size_t offset = 0; offset < riff.size(); offset += blockSize)
    {
        const auto n = (int) juce::jmin ((size_t) blockSize, riff.size() - offset);
        chain.process (riff.data() + offset, out.data() + offset, right.data() + offset, n);
    }

    return out;
}

struct Comparison
{
    const char* name;
    Setup first, second;
    double minToneDb, minDynamicsDb;   // how different the two orders must sound at least
};

std::vector<Comparison> getComparisons()
{
    return {
        { "compressor-before-vs-after-overdrive",
          { { "comp", "od" }, {}, {} }, { { "od", "comp" }, {}, {} }, 0.0, 1.0 },
        { "wah-before-vs-after-fuzz",
          { { "wah", "fuzz" }, {}, {} }, { { "fuzz", "wah" }, {}, {} }, 1.0, 0.0 },
        { "fuzz-first-vs-after-a-buffer",
          { { "fuzz", "boost" }, {}, { { "boost.gain", 0.0f } } },
          { { "boost", "fuzz" }, {}, { { "boost.gain", 0.0f } } }, 1.0, 0.0 },
        { "overdrive-into-fuzz-vs-fuzz-into-overdrive",
          { { "od", "fuzz" }, {}, {} }, { { "fuzz", "od" }, {}, {} }, 1.0, 0.0 },
        { "drive-before-vs-after-the-amp",
          { { "od" }, {}, {} }, { {}, { "od" }, {} }, 0.5, 0.0 },
        { "auto-wah-before-vs-after-distortion",
          { { "envfilter", "dist" }, {}, {} }, { { "dist", "envfilter" }, {}, {} }, 1.0, 0.0 },
        { "gate-before-vs-after-distortion",
          { { "gate", "dist" }, {}, {} }, { { "dist", "gate" }, {}, {} }, 0.0, 0.0 },
    };
}

/** Writes a mono WAV file, for listening to a comparison. */
void writeWav (const juce::File& file, const std::vector<float>& samples)
{
    file.deleteFile();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    REQUIRE (stream != nullptr);

    auto writer = format.createWriterFor (stream,
                                          juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate)
                                                                          .withNumChannels (1)
                                                                          .withBitsPerSample (24));
    REQUIRE (writer != nullptr);

    const float* channels[] = { samples.data() };
    writer->writeFromFloatArrays (channels, 1, (int) samples.size());
}
}

//==============================================================================
TEST_CASE ("the order of the pedals changes the sound the way it does on a real board")
{
    const auto riff = test::guitarRiff (sampleRate);

    for (const auto& comparison : getComparisons())
    {
        if (comparison.minToneDb <= 0.0 && comparison.minDynamicsDb <= 0.0)
            continue;

        INFO (comparison.name);
        const auto a = render (comparison.first, riff);
        const auto b = render (comparison.second, riff);
        REQUIRE (test::allFinite (a));
        REQUIRE (test::allFinite (b));

        CHECK (test::toneDifferenceDb (a, b, sampleRate) >= comparison.minToneDb);
        CHECK (test::dynamicsDifferenceDb (a, b, sampleRate) >= comparison.minDynamicsDb);
    }
}

TEST_CASE ("a fuzz straight from the guitar sounds fuller and darker than one after another pedal")
{
    const auto riff = test::guitarRiff (sampleRate);
    const Setup first { { "fuzz", "boost" }, {}, { { "boost.gain", 0.0f } } };
    const Setup buffered { { "boost", "fuzz" }, {}, { { "boost.gain", 0.0f } } };

    const auto a = test::bandLevelsDb (render (first, riff), sampleRate);
    const auto b = test::bandLevelsDb (render (buffered, riff), sampleRate);

    // Band 0 is 63 Hz; each band is a third of an octave. Compare the share of
    // treble (2.5 kHz and up) against the low mids (160 to 400 Hz).
    const auto tilt = [] (const std::vector<double>& bands)
    {
        double lowMid = 0.0, treble = 0.0;
        for (size_t i = 4; i <= 8; ++i)    lowMid += bands[i];
        for (size_t i = 16; i <= 20; ++i)  treble += bands[i];
        return (treble - lowMid) / 5.0;
    };

    CHECK (tilt (a) < tilt (b) - 1.0);
}

TEST_CASE ("a gate in front of a distortion silences the hiss between notes; one after it cannot")
{
    // The riff ends with a moment of nothing but pickup hum and hiss.
    const auto riff = test::guitarRiff (sampleRate);
    const auto tailStart = riff.size() - (size_t) (0.4 * sampleRate);

    const auto tailLevel = [tailStart] (const std::vector<float>& signal)
    {
        return juce::Decibels::gainToDecibels ((float) test::rms (signal, tailStart), -200.0f);
    };

    const auto gateFirst = render ({ { "gate", "dist" }, {}, {} }, riff);
    const auto gateLast = render ({ { "dist", "gate" }, {}, {} }, riff);

    CHECK (tailLevel (gateFirst) < -90.0f);
    CHECK (tailLevel (gateLast) > tailLevel (gateFirst) + 30.0f);
}

//==============================================================================
TEST_CASE ("sound lab report", "[.][soundlab]")
{
    const auto riff = test::guitarRiff (sampleRate);

    std::printf ("\nOrder comparisons (tone and dynamics difference, dB):\n");

    for (const auto& comparison : getComparisons())
    {
        const auto a = render (comparison.first, riff);
        const auto b = render (comparison.second, riff);
        std::printf ("  %-46s tone %5.2f  dynamics %5.2f\n", comparison.name,
                     test::toneDifferenceDb (a, b, sampleRate), test::dynamicsDifferenceDb (a, b, sampleRate));
    }

    // How much louder or quieter each pedal makes the riff at its default
    // settings, straight into the amp input (no amp), and after the amp.
    std::printf ("\nPedal level change at default settings (dB), riff peak -10 dBFS:\n");

    for (const auto& type : getPedalTypes())
    {
        auto pedal = std::make_unique<Pedal> (type);
        pedal->getEffect().enabled.set (1.0f);
        pedal->prepare (sampleRate, blockSize);
        pedal->setFedByGuitar (true);

        auto signal = riff;

        for (size_t offset = 0; offset < signal.size(); offset += blockSize)
            pedal->processMono (signal.data() + offset, (int) juce::jmin ((size_t) blockSize, signal.size() - offset));

        const auto change = juce::Decibels::gainToDecibels ((float) (test::rms (signal) / test::rms (riff)));
        const auto peakChange = juce::Decibels::gainToDecibels (test::peak (signal) / test::peak (riff));
        std::printf ("  %-12s %-20s rms %+6.1f  peak %+6.1f\n", type.key, pedal->getEffect().name, change, peakChange);
    }
}

TEST_CASE ("write comparison clips", "[.][listen]")
{
    const auto* folderName = std::getenv ("GS_LISTEN_DIR");
    REQUIRE (folderName != nullptr);

    const juce::File folder (juce::String::fromUTF8 (folderName));
    folder.createDirectory();

    const auto riff = test::guitarRiff (sampleRate);
    writeWav (folder.getChildFile ("00-dry-riff.wav"), riff);

    int number = 1;

    for (const auto& comparison : getComparisons())
    {
        const auto prefix = juce::String (number++).paddedLeft ('0', 2) + "-" + comparison.name;
        writeWav (folder.getChildFile (prefix + "-A.wav"), render (comparison.first, riff));
        writeWav (folder.getChildFile (prefix + "-B.wav"), render (comparison.second, riff));
    }
}
