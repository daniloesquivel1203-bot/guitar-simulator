#include "TestSignals.h"
#include "engine/AmpModel.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace
{
constexpr int blockSize = 128;
constexpr double noteHz = 200.0;   // divides evenly into every sample rate used below

/** One second of a steady note through the amp. Measurements skip the first half. */
std::vector<float> playNote (AmpModel& amp, double sampleRate, float amplitude = 0.2f)
{
    amp.prepare (sampleRate, blockSize);
    return test::render (amp, test::sine (noteHz, amplitude, sampleRate, (int) sampleRate), blockSize);
}

double distortionOf (AmpModel::Voicing voicing, float gain, double sampleRate = 48000.0)
{
    AmpModel amp;
    amp.voicing.set ((float) voicing);
    amp.gain.set (gain);
    const auto out = playNote (amp, sampleRate);
    return test::distortion (out, noteHz, sampleRate, out.size() / 2);
}
}

TEST_CASE ("the amp turns silence into silence for every voicing")
{
    const auto voicing = GENERATE (AmpModel::clean, AmpModel::crunch, AmpModel::lead);

    AmpModel amp;
    amp.voicing.set ((float) voicing);
    amp.gain.set (10.0f);
    amp.master.set (10.0f);
    amp.prepare (48000.0, blockSize);

    const auto out = test::render (amp, std::vector<float> (48000, 0.0f), blockSize);
    REQUIRE (test::peak (out) < 1.0e-6f);
}

TEST_CASE ("the amp output stays finite and bounded at extreme settings")
{
    const auto voicing = GENERATE (AmpModel::clean, AmpModel::crunch, AmpModel::lead);
    const auto sampleRate = GENERATE (44100.0, 48000.0, 96000.0);

    AmpModel amp;
    amp.voicing.set ((float) voicing);

    for (auto* parameter : { &amp.gain, &amp.bass, &amp.mid, &amp.treble, &amp.presence, &amp.master })
        parameter->set (10.0f);

    const auto out = playNote (amp, sampleRate, 1.0f);

    REQUIRE (test::allFinite (out));
    REQUIRE (test::peak (out) < 30.0f);
}

TEST_CASE ("the clean voicing at low gain is close to undistorted")
{
    REQUIRE (distortionOf (AmpModel::clean, 1.0f) < 0.05);
}

TEST_CASE ("turning up the gain adds distortion")
{
    const auto voicing = GENERATE (AmpModel::clean, AmpModel::crunch, AmpModel::lead);

    const auto low = distortionOf (voicing, 2.0f);
    const auto mid = distortionOf (voicing, 6.0f);
    const auto high = distortionOf (voicing, 10.0f);

    REQUIRE (mid > low);
    REQUIRE (high > mid);
}

TEST_CASE ("each voicing is more distorted than the one before at the same gain")
{
    const auto cleanAmount = distortionOf (AmpModel::clean, 6.0f);
    const auto crunchAmount = distortionOf (AmpModel::crunch, 6.0f);
    const auto leadAmount = distortionOf (AmpModel::lead, 6.0f);

    REQUIRE (crunchAmount > cleanAmount);
    REQUIRE (leadAmount > crunchAmount);
    REQUIRE (leadAmount > 0.3);
}

TEST_CASE ("the voicings sound the same at different sample rates")
{
    const auto voicing = GENERATE (AmpModel::clean, AmpModel::crunch, AmpModel::lead);

    const auto at48 = distortionOf (voicing, 6.0f, 48000.0);
    const auto at96 = distortionOf (voicing, 6.0f, 96000.0);

    REQUIRE (std::abs (at48 - at96) < 0.05);
}

