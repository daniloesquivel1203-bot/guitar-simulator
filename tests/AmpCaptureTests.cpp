#include "engine/AmpCapture.h"
#include "engine/Presets.h"
#include "engine/RateConverter.h"
#include "TestSignals.h"

#include <NAM/dsp.h>
#include <NAM/get_dsp.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <random>

using Catch::Matchers::WithinAbs;

namespace
{
/** The example captures that come with NeuralAmpModelerCore. */
juce::File exampleCapture (const char* name)
{
    return juce::File (NAM_EXAMPLE_MODELS_DIR).getChildFile (name);
}

/** A guitar-like test signal: a decaying chord-ish tone with some noise. */
std::vector<float> guitarLike (double sampleRate, int numSamples)
{
    std::vector<float> out ((size_t) numSamples);
    std::mt19937 random (7);
    std::uniform_real_distribution<float> hiss (-0.005f, 0.005f);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto t = i / sampleRate;
        const auto envelope = std::exp (-1.5 * std::fmod (t, 1.0));
        out[(size_t) i] = (float) (0.3 * envelope * (std::sin (juce::MathConstants<double>::twoPi * 110.0 * t)
                                                     + 0.5 * std::sin (juce::MathConstants<double>::twoPi * 165.0 * t)
                                                     + 0.3 * std::sin (juce::MathConstants<double>::twoPi * 220.0 * t)))
                          + hiss (random);
    }

    return out;
}

/** Runs a signal through the capture in blocks and returns the result. */
std::vector<float> run (AmpCapture& capture, std::vector<float> signal, int blockSize)
{
    for (size_t offset = 0; offset < signal.size(); offset += (size_t) blockSize)
        capture.process (signal.data() + offset, (int) juce::jmin ((size_t) blockSize, signal.size() - offset));

    return signal;
}

bool allFinite (const std::vector<float>& signal)
{
    return std::all_of (signal.begin(), signal.end(), [] (float x) { return std::isfinite (x); });
}

juce::File writeTempFile (const juce::String& content, const juce::String& extension)
{
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("gs-capture-test-" + juce::Uuid().toString() + extension);
    file.replaceWithText (content);
    return file;
}
}

//==============================================================================
TEST_CASE ("the rate converter keeps a tone intact through 48 kHz to 44.1 kHz and back")
{
    constexpr double deviceRate = 48000.0, captureRate = 44100.0;
    constexpr int blockSize = 64, numBlocks = 400;

    RateConverter down, up;
    down.prepare (deviceRate, captureRate, blockSize);
    up.prepare (captureRate, deviceRate, down.getMaxOutputFor (blockSize), 16);

    const auto input = test::sine (1000.0, 0.5f, deviceRate, blockSize * numBlocks);
    std::vector<float> output (input.size()), middle ((size_t) down.getMaxOutputFor (blockSize));

    int shortBlocks = 0;

    for (int block = 0; block < numBlocks; ++block)
    {
        down.push (input.data() + block * blockSize, blockSize);
        const auto middleCount = down.pull (middle.data(), (int) middle.size());
        up.push (middle.data(), middleCount);

        if (up.pull (output.data() + block * blockSize, blockSize) < blockSize)
            ++shortBlocks;
    }

    // Every block came back full, after the first few while it fills.
    CHECK (shortBlocks <= 1);

    // Fit the best 1 kHz sine to the output. What is left over is distortion
    // and noise added by the round trip.
    const auto from = (size_t) 4000;
    double ss = 0.0, sc = 0.0, cc = 0.0, ys = 0.0, yc = 0.0;

    for (size_t i = from; i < output.size(); ++i)
    {
        const auto phase = juce::MathConstants<double>::twoPi * 1000.0 * (double) i / deviceRate;
        const auto s = std::sin (phase), c = std::cos (phase);
        ss += s * s; sc += s * c; cc += c * c;
        ys += output[i] * s; yc += output[i] * c;
    }

    const auto det = ss * cc - sc * sc;
    const auto a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
    double residual = 0.0;

    for (size_t i = from; i < output.size(); ++i)
    {
        const auto phase = juce::MathConstants<double>::twoPi * 1000.0 * (double) i / deviceRate;
        const auto e = output[i] - (a * std::sin (phase) + b * std::cos (phase));
        residual += e * e;
    }

    const auto amplitude = std::sqrt (a * a + b * b);
    const auto residualRms = std::sqrt (residual / (double) (output.size() - from));

    CHECK_THAT (amplitude, WithinAbs (0.5, 0.005));             // level kept within 0.1 dB
    CHECK (residualRms / (amplitude / std::sqrt (2.0)) < 0.003); // leftovers 50 dB below the tone
}

