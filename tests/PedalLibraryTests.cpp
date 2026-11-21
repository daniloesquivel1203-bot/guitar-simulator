#include "TestSignals.h"
#include "engine/Pedalboard.h"
#include "engine/Presets.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <set>

/** Checks that apply to every pedal in the library, plus a behaviour check
    for each new kind of pedal. */

namespace
{
constexpr int blockSize = 128;
constexpr double sampleRate = 48000.0;

struct Stereo
{
    std::vector<float> left, right;
};

/** Runs any pedal over a signal through the same calls the pedalboard uses. */
Stereo run (Pedal& pedal, std::vector<float> left, std::vector<float> right, int block = blockSize)
{
    for (size_t offset = 0; offset < left.size(); offset += (size_t) block)
    {
        const auto n = (int) juce::jmin ((size_t) block, left.size() - offset);
        pedal.processStereo (left.data() + offset, right.data() + offset, n);
    }

    return { std::move (left), std::move (right) };
}

std::vector<float> runMono (Pedal& pedal, std::vector<float> signal, int block = blockSize)
{
    for (size_t offset = 0; offset < signal.size(); offset += (size_t) block)
        pedal.processMono (signal.data() + offset, (int) juce::jmin ((size_t) block, signal.size() - offset));

    return signal;
}

std::unique_ptr<Pedal> makePedal (const char* key, double rate = sampleRate, int block = blockSize)
{
    const auto* type = findPedalType (key);
    REQUIRE (type != nullptr);
    auto pedal = std::make_unique<Pedal> (*type);
    pedal->getEffect().enabled.set (1.0f);
    pedal->prepare (rate, block);
    return pedal;
}

Parameter& control (Pedal& pedal, const char* id)
{
    for (auto* parameter : pedal.getParameters())
        if (juce::String (parameter->id) == id)
            return *parameter;

    FAIL ("no parameter " << id);
    return pedal.getEffect().enabled;
}

std::vector<float> noise (int numSamples, float amplitude, int seed = 1)
{
    juce::Random random (seed);
    std::vector<float> out ((size_t) numSamples);

    for (auto& sample : out)
        sample = amplitude * (2.0f * random.nextFloat() - 1.0f);

    return out;
}

std::vector<float> impulse (int numSamples, float value = 0.5f)
{
    std::vector<float> out ((size_t) numSamples, 0.0f);
    out[0] = value;
    return out;
}

double energyBetween (const std::vector<float>& signal, size_t from, size_t to)
{
    double sum = 0.0;

    for (size_t i = from; i < juce::jmin (to, signal.size()); ++i)
        sum += (double) signal[i] * signal[i];

    return sum;
}

/** A plucked-string-like test note: a few harmonics with a decaying envelope. */
std::vector<float> pluck (double hz, int numSamples, float amplitude = 0.4f)
{
    std::vector<float> out ((size_t) numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto t = i / sampleRate;
        const auto phase = juce::MathConstants<double>::twoPi * hz * t;
        out[(size_t) i] = amplitude * (float) (std::exp (-2.0 * t)
                                               * (std::sin (phase) + 0.5 * std::sin (2 * phase) + 0.25 * std::sin (3 * phase)) / 1.75);
    }

    return out;
}
}