TEST_CASE ("high gain distortion does not alias back into the audible range")
{
    // A 5 kHz note has harmonics at 10, 15, 20 kHz and beyond. Without
    // oversampling the higher ones fold back to frequencies such as 1 kHz that
    // are not harmonics of the note. Measure how much lands there.
    const double sampleRate = 48000.0;
    const double hz = 5000.0;

    AmpModel amp;
    amp.voicing.set ((float) AmpModel::lead);
    amp.gain.set (10.0f);
    amp.prepare (sampleRate, blockSize);

    const auto out = test::render (amp, test::sine (hz, 0.5f, sampleRate, 48000), blockSize);
    const auto from = out.size() / 2;

    const auto fundamental = test::amplitudeAt (out, hz, sampleRate, from);
    const auto aliasDb = [&] (double aliasHz)
    {
        return juce::Decibels::gainToDecibels (test::amplitudeAt (out, aliasHz, sampleRate, from) / fundamental);
    };

    // 9 x 5 kHz folds to 3 kHz, 11 x 5 kHz to 7 kHz, and 19 x 5 kHz to 1 kHz.
    INFO ("3 kHz " << aliasDb (3000.0) << " dB, 7 kHz " << aliasDb (7000.0) << " dB, 1 kHz " << aliasDb (1000.0) << " dB");
    REQUIRE (aliasDb (3000.0) < -50.0);
    REQUIRE (aliasDb (7000.0) < -50.0);
    REQUIRE (aliasDb (1000.0) < -50.0);
}

TEST_CASE ("the tone controls boost and cut their own frequency ranges")
{
    const double sampleRate = 48000.0;

    const auto levelAt = [&] (double hz, Parameter AmpModel::* knob, float setting)
    {
        AmpModel amp;
        amp.gain.set (0.0f);
        (amp.*knob).set (setting);
        amp.prepare (sampleRate, blockSize);
        const auto out = test::render (amp, test::sine (hz, 0.05f, sampleRate, 48000), blockSize);
        return test::amplitudeAt (out, hz, sampleRate, out.size() / 2);
    };

    REQUIRE (levelAt (80.0, &AmpModel::bass, 10.0f) > 2.0 * levelAt (80.0, &AmpModel::bass, 5.0f));
    REQUIRE (levelAt (80.0, &AmpModel::bass, 0.0f) < 0.5 * levelAt (80.0, &AmpModel::bass, 5.0f));
    REQUIRE (levelAt (650.0, &AmpModel::mid, 10.0f) > 2.0 * levelAt (650.0, &AmpModel::mid, 5.0f));
    REQUIRE (levelAt (6000.0, &AmpModel::treble, 10.0f) > 2.0 * levelAt (6000.0, &AmpModel::treble, 5.0f));
    REQUIRE (levelAt (8000.0, &AmpModel::presence, 10.0f) > 1.5 * levelAt (8000.0, &AmpModel::presence, 5.0f));
}

TEST_CASE ("the master volume scales the output and silences it at zero")
{
    const auto levelWithMaster = [] (float setting)
    {
        AmpModel amp;
        amp.master.set (setting);
        const auto out = playNote (amp, 48000.0);
        return test::rms (out, out.size() / 2);
    };

    REQUIRE (levelWithMaster (0.0f) < 1.0e-7);
    REQUIRE (levelWithMaster (8.0f) > levelWithMaster (4.0f));
}

TEST_CASE ("a bypassed amp passes the signal through untouched")
{
    AmpModel amp;
    amp.enabled.set (0.0f);
    amp.prepare (48000.0, blockSize);

    const auto in = test::sine (noteHz, 0.3f, 48000.0, 4800);
    REQUIRE (test::render (amp, in, blockSize) == in);
}

TEST_CASE ("the amp does not leave a DC offset on the output")
{
    const auto voicing = GENERATE (AmpModel::clean, AmpModel::crunch, AmpModel::lead);

    AmpModel amp;
    amp.voicing.set ((float) voicing);
    amp.gain.set (8.0f);
    const auto out = playNote (amp, 48000.0);

    double sum = 0.0;

    for (size_t i = out.size() / 2; i < out.size(); ++i)
        sum += out[i];

    REQUIRE (std::abs (sum / (double) (out.size() / 2)) < 0.002);
}