TEST_CASE ("a capture file loads and reports what it is")
{
    AmpCapture capture;
    capture.prepare (48000.0, 256);

    REQUIRE (capture.loadFile (exampleCapture ("wavenet.nam")).isEmpty());
    CHECK (capture.isLoaded());
    CHECK (capture.getInfo().name == "wavenet");
    CHECK (capture.getInfo().gearType == "amp");
    CHECK_FALSE (capture.getInfo().includesCabinet());
    CHECK (capture.getInfo().hasLoudness);
    CHECK_THAT (capture.getInfo().sampleRate, WithinAbs (48000.0, 0.1));

    // A capture is only used once it is switched on.
    CHECK_FALSE (capture.isInUse());
    capture.active.set (1.0f);
    CHECK (capture.isInUse());

    capture.unload();
    CHECK_FALSE (capture.isLoaded());
    CHECK_FALSE (capture.isInUse());
}

TEST_CASE ("files that are not captures are refused and the current capture stays")
{
    AmpCapture capture;
    capture.prepare (48000.0, 256);
    REQUIRE (capture.loadFile (exampleCapture ("lstm.nam")).isEmpty());

    const auto notJson = writeTempFile ("this is not a capture", ".nam");
    const auto wrongJson = writeTempFile ("{\"version\": \"0.5.4\", \"architecture\": \"Nonsense\", \"config\": {}, \"weights\": []}", ".nam");

    CHECK (capture.loadFile (notJson).isNotEmpty());
    CHECK (capture.loadFile (wrongJson).isNotEmpty());
    CHECK (capture.loadFile (juce::File ("/no/such/folder/missing.nam")).isNotEmpty());

    CHECK (capture.isLoaded());
    CHECK (capture.getInfo().name == "lstm");

    notJson.deleteFile();
    wrongJson.deleteFile();
}

TEST_CASE ("a capture sounds exactly as the NAM library plays it")
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 128;
    const auto file = exampleCapture ("wavenet.nam");

    AmpCapture capture;
    capture.prepare (sampleRate, blockSize);
    REQUIRE (capture.loadFile (file).isEmpty());
    capture.active.set (1.0f);

    // The reference: the library on its own, levelled the same way.
    auto reference = nam::get_dsp (std::filesystem::path (file.getFullPathName().toStdString()));
    REQUIRE (reference != nullptr);
    reference->Reset (sampleRate, blockSize);
    const auto levelling = (float) juce::Decibels::decibelsToGain (AmpCapture::targetLoudnessDb - reference->GetLoudness());

    const auto input = guitarLike (sampleRate, (int) sampleRate);
    const auto output = run (capture, input, blockSize);

    std::vector<float> expected (input.size());
    auto in = input;   // the library takes a non-const pointer

    for (size_t offset = 0; offset < input.size(); offset += blockSize)
    {
        const auto count = (int) juce::jmin ((size_t) blockSize, input.size() - offset);
        float* inputs[] = { in.data() + offset };
        float* outputs[] = { expected.data() + offset };
        reference->process (inputs, outputs, count);
    }

    double worst = 0.0;

    // Skip the short fade-in the app adds when a capture is swapped in.
    for (size_t i = 4800; i < input.size(); ++i)
        worst = juce::jmax (worst, (double) std::abs (output[i] - expected[i] * levelling));

    CHECK (test::rms (output, 4800) > 0.001);
    CHECK (worst < 1.0e-4);
}

TEST_CASE ("a capture made at 48 kHz plays at the right pitch and level on a 44.1 kHz device")
{
    const auto file = exampleCapture ("wavenet.nam");

    const auto loudnessAt = [&file] (double sampleRate)
    {
        AmpCapture capture;
        capture.prepare (sampleRate, 64);
        REQUIRE (capture.loadFile (file).isEmpty());
        const auto output = run (capture, test::sine (220.0, 0.3f, sampleRate, (int) sampleRate), 64);
        REQUIRE (allFinite (output));
        return output;
    };

    const auto native = loudnessAt (48000.0);
    const auto converted = loudnessAt (44100.0);

    const auto nativeDb = juce::Decibels::gainToDecibels (test::rms (native, 9600));
    const auto convertedDb = juce::Decibels::gainToDecibels (test::rms (converted, 8820));
    CHECK_THAT (convertedDb, WithinAbs (nativeDb, 1.0));

    // Count the upward zero crossings over one second: about 220 means the pitch is right.
    int crossings = 0;

    for (size_t i = 8820 + 1; i < converted.size(); ++i)
        if (converted[i - 1] < 0.0f && converted[i] >= 0.0f)
            ++crossings;

    const auto seconds = (double) (converted.size() - 8821) / 44100.0;
    CHECK_THAT (crossings / seconds, WithinAbs (220.0, 15.0));
}