//==============================================================================
TEST_CASE ("the library has many pedals, each with a unique key, name and parameter ids")
{
    const auto& types = getPedalTypes();
    REQUIRE (types.size() >= 35);

    std::set<juce::String> keys, names, ids;
    std::set<juce::String> categories;

    for (const auto& type : types)
    {
        INFO (type.key);
        REQUIRE (keys.insert (type.key).second);
        REQUIRE (juce::String (type.shortName).isNotEmpty());
        REQUIRE (juce::String (type.shortName).length() <= 8);
        REQUIRE (juce::String (type.category).isNotEmpty());
        categories.insert (type.category);

        Pedal pedal (type);
        REQUIRE (names.insert (pedal.getEffect().name).second);

        // Ids start with the pedal's key, so a preset can never mix two pedals up.
        for (auto* parameter : pedal.getParameters())
        {
            INFO (parameter->id);
            REQUIRE (juce::String (parameter->id).startsWith (juce::String (type.key) + "."));
            REQUIRE (ids.insert (parameter->id).second);
            REQUIRE (parameter->minValue < parameter->maxValue);
            REQUIRE (parameter->defaultValue >= parameter->minValue);
            REQUIRE (parameter->defaultValue <= parameter->maxValue);
        }

        // The panel has room for this many controls. The Custom pedal has its
        // own panel, which shows only the selected block's controls.
        if (juce::String (type.key) != "custom")
            REQUIRE (pedal.getEffect().getControls().size() <= 8);
    }

    REQUIRE (categories.size() >= 7);
}

TEST_CASE ("the original eight pedals keep their keys so saved presets still load")
{
    for (const auto* key : { "gate", "comp", "od", "dist", "eq", "chorus", "delay", "reverb" })
        REQUIRE (findPedalType (key) != nullptr);
}

TEST_CASE ("every library pedal passes the signal through untouched while switched off")
{
    const auto in = test::sine (220.0, 0.3f, sampleRate, 4800);

    for (const auto& type : getPedalTypes())
    {
        INFO (type.key);
        Pedal pedal (type);
        REQUIRE_FALSE (pedal.getEffect().enabled.isOn());
        pedal.prepare (sampleRate, blockSize);

        REQUIRE (runMono (pedal, in) == in);

        const auto out = run (pedal, in, in);
        REQUIRE (out.left == in);
        REQUIRE (out.right == in);
    }
}

TEST_CASE ("every library pedal turns silence into silence")
{
    const std::vector<float> silence (48000, 0.0f);

    for (const auto& type : getPedalTypes())
    {
        INFO (type.key);
        auto pedal = makePedal (type.key);
        REQUIRE (test::peak (runMono (*pedal, silence)) < 1.0e-6f);

        const auto out = run (*pedal, silence, silence);
        REQUIRE (test::peak (out.left) < 1.0e-6f);
        REQUIRE (test::peak (out.right) < 1.0e-6f);
    }
}

TEST_CASE ("every library pedal makes sound when switched on")
{
    const auto in = pluck (196.0, 48000);

    for (const auto& type : getPedalTypes())
    {
        INFO (type.key);
        auto pedal = makePedal (type.key);
        const auto out = run (*pedal, in, in);

        REQUIRE (test::allFinite (out.left));
        REQUIRE (test::rms (out.left) > 0.2 * test::rms (in));
        REQUIRE (test::rms (out.right) > 0.2 * test::rms (in));
    }
}

TEST_CASE ("every library pedal stays finite and bounded at extreme and random settings")
{
    const auto loudSine = test::sine (110.0, 1.0f, sampleRate, 48000);
    const auto loudNoise = noise (48000, 1.0f);

    for (const auto& type : getPedalTypes())
    {
        INFO (type.key);

        for (int setting = 0; setting < 6; ++setting)
        {
            INFO ("setting " << setting);
            juce::Random random (setting + 7);
            Pedal pedal (type);
            pedal.getEffect().enabled.set (1.0f);

            for (auto* parameter : pedal.getEffect().getControls())
            {
                const auto where = setting == 0 ? 0.0f : setting == 1 ? 1.0f : random.nextFloat();
                parameter->set (juce::jmap (where, parameter->minValue, parameter->maxValue));
            }

            pedal.prepare (sampleRate, blockSize);

            for (const auto* in : { &loudSine, &loudNoise })
            {
                const auto out = run (pedal, *in, *in);
                REQUIRE (test::allFinite (out.left));
                REQUIRE (test::allFinite (out.right));
                // A full-scale input is far louder than a guitar, so allow
                // for a boost or EQ turned all the way up.
                REQUIRE (test::peak (out.left) < 100.0f);
                REQUIRE (test::peak (out.right) < 100.0f);

                const auto mono = runMono (pedal, *in);
                REQUIRE (test::allFinite (mono));
                REQUIRE (test::peak (mono) < 100.0f);
            }
        }
    }
}

