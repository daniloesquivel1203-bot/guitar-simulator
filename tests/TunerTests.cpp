#include "TestSignals.h"
#include "engine/Tuner.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <random>

using Catch::Matchers::WithinAbs;

namespace
{
double centsBetween (double a, double b)
{
    return 1200.0 * std::log2 (a / b);
}

/** A plucked-string-like tone: several harmonics, with the fundamental weaker than the second. */
std::vector<float> stringTone (double hz, double sampleRate, int numSamples)
{
    const double levels[] = { 0.4, 1.0, 0.6, 0.35, 0.2, 0.1 };
    std::vector<float> out ((size_t) numSamples, 0.0f);

    for (int i = 0; i < numSamples; ++i)
    {
        double sample = 0.0;

        for (int harmonic = 1; harmonic <= 6; ++harmonic)
            sample += levels[harmonic - 1] * std::sin (juce::MathConstants<double>::twoPi * hz * harmonic * i / sampleRate
                                                       + 0.7 * harmonic);

        out[(size_t) i] = (float) (0.1 * sample);
    }

    return out;
}
}

TEST_CASE ("the tuner finds each open string of a guitar to within a cent")
{
    const auto hz = GENERATE (82.41, 110.0, 146.83, 196.0, 246.94, 329.63);
    const auto sampleRate = GENERATE (44100.0, 48000.0);

    const auto tone = test::sine (hz, 0.2f, sampleRate, TunerTap::windowSize);
    const auto found = PitchDetector::detect (tone.data(), (int) tone.size(), sampleRate);

    REQUIRE (found.has_value());
    REQUIRE (std::abs (centsBetween (*found, hz)) < 1.0);
}

TEST_CASE ("the tuner is not fooled into the wrong octave by strong harmonics")
{
    const auto hz = GENERATE (82.41, 110.0, 146.83, 196.0, 246.94, 329.63, 61.74, 659.26);

    const auto tone = stringTone (hz, 48000.0, TunerTap::windowSize);
    const auto found = PitchDetector::detect (tone.data(), (int) tone.size(), 48000.0);

    REQUIRE (found.has_value());
    REQUIRE (std::abs (centsBetween (*found, hz)) < 3.0);
}

TEST_CASE ("the tuner can tell a string that is slightly out of tune")
{
    const auto offsetCents = GENERATE (-20.0, -5.0, 5.0, 20.0);
    const auto hz = 110.0 * std::pow (2.0, offsetCents / 1200.0);

    const auto tone = stringTone (hz, 48000.0, TunerTap::windowSize);
    const auto found = PitchDetector::detect (tone.data(), (int) tone.size(), 48000.0);

    REQUIRE (found.has_value());

    const auto reading = TunerReading::fromHz (*found);
    REQUIRE (reading.note == "A");
    REQUIRE (reading.octave == 2);
    REQUIRE_THAT (reading.cents, WithinAbs (offsetCents, 1.5));
}

TEST_CASE ("the tuner reports nothing for silence, faint hiss or noise")
{
    const std::vector<float> silence ((size_t) TunerTap::windowSize, 0.0f);
    REQUIRE_FALSE (PitchDetector::detect (silence.data(), (int) silence.size(), 48000.0).has_value());

    const auto faint = test::sine (110.0, 0.0002f, 48000.0, TunerTap::windowSize);
    REQUIRE_FALSE (PitchDetector::detect (faint.data(), (int) faint.size(), 48000.0).has_value());

    std::mt19937 random (1234);
    std::uniform_real_distribution<float> distribution (-0.3f, 0.3f);
    std::vector<float> noise ((size_t) TunerTap::windowSize);

    for (auto& sample : noise)
        sample = distribution (random);

    REQUIRE_FALSE (PitchDetector::detect (noise.data(), (int) noise.size(), 48000.0).has_value());

    REQUIRE_FALSE (PitchDetector::detect (nullptr, 0, 48000.0).has_value());
}

TEST_CASE ("pitches are named correctly")
{
    const auto a4 = TunerReading::fromHz (440.0);
    REQUIRE (a4.note == "A");
    REQUIRE (a4.octave == 4);
    REQUIRE_THAT (a4.cents, WithinAbs (0.0, 1.0e-6));

    const auto lowE = TunerReading::fromHz (82.4069);
    REQUIRE (lowE.note == "E");
    REQUIRE (lowE.octave == 2);
    REQUIRE_THAT (lowE.cents, WithinAbs (0.0, 0.01));

    const auto middleC = TunerReading::fromHz (261.6256);
    REQUIRE (middleC.note == "C");
    REQUIRE (middleC.octave == 4);

    const auto sharp = TunerReading::fromHz (440.0 * std::pow (2.0, 30.0 / 1200.0));
    REQUIRE (sharp.note == "A");
    REQUIRE_THAT (sharp.cents, WithinAbs (30.0, 1.0e-6));

    // Just past the halfway point it becomes a flat reading of the next note up.
    const auto nearlyNext = TunerReading::fromHz (440.0 * std::pow (2.0, 60.0 / 1200.0));
    REQUIRE (nearlyNext.note == "A#");
    REQUIRE_THAT (nearlyNext.cents, WithinAbs (-40.0, 1.0e-6));
}

TEST_CASE ("the tuner tap delivers the most recent samples in order, only while active")
{
    TunerTap tap;
    std::vector<float> ramp (10000);

    for (size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = (float) i;

    // Inactive: nothing is recorded.
    tap.push (ramp.data(), (int) ramp.size());
    std::vector<float> window ((size_t) TunerTap::windowSize, -1.0f);
    tap.copyLatest (window.data());
    REQUIRE_THAT (window.back(), WithinAbs (0.0, 0.0));

    tap.setActive (true);

    // Push in uneven pieces, as an audio device would.
    for (size_t offset = 0; offset < ramp.size(); offset += 333)
        tap.push (ramp.data() + offset, (int) juce::jmin ((size_t) 333, ramp.size() - offset));

    tap.copyLatest (window.data());
    REQUIRE_THAT (window.back(), WithinAbs (9999.0, 0.0));
    REQUIRE_THAT (window.front(), WithinAbs (10000.0 - TunerTap::windowSize, 0.0));
}