TEST_CASE ("captures are levelled by the loudness they report")
{
    // The same capture, but claiming to be 10 dB quieter, should be turned up by 10 dB.
    auto json = juce::JSON::parse (exampleCapture ("wavenet.nam"));
    const auto loudness = (double) json["metadata"]["loudness"];
    json["metadata"].getDynamicObject()->setProperty ("loudness", loudness - 10.0);
    const auto quieter = writeTempFile (juce::JSON::toString (json, true), ".nam");

    const auto levelOf = [] (const juce::File& file)
    {
        AmpCapture capture;
        capture.prepare (48000.0, 256);
        REQUIRE (capture.loadFile (file).isEmpty());
        return juce::Decibels::gainToDecibels (test::rms (run (capture, guitarLike (48000.0, 48000), 256), 4800));
    };

    CHECK_THAT (levelOf (quieter) - levelOf (exampleCapture ("wavenet.nam")), WithinAbs (10.0, 0.05));
    quieter.deleteFile();
}

TEST_CASE ("the input, tone and output knobs change the capture's sound")
{
    const auto play = [] (std::function<void (AmpCapture&)> setUp)
    {
        AmpCapture capture;
        setUp (capture);
        capture.prepare (48000.0, 256);
        REQUIRE (capture.loadFile (exampleCapture ("wavenet.nam")).isEmpty());
        return test::rms (run (capture, guitarLike (48000.0, 24000), 256), 4800);
    };

    const auto plain = play ([] (AmpCapture&) {});
    const auto quieter = play ([] (AmpCapture& c) { c.output.set (-12.0f); });
    const auto bassCut = play ([] (AmpCapture& c) { c.bass.set (0.0f); });

    CHECK_THAT (juce::Decibels::gainToDecibels (quieter / plain), WithinAbs (-12.0, 0.2));
    CHECK (bassCut < plain);
}

TEST_CASE ("the chain uses the capture only when it is loaded and switched on")
{
    const auto input = guitarLike (48000.0, 24000);

    const auto play = [&input] (SignalChain& chain)
    {
        chain.prepare (48000.0, 256);
        std::vector<float> left (input.size()), right (input.size());
        chain.process (input.data(), left.data(), right.data(), (int) input.size());
        return left;
    };

    SignalChain builtIn;
    const auto builtInOutput = play (builtIn);

    SignalChain switchedOff;
    REQUIRE (switchedOff.getCapture().loadFile (exampleCapture ("wavenet.nam")).isEmpty());
    CHECK (play (switchedOff) == builtInOutput);

    SignalChain withCapture;
    REQUIRE (withCapture.getCapture().loadFile (exampleCapture ("wavenet.nam")).isEmpty());
    withCapture.getCapture().active.set (1.0f);
    const auto captureOutput = play (withCapture);
    CHECK (captureOutput != builtInOutput);
    CHECK (allFinite (captureOutput));

    // Switched on without a file, the built-in amp keeps playing.
    SignalChain noFile;
    noFile.getCapture().active.set (1.0f);
    CHECK (play (noFile) == builtInOutput);
}

TEST_CASE ("presets remember the capture file")
{
    SignalChain source;
    REQUIRE (source.getCapture().loadFile (exampleCapture ("wavenet.nam")).isEmpty());
    source.getCapture().active.set (1.0f);
    source.getCapture().input.set (6.0f);

    const auto preset = PresetManager::capture (source, "With capture");

    SignalChain target;
    REQUIRE (PresetManager::apply (target, preset));
    CHECK (target.getCapture().isInUse());
    CHECK (target.getCapture().getLoadedFile() == exampleCapture ("wavenet.nam"));
    CHECK_THAT (target.getCapture().input.get(), WithinAbs (6.0f, 0.001f));

    // A preset saved without a capture goes back to the built-in amp.
    SignalChain plain;
    REQUIRE (PresetManager::apply (target, PresetManager::capture (plain, "Plain")));
    CHECK_FALSE (target.getCapture().isLoaded());
    CHECK_FALSE (target.getCapture().isInUse());

    // So does one whose capture file has been moved or deleted.
    auto moved = preset;
    moved.getDynamicObject()->setProperty ("ampCapture", "/no/such/folder/gone.nam");
    REQUIRE (PresetManager::apply (target, moved));
    CHECK_FALSE (target.getCapture().isInUse());
}

// Hidden by default because timing depends on the machine. Run it with:
//   GuitarSimulatorTests "[performance]"
TEST_CASE ("a full-size capture uses a modest share of one processor core", "[.][performance]")
{
    for (const auto* name : { "wavenet_a1_standard.nam", "A2.nam", "lstm.nam" })
    {
        for (const auto sampleRate : { 48000.0, 44100.0 })
        {
            constexpr int blockSize = 64;
            AmpCapture capture;
            capture.prepare (sampleRate, blockSize);
            REQUIRE (capture.loadFile (exampleCapture (name)).isEmpty());

            auto signal = guitarLike (sampleRate, (int) (4 * sampleRate));
            const auto start = std::chrono::steady_clock::now();
            run (capture, signal, blockSize);
            const auto taken = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();

            std::printf ("%-26s at %5.0f Hz: %5.2f%% of one core\n", name, sampleRate, 100.0 * taken / 4.0);
        }
    }

    SUCCEED();
}