TEST_CASE ("every library pedal copes with odd block sizes, other sample rates and knobs moving while playing")
{
    const auto in = pluck (147.0, 24000);

    for (const auto& type : getPedalTypes())
    {
        INFO (type.key);

        for (const auto rate : { 44100.0, 96000.0 })
        {
            for (const auto block : { 1, 37, 512 })
            {
                auto pedal = makePedal (type.key, rate, block);
                auto signal = in;
                juce::Random random (block);

                for (size_t offset = 0; offset < signal.size(); offset += (size_t) block)
                {
                    // Turn a random knob to a random place every block.
                    auto controls = pedal->getEffect().getControls();
                    auto* knob = controls[(size_t) random.nextInt ((int) controls.size())];
                    knob->set (juce::jmap (random.nextFloat(), knob->minValue, knob->maxValue));

                    std::vector<float> right (signal.begin() + (long) offset,
                                              signal.begin() + (long) juce::jmin (signal.size(), offset + (size_t) block));
                    pedal->processStereo (signal.data() + offset, right.data(), (int) right.size());
                }

                REQUIRE (test::allFinite (signal));
                REQUIRE (test::peak (signal) < 30.0f);
            }
        }
    }
}

TEST_CASE ("choice controls list their options and store the chosen index")
{
    auto pedal = makePedal ("trem");
    auto& mode = control (*pedal, "trem.mode");

    REQUIRE (mode.isChoice());
    REQUIRE_FALSE (mode.isSwitch());
    REQUIRE (mode.getChoices().size() == 3);
    REQUIRE (juce::String (mode.getChoices()[2]) == "Pan");

    mode.set (2.0f);
    REQUIRE (mode.getIndex() == 2);
    mode.set (9.0f);
    REQUIRE (mode.getIndex() == 2);

    // A two-option choice must not be mistaken for an on/off switch.
    REQUIRE_FALSE (control (*makePedal ("wah"), "wah.mode").isSwitch());
    REQUIRE (control (*makePedal ("rotary"), "rotary.fast").isSwitch());
}

TEST_CASE ("a preset keeps every library pedal and all of its settings")
{
    const auto& types = getPedalTypes();

    for (size_t first = 0; first < types.size(); first += 2 * Pedalboard::maxPedalsPerSide)
    {
        SignalChain source, target;
        Pedalboard::PedalList before, after;
        float nudge = 0.13f;

        for (size_t i = first; i < juce::jmin (types.size(), first + 2 * Pedalboard::maxPedalsPerSide); ++i)
        {
            auto pedal = std::make_shared<Pedal> (types[i]);

            for (auto* parameter : pedal->getParameters())
            {
                parameter->set (parameter->isSwitch() ? 1.0f - parameter->defaultValue
                                                      : juce::jmap (nudge, parameter->minValue, parameter->maxValue));
                nudge = nudge > 0.85f ? 0.13f : nudge + 0.07f;
            }

            (before.size() < (size_t) Pedalboard::maxPedalsPerSide ? before : after).push_back (pedal);
        }

        source.getBoard().setPedals (before, after);
        REQUIRE (PresetManager::apply (target, PresetManager::capture (source, "everything")));

        for (auto side : { Pedalboard::beforeAmp, Pedalboard::afterAmp })
        {
            const auto& expected = source.getBoard().getPedals (side);
            const auto& actual = target.getBoard().getPedals (side);
            REQUIRE (expected.size() == actual.size());

            for (size_t p = 0; p < expected.size(); ++p)
            {
                INFO (expected[p]->type.key);
                REQUIRE (&expected[p]->type == &actual[p]->type);

                const auto a = expected[p]->getParameters();
                const auto b = actual[p]->getParameters();

                for (size_t k = 0; k < a.size(); ++k)
                    REQUIRE_THAT (b[k]->get(), Catch::Matchers::WithinAbs (a[k]->get(), 1.0e-4));
            }
        }
    }
}

