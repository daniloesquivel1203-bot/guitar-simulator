#include "TestSignals.h"
#include "engine/Pedalboard.h"
#include "engine/effects/TempoSync.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <memory>

using Catch::Matchers::WithinAbs;

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 128;

enum Choice { off = 0, quarter = 1, dottedEighth = 2, eighth = 3, eighthTriplet = 4, sixteenth = 5, half = 6 };

std::vector<float> impulse (int numSamples)
{
    std::vector<float> out ((size_t) numSamples, 0.0f);
    out[0] = 1.0f;
    return out;
}

/** Runs an impulse through a stereo pedal at `bpm` and returns the left side. */
std::vector<float> renderAt (StereoEffect& effect, double bpm, int numSamples)
{
    effect.setTempo (bpm);
    effect.reset();

    auto left = impulse (numSamples);
    auto right = left;

    for (int offset = 0; offset < numSamples; offset += blockSize)
        effect.process (left.data() + offset, right.data() + offset, juce::jmin (blockSize, numSamples - offset));

    return left;
}

/** Where the first echo lands: the first sample after the dry click that is
    clearly above silence. */
int firstEcho (const std::vector<float>& signal)
{
    for (size_t i = 16; i < signal.size(); ++i)
        if (std::abs (signal[i]) > 0.02f)
            return (int) i;

    return -1;
}

int msToSamples (double ms)   { return (int) std::round (ms * 0.001 * sampleRate); }

template <typename EffectType>
std::unique_ptr<EffectType> makeOn()
{
    auto effect = std::make_unique<EffectType>();
    effect->enabled.set (1.0f);
    effect->mix.set (100.0f);
    effect->prepare (sampleRate, blockSize);
    return effect;
}

std::vector<std::unique_ptr<StereoEffect>> allDelays()
{
    std::vector<std::unique_ptr<StereoEffect>> delays;
    delays.push_back (std::make_unique<Delay>());
    delays.push_back (std::make_unique<TapeEcho>());
    delays.push_back (std::make_unique<AnalogDelay>());
    delays.push_back (std::make_unique<ReverseDelay>());
    return delays;
}
}

//==============================================================================
TEST_CASE ("note values give the right times at a tempo")
{
    REQUIRE_THAT (tempo_sync::noteMs (120.0, quarter, 20.0f, 1500.0f), WithinAbs (500.0, 1.0e-3));
    REQUIRE_THAT (tempo_sync::noteMs (120.0, dottedEighth, 20.0f, 1500.0f), WithinAbs (375.0, 1.0e-3));
    REQUIRE_THAT (tempo_sync::noteMs (120.0, eighth, 20.0f, 1500.0f), WithinAbs (250.0, 1.0e-3));
    REQUIRE_THAT (tempo_sync::noteMs (120.0, eighthTriplet, 20.0f, 1500.0f), WithinAbs (500.0 / 3.0, 1.0e-3));
    REQUIRE_THAT (tempo_sync::noteMs (120.0, sixteenth, 20.0f, 1500.0f), WithinAbs (125.0, 1.0e-3));
    REQUIRE_THAT (tempo_sync::noteMs (120.0, half, 20.0f, 1500.0f), WithinAbs (1000.0, 1.0e-3));
}

TEST_CASE ("a note too long or short for the pedal is halved or doubled until it fits")
{
    // A half note at 40 BPM is 3000 ms. The analog delay stops at 600 ms, so it
    // becomes 375 ms, still on the beat grid.
    REQUIRE_THAT (tempo_sync::noteMs (40.0, half, 20.0f, 600.0f), WithinAbs (375.0, 1.0e-3));

    // A sixteenth at 300 BPM is 50 ms; the reverse delay's shortest slice is 150 ms.
    REQUIRE_THAT (tempo_sync::noteMs (300.0, sixteenth, 150.0f, 1500.0f), WithinAbs (200.0, 1.0e-3));
}