//==============================================================================
// Behaviour of individual pedals.

TEST_CASE ("the drive pedals add harmonics")
{
    const auto in = test::sine (200.0, 0.3f, sampleRate, 48000);

    for (const auto* key : { "blues", "fuzz", "bigfuzz", "octfuzz", "metal", "crusher" })
    {
        INFO (key);
        auto pedal = makePedal (key);

        if (juce::String (key) == "crusher")
            control (*pedal, "crusher.bits").set (4.0f);

        const auto out = runMono (*pedal, in);
        REQUIRE (test::distortion (out, 200.0, sampleRate, 24000) > 0.1);
    }

    // The treble booster mostly boosts; it only clips hard when turned up.
    auto treble = makePedal ("treble");
    control (*treble, "treble.boost").set (10.0f);
    REQUIRE (test::distortion (runMono (*treble, test::sine (1000.0, 0.3f, sampleRate, 48000)), 1000.0, sampleRate, 24000) > 0.1);
}

TEST_CASE ("the treble booster lifts treble more than bass")
{
    const auto gainAt = [] (double hz)
    {
        auto pedal = makePedal ("treble");
        control (*pedal, "treble.boost").set (0.0f);
        const auto in = test::sine (hz, 0.01f, sampleRate, 24000);
        return test::rms (runMono (*pedal, in), 12000) / test::rms (in, 12000);
    };

    REQUIRE (gainAt (3000.0) > 3.0 * gainAt (100.0));
}

TEST_CASE ("the octave fuzz adds a note an octave up")
{
    auto pedal = makePedal ("octfuzz");
    control (*pedal, "octfuzz.octave").set (10.0f);
    control (*pedal, "octfuzz.fuzz").set (2.0f);
    const auto out = runMono (*pedal, test::sine (200.0, 0.3f, sampleRate, 48000));

    REQUIRE (test::amplitudeAt (out, 400.0, sampleRate, 24000) > 2.0 * test::amplitudeAt (out, 200.0, sampleRate, 24000));
}

TEST_CASE ("the bit crusher reduces the signal to a few levels")
{
    auto pedal = makePedal ("crusher");
    control (*pedal, "crusher.bits").set (3.0f);
    const auto out = runMono (*pedal, test::sine (200.0, 0.9f, sampleRate, 4800));

    std::set<float> levels (out.begin(), out.end());
    REQUIRE (levels.size() <= 9);
}

TEST_CASE ("the clean boost raises the level without distorting")
{
    auto pedal = makePedal ("boost");
    control (*pedal, "boost.gain").set (12.0f);
    const auto in = test::sine (440.0, 0.1f, sampleRate, 48000);
    const auto out = runMono (*pedal, in);

    REQUIRE_THAT (test::rms (out, 24000) / test::rms (in, 24000), Catch::Matchers::WithinAbs (juce::Decibels::decibelsToGain (12.0), 0.1));
    REQUIRE (test::distortion (out, 440.0, sampleRate, 24000) < 0.01);
}

TEST_CASE ("the opto compressor turns loud playing down and leaves quiet playing alone")
{
    auto pedal = makePedal ("opto");
    control (*pedal, "opto.gain").set (0.0f);
    control (*pedal, "opto.reduction").set (8.0f);

    const auto loud = test::sine (220.0, 0.8f, sampleRate, 96000);
    REQUIRE (test::rms (runMono (*pedal, loud), 48000) < 0.5 * test::rms (loud, 48000));

    pedal->reset();
    const auto quiet = test::sine (220.0, 0.003f, sampleRate, 48000);
    REQUIRE_THAT (test::rms (runMono (*pedal, quiet), 24000), Catch::Matchers::WithinAbs (test::rms (quiet, 24000), 1.0e-4));
}

TEST_CASE ("the auto swell fades each new note in")
{
    auto pedal = makePedal ("swell");
    control (*pedal, "swell.time").set (300.0f);
    const auto in = test::sine (220.0, 0.3f, sampleRate, 48000);
    const auto out = runMono (*pedal, in);

    // Quiet in the first 50 ms, full level after half a second.
    REQUIRE (test::peak (std::vector<float> (out.begin(), out.begin() + 2400)) < 0.05f);
    REQUIRE (test::peak (out, 24000) > 0.29f);
}

TEST_CASE ("the wah's position moves its peak from low to high")
{
    const auto in = noise (48000, 0.2f);
    const auto lowRatio = [&] (float position)
    {
        auto pedal = makePedal ("wah");
        control (*pedal, "wah.position").set (position);
        const auto out = runMono (*pedal, in);
        return test::amplitudeAt (out, 400.0, sampleRate, 0) / (test::amplitudeAt (out, 2000.0, sampleRate, 0) + 1.0e-9);
    };

    REQUIRE (lowRatio (0.0f) > 4.0 * lowRatio (10.0f));
}

TEST_CASE ("the envelope filter opens wider for louder notes")
{
    const auto brightness = [] (float amplitude)
    {
        auto pedal = makePedal ("envfilter");
        const auto out = runMono (*pedal, test::sine (1500.0, amplitude, sampleRate, 9600));
        return test::rms (out, 2400) / amplitude;
    };

    REQUIRE (brightness (0.4f) > 2.0 * brightness (0.002f));
}

TEST_CASE ("the graphic EQ boosts only its own band")
{
    auto pedal = makePedal ("geq");
    control (*pedal, "geq.800").set (12.0f);

    const auto gainAt = [&pedal] (double hz)
    {
        pedal->reset();
        const auto in = test::sine (hz, 0.1f, sampleRate, 24000);
        return test::rms (runMono (*pedal, in), 12000) / test::rms (in, 12000);
    };

    REQUIRE_THAT (gainAt (800.0), Catch::Matchers::WithinAbs (juce::Decibels::decibelsToGain (12.0), 0.3));
    REQUIRE_THAT (gainAt (100.0), Catch::Matchers::WithinAbs (1.0, 0.15));
}

TEST_CASE ("the tremolo pulses the volume and pan mode swaps between ears")
{
    auto pedal = makePedal ("trem");
    control (*pedal, "trem.depth").set (100.0f);
    control (*pedal, "trem.rate").set (4.0f);
    const auto in = test::sine (1000.0, 0.5f, sampleRate, 48000);

    auto out = run (*pedal, in, in);

    // Over one LFO cycle the 5 ms chunks swing from near silence to full.
    double quietest = 1.0, loudest = 0.0;

    for (size_t chunk = 0; chunk < 48; ++chunk)
    {
        const auto level = test::rms (std::vector<float> (out.left.begin() + (long) (chunk * 240),
                                                          out.left.begin() + (long) ((chunk + 1) * 240)));
        quietest = juce::jmin (quietest, level);
        loudest = juce::jmax (loudest, level);
    }

    REQUIRE (quietest < 0.1 * loudest);

    control (*pedal, "trem.mode").set (2.0f);
    pedal->reset();
    out = run (*pedal, in, in);

    // When the left is loud the right is quiet.
    const auto l = test::rms (std::vector<float> (out.left.begin() + 2760, out.left.begin() + 3240));
    const auto r = test::rms (std::vector<float> (out.right.begin() + 2760, out.right.begin() + 3240));
    REQUIRE (juce::jmax (l, r) > 4.0 * juce::jmin (l, r));
}