TEST_CASE ("every delay has a Sync choice that starts off, so old presets sound the same")
{
    for (auto& delay : allDelays())
    {
        auto* sync = delay->getTempoSyncParameter();
        auto* time = delay->getTapTimeParameter();

        REQUIRE (sync != nullptr);
        REQUIRE (time != nullptr);
        REQUIRE (sync->isChoice());
        REQUIRE (sync->getIndex() == off);
        REQUIRE (juce::String (sync->id).endsWith (".sync"));
        REQUIRE (juce::String (time->id).endsWith (".time"));

        const auto controls = delay->getControls();
        REQUIRE (std::find (controls.begin(), controls.end(), sync) != controls.end());
    }

    // Other pedals have no tap tempo.
    Chorus chorus;
    REQUIRE (chorus.getTapTimeParameter() == nullptr);
    REQUIRE (chorus.getTempoSyncParameter() == nullptr);
}

TEST_CASE ("with Sync off the delay ignores the tempo and uses its Time knob")
{
    auto delay = makeOn<Delay>();
    delay->time.set (100.0f);

    REQUIRE (firstEcho (renderAt (*delay, 60.0, 24000)) == msToSamples (100.0));
    REQUIRE (firstEcho (renderAt (*delay, 200.0, 24000)) == msToSamples (100.0));
}

TEST_CASE ("with Sync on the delay follows the tempo instead of its Time knob")
{
    auto delay = makeOn<Delay>();
    delay->time.set (100.0f);
    delay->sync.set ((float) eighth);

    REQUIRE (firstEcho (renderAt (*delay, 120.0, 24000)) == msToSamples (250.0));
    REQUIRE (firstEcho (renderAt (*delay, 150.0, 24000)) == msToSamples (200.0));

    delay->sync.set ((float) dottedEighth);
    REQUIRE (firstEcho (renderAt (*delay, 120.0, 24000)) == msToSamples (375.0));
}

TEST_CASE ("the tape and analog delays follow the tempo too")
{
    auto tape = makeOn<TapeEcho>();
    tape->wow.set (0.0f);
    tape->sync.set ((float) quarter);

    // The tape's filters smear the echo by a few samples.
    REQUIRE (std::abs (firstEcho (renderAt (*tape, 120.0, 30000)) - msToSamples (500.0)) < 30);

    auto analog = makeOn<AnalogDelay>();
    analog->modulation.set (0.0f);
    analog->sync.set ((float) eighth);
    REQUIRE (std::abs (firstEcho (renderAt (*analog, 100.0, 24000)) - msToSamples (300.0)) < 30);
}

TEST_CASE ("the reverse delay's slices follow the tempo")
{
    auto reverse = makeOn<ReverseDelay>();
    reverse->sync.set ((float) quarter);

    const auto riff = test::guitarRiff (sampleRate);
    const auto render = [&] (double bpm)
    {
        reverse->setTempo (bpm);
        reverse->reset();
        auto left = riff;
        auto right = riff;

        for (size_t offset = 0; offset < left.size(); offset += (size_t) blockSize)
            reverse->process (left.data() + offset, right.data() + offset,
                              (int) juce::jmin ((size_t) blockSize, left.size() - offset));

        return left;
    };

    const auto slow = render (60.0);
    const auto fast = render (180.0);

    REQUIRE (test::allFinite (slow));
    REQUIRE (test::allFinite (fast));
    REQUIRE (slow != fast);
}

TEST_CASE ("the pedalboard hands its tempo to the delays")
{
    Pedalboard board;
    auto pedal = board.add (Pedalboard::afterAmp, *findPedalType ("delay"));
    REQUIRE (pedal != nullptr);

    auto& delay = dynamic_cast<Delay&> (pedal->getEffect());
    delay.mix.set (100.0f);
    delay.feedback.set (0.0f);
    delay.sync.set ((float) quarter);

    board.prepare (sampleRate, blockSize);
    board.setTempo (150.0);   // a quarter note is 400 ms

    auto left = impulse (24000);
    std::vector<float> right (left.size(), 0.0f);

    for (size_t offset = 0; offset < left.size(); offset += (size_t) blockSize)
    {
        board.beginChunk();
        board.processAfterAmp (left.data() + offset, right.data() + offset,
                               (int) juce::jmin ((size_t) blockSize, left.size() - offset));
    }

    REQUIRE (firstEcho (left) == msToSamples (400.0));
}