TEST_CASE ("the modulation pedals change the sound over time")
{
    const auto in = noise (96000, 0.2f);

    for (const auto* key : { "flanger", "phaser", "vibe", "vibrato", "rotary" })
    {
        INFO (key);
        auto pedal = makePedal (key);
        const auto out = run (*pedal, in, in);

        // The spectrum in two windows half an LFO cycle apart differs.
        const auto early = test::amplitudeAt (std::vector<float> (out.left.begin() + 24000, out.left.begin() + 28800), 700.0, sampleRate, 0);
        bool changed = false;

        for (int k = 1; k < 16 && ! changed; ++k)
        {
            const auto from = 24000 + k * 4000;
            const auto later = test::amplitudeAt (std::vector<float> (out.left.begin() + from, out.left.begin() + from + 4800), 700.0, sampleRate, 0);
            changed = std::abs (later - early) > 0.2 * juce::jmax (early, later);
        }

        REQUIRE (changed);
        REQUIRE (out.left != in);
    }
}

TEST_CASE ("the ring modulator replaces a note with its sum and difference")
{
    auto pedal = makePedal ("ringmod");
    control (*pedal, "ringmod.frequency").set (500.0f);
    const auto out = runMono (*pedal, test::sine (200.0, 0.4f, sampleRate, 48000));

    REQUIRE (test::amplitudeAt (out, 300.0, sampleRate, 24000) > 0.15);
    REQUIRE (test::amplitudeAt (out, 700.0, sampleRate, 24000) > 0.15);
    REQUIRE (test::amplitudeAt (out, 200.0, sampleRate, 24000) < 0.01);
}

TEST_CASE ("the pitch pedals move notes to the right pitch")
{
    const auto in = test::sine (220.0, 0.4f, sampleRate, 48000);

    SECTION ("the octaver adds the octave below")
    {
        auto pedal = makePedal ("octave");
        control (*pedal, "octave.dry").set (0.0f);
        const auto out = runMono (*pedal, in);
        REQUIRE (test::amplitudeAt (out, 110.0, sampleRate, 24000) > 3.0 * test::amplitudeAt (out, 220.0, sampleRate, 24000));
    }

    SECTION ("the octaver's upper voice adds the octave above")
    {
        auto pedal = makePedal ("octave");
        control (*pedal, "octave.dry").set (0.0f);
        control (*pedal, "octave.down").set (0.0f);
        control (*pedal, "octave.up").set (100.0f);
        const auto out = runMono (*pedal, in);
        REQUIRE (test::amplitudeAt (out, 440.0, sampleRate, 24000) > 3.0 * test::amplitudeAt (out, 220.0, sampleRate, 24000));
    }

    SECTION ("the pitch shifter moves up a fifth")
    {
        auto pedal = makePedal ("pitch");
        control (*pedal, "pitch.mix").set (100.0f);
        control (*pedal, "pitch.shift").set (7.0f);
        const auto out = runMono (*pedal, in);
        const auto fifth = 220.0 * std::pow (2.0, 7.0 / 12.0);
        REQUIRE (test::amplitudeAt (out, fifth, sampleRate, 24000) > 3.0 * test::amplitudeAt (out, 220.0, sampleRate, 24000));
    }

    SECTION ("the detune makes the ears differ")
    {
        auto pedal = makePedal ("detune");
        const auto out = run (*pedal, in, in);
        REQUIRE (out.left != out.right);
    }
}

TEST_CASE ("the delays repeat a note at the set time")
{
    for (const auto* key : { "tape", "analogdelay" })
    {
        INFO (key);
        auto pedal = makePedal (key);
        control (*pedal, juce::String (key + juce::String (".time")).toRawUTF8()).set (250.0f);
        control (*pedal, juce::String (key + juce::String (".mix")).toRawUTF8()).set (100.0f);

        if (juce::String (key) == "tape")
            control (*pedal, "tape.wow").set (0.0f);
        else
            control (*pedal, "analogdelay.modulation").set (0.0f);

        const auto out = run (*pedal, impulse (48000), impulse (48000));
        const auto echoAt = (size_t) (0.25 * sampleRate);

        // The echo's energy lands in a few milliseconds around 250 ms.
        REQUIRE (energyBetween (out.left, echoAt - 100, echoAt + 400) > 10.0 * energyBetween (out.left, 1000, echoAt - 200));
    }
}

TEST_CASE ("the reverse delay builds up to its loudest point instead of starting with it")
{
    auto pedal = makePedal ("reverse");
    control (*pedal, "reverse.time").set (400.0f);
    control (*pedal, "reverse.feedback").set (0.0f);
    control (*pedal, "reverse.mix").set (100.0f);

    // A sharp attack that fades out, as a picked note does.
    std::vector<float> in (48000, 0.0f);

    for (size_t i = 0; i < 4800; ++i)
        in[i] = 0.5f * std::exp (-(float) i / 800.0f) * (float) std::sin (0.1 * (double) i);

    const auto out = run (*pedal, in, in);

    // Find where the reversed copy peaks after the dry note is gone.
    size_t loudest = 0;

    for (size_t i = 5000; i < out.left.size(); ++i)
        if (std::abs (out.left[i]) > std::abs (out.left[loudest]))
            loudest = i;

    REQUIRE (loudest > 5000);

    // Just before the peak it is rising, not falling.
    REQUIRE (energyBetween (out.left, loudest - 400, loudest) > 5.0 * energyBetween (out.left, loudest + 600, loudest + 1000));
}

TEST_CASE ("the reverbs leave a tail that lasts longer with more decay")
{
    for (const auto& [key, decayId, shortValue, longValue] :
         { std::tuple<const char*, const char*, float, float> { "hall", "hall.decay", 1.0f, 6.0f },
           { "plate", "plate.decay", 0.5f, 4.0f },
           { "shimmer", "shimmer.decay", 1.5f, 8.0f },
           { "spring", "spring.decay", 0.0f, 10.0f } })
    {
        INFO (key);

        const auto tail = [&] (float decayValue)
        {
            auto pedal = makePedal (key);
            control (*pedal, decayId).set (decayValue);
            const auto out = run (*pedal, impulse (96000), impulse (96000));
            return energyBetween (out.left, 24000, 72000) + energyBetween (out.right, 24000, 72000);
        };

        const auto shortTail = tail (shortValue);
        const auto longTail = tail (longValue);
        REQUIRE (longTail > 0.0);
        REQUIRE (longTail > 3.0 * shortTail);
    }
}

TEST_CASE ("the shimmer reverb's tail rises in pitch")
{
    const auto highShare = [] (float amount)
    {
        auto pedal = makePedal ("shimmer");
        control (*pedal, "shimmer.amount").set (amount);
        control (*pedal, "shimmer.mix").set (100.0f);
        const auto note = test::sine (220.0, 0.3f, sampleRate, 96000);
        const auto out = run (*pedal, note, note);
        return test::amplitudeAt (out.left, 880.0, sampleRate, 48000) / (test::amplitudeAt (out.left, 220.0, sampleRate, 48000) + 1.0e-9);
    };

    REQUIRE (highShare (100.0f) > 3.0 * highShare (0.0f));
}


TEST_CASE ("each library pedal uses only a small share of one core", "[.][performance]")
{
    constexpr int smallBlock = 64;
    const auto in = pluck (110.0, 4 * (int) sampleRate);

    for (const auto& type : getPedalTypes())
    {
        auto pedal = makePedal (type.key, sampleRate, smallBlock);
        auto left = in, right = in;

        const auto start = std::chrono::steady_clock::now();

        for (size_t offset = 0; offset + smallBlock <= left.size(); offset += smallBlock)
            pedal->processStereo (left.data() + offset, right.data() + offset, smallBlock);

        const auto taken = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        const auto percent = 100.0 * taken / ((double) in.size() / sampleRate);
        std::printf ("%-12s %5.2f %% of one core\n", type.key, percent);

        INFO (type.key);
        REQUIRE (percent < 5.0);
    }
}
