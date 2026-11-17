#include "engine/Presets.h"
#include "TestSignals.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
using test::allFinite;
using test::rms;

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 64;

std::vector<juce::String> keysOn (SignalChain& chain, Pedalboard::Side side)
{
    std::vector<juce::String> keys;

    for (auto& pedal : chain.getBoard().getPedals (side))
        keys.push_back (pedal->type.key);

    return keys;
}

/** Turns on every pedal and gives each knob a distinct, non-default value. */
void setEverything (SignalChain& chain)
{
    float nudge = 0.2f;

    for (auto* parameter : chain.getAllParameters())
    {
        if (parameter->isSwitch())
        {
            parameter->set (1.0f);
        }
        else
        {
            parameter->set (juce::jmap (nudge, parameter->minValue, parameter->maxValue));
            nudge = nudge > 0.75f ? 0.2f : nudge + 0.09f;
        }
    }

    chain.findPedal<Delay>()->pingPong.set (1.0f);
}

/** Sends a guitar-like test signal through the chain and returns both ears. */
std::pair<std::vector<float>, std::vector<float>> play (SignalChain& chain, int numBlocks,
                                                        const std::function<void (int block)>& beforeBlock = {})
{
    std::vector<float> left, right;
    std::vector<float> input ((size_t) blockSize), outL ((size_t) blockSize), outR ((size_t) blockSize);

    for (int block = 0; block < numBlocks; ++block)
    {
        if (beforeBlock != nullptr)
            beforeBlock (block);

        for (int i = 0; i < blockSize; ++i)
        {
            const auto t = (double) (block * blockSize + i) / sampleRate;
            input[(size_t) i] = 0.3f * (float) (std::sin (juce::MathConstants<double>::twoPi * 110.0 * t)
                                                + 0.5 * std::sin (juce::MathConstants<double>::twoPi * 330.0 * t));
        }

        chain.process (input.data(), outL.data(), outR.data(), blockSize);
        left.insert (left.end(), outL.begin(), outL.end());
        right.insert (right.end(), outR.begin(), outR.end());
    }

    return { left, right };
}
}

TEST_CASE ("a new chain has the V1 pedals in the V1 order")
{
    SignalChain chain;
    REQUIRE (keysOn (chain, Pedalboard::beforeAmp) == std::vector<juce::String> { "gate", "comp", "od", "dist" });
    REQUIRE (keysOn (chain, Pedalboard::afterAmp) == std::vector<juce::String> { "eq", "chorus", "delay", "reverb" });
}

TEST_CASE ("the classic board sounds exactly like the V1 fixed chain")
{
    // The V1 chain, written out by hand with separate effect objects.
    NoiseGate gate;  Compressor comp;  Overdrive od;  Distortion dist;
    AmpModel amp;    Equaliser eq;     Chorus chorus; Delay delay;  Reverb reverb;
    SafetyLimiter limiter;

    SignalChain chain;
    setEverything (chain);

    // The cabinet loads its impulse on a background thread, so leave it out of the comparison.
    chain.getCabinet().enabled.set (0.0f);

    // Copy every value into the hand-made chain by id.
    std::vector<Parameter*> manual { &gate.enabled, &comp.enabled, &od.enabled, &dist.enabled,
                                     &eq.enabled, &chorus.enabled, &delay.enabled, &reverb.enabled };

    for (Effect* effect : std::initializer_list<Effect*> { &gate, &comp, &od, &dist, &eq, &chorus, &delay, &reverb })
        for (auto* control : effect->getControls())
            manual.push_back (control);

    for (auto* parameter : amp.getParameters())
        manual.push_back (parameter);

    int copied = 0;

    for (auto* source : chain.getAllParameters())
    {
        for (auto* target : manual)
        {
            if (juce::String (target->id) == source->id)
            {
                target->set (source->get());
                ++copied;
            }
        }
    }

    REQUIRE (copied == (int) manual.size());

    chain.prepare (sampleRate, blockSize);

    for (Effect* effect : std::initializer_list<Effect*> { &gate, &comp, &od, &dist, &eq, &chorus, &delay, &reverb })
        effect->prepare (sampleRate, blockSize);

    amp.prepare (sampleRate, blockSize);
    limiter.prepare (sampleRate);

    const auto [left, right] = play (chain, 400);

    std::vector<float> input ((size_t) blockSize), l ((size_t) blockSize), r ((size_t) blockSize);

    for (int block = 0; block < 400; ++block)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const auto t = (double) (block * blockSize + i) / sampleRate;
            input[(size_t) i] = 0.3f * (float) (std::sin (juce::MathConstants<double>::twoPi * 110.0 * t)
                                                + 0.5 * std::sin (juce::MathConstants<double>::twoPi * 330.0 * t));
        }

        std::copy (input.begin(), input.end(), l.begin());
        gate.process (l.data(), blockSize);
        comp.process (l.data(), blockSize);
        od.process (l.data(), blockSize);
        dist.process (l.data(), blockSize);
        amp.process (l.data(), blockSize);
        eq.process (l.data(), blockSize);
        std::copy (l.begin(), l.end(), r.begin());
        chorus.process (l.data(), r.data(), blockSize);
        delay.process (l.data(), r.data(), blockSize);
        reverb.process (l.data(), r.data(), blockSize);

        for (int i = 0; i < blockSize; ++i)
        {
            auto a = l[(size_t) i], b = r[(size_t) i];
            limiter.processStereo (a, b);
            const auto index = (size_t) (block * blockSize + i);
            REQUIRE (juce::exactlyEqual (left[index], a));
            REQUIRE (juce::exactlyEqual (right[index], b));
        }
    }
}

TEST_CASE ("pedals can be added, moved and removed, and each side has a limit")
{
    SignalChain chain;
    auto& board = chain.getBoard();
    board.setPedals ({}, {});

    auto drive = board.add (Pedalboard::beforeAmp, *findPedalType ("od"));
    REQUIRE (drive != nullptr);
    REQUIRE (drive->getEffect().enabled.isOn());   // a new pedal starts switched on

    board.add (Pedalboard::beforeAmp, *findPedalType ("gate"), 0);
    board.add (Pedalboard::beforeAmp, *findPedalType ("od"));   // two of the same kind
    REQUIRE (keysOn (chain, Pedalboard::beforeAmp) == std::vector<juce::String> { "gate", "od", "od" });

    REQUIRE (board.move (Pedalboard::beforeAmp, 0, Pedalboard::beforeAmp, 2));
    REQUIRE (keysOn (chain, Pedalboard::beforeAmp) == std::vector<juce::String> { "od", "od", "gate" });
    REQUIRE (board.getPedals (Pedalboard::beforeAmp)[0] == drive);   // the same pedal object, settings kept

    REQUIRE (board.move (Pedalboard::beforeAmp, 2, Pedalboard::afterAmp, 0));
    REQUIRE (keysOn (chain, Pedalboard::afterAmp) == std::vector<juce::String> { "gate" });
    REQUIRE_FALSE (board.move (Pedalboard::afterAmp, 0, Pedalboard::afterAmp, 0));

    board.remove (Pedalboard::beforeAmp, 0);
    REQUIRE (keysOn (chain, Pedalboard::beforeAmp) == std::vector<juce::String> { "od" });

    while ((int) board.getPedals (Pedalboard::afterAmp).size() < Pedalboard::maxPedalsPerSide)
        REQUIRE (board.add (Pedalboard::afterAmp, *findPedalType ("eq")) != nullptr);

    REQUIRE (board.add (Pedalboard::afterAmp, *findPedalType ("eq")) == nullptr);
    REQUIRE_FALSE (board.move (Pedalboard::beforeAmp, 0, Pedalboard::afterAmp, 0));
}

TEST_CASE ("changing the order while playing fades instead of clicking")
{
    SignalChain chain;
    chain.getAmp().enabled.set (0.0f);
    chain.getCabinet().enabled.set (0.0f);

    // A loud overdrive and a quiet EQ, so swapping them makes a large jump in level.
    auto& board = chain.getBoard();
    board.setPedals ({}, {});
    auto drive = board.add (Pedalboard::beforeAmp, *findPedalType ("od"));
    auto eq = board.add (Pedalboard::afterAmp, *findPedalType ("eq"));
    drive->getEffect().getControls()[2]->set (10.0f);
    eq->getEffect().getControls()[0]->set (-12.0f);
    chain.prepare (sampleRate, blockSize);

    int moves = 0;
    const auto [left, right] = play (chain, 300, [&] (int block)
    {
        if (block % 50 == 25)
        {
            const auto fromBefore = ! board.getPedals (Pedalboard::beforeAmp).empty();
            board.move (fromBefore ? Pedalboard::beforeAmp : Pedalboard::afterAmp, 0,
                        fromBefore ? Pedalboard::afterAmp : Pedalboard::beforeAmp, 0);
            ++moves;
        }
    });

    REQUIRE (moves == 6);
    REQUIRE (allFinite (left));

    // The biggest step between neighbouring samples during the moves is no
    // bigger than in steady playing.
    float steadyStep = 0.0f, worstStep = 0.0f;

    for (size_t i = 1; i < left.size(); ++i)
    {
        const auto step = std::abs (left[i] - left[i - 1]);
        worstStep = juce::jmax (worstStep, step);

        if (i < 25 * blockSize)
            steadyStep = juce::jmax (steadyStep, step);
    }

    REQUIRE (worstStep <= steadyStep * 1.05f);
}

TEST_CASE ("a mono pedal after a stereo pedal processes both ears the same way")
{
    SignalChain chain;
    chain.getAmp().enabled.set (0.0f);
    chain.getCabinet().enabled.set (0.0f);

    auto& board = chain.getBoard();
    board.setPedals ({}, {});
    auto delay = board.add (Pedalboard::afterAmp, *findPedalType ("delay"));
    auto eq = board.add (Pedalboard::afterAmp, *findPedalType ("eq"));
    delay->getEffect().enabled.set (0.0f);   // a stereo pedal that is off still makes the signal stereo
    eq->getEffect().getControls()[3]->set (-12.0f);   // high cut
    chain.prepare (sampleRate, blockSize);

    const auto [left, right] = play (chain, 100);

    for (size_t i = 0; i < left.size(); ++i)
        REQUIRE_THAT (right[i], WithinAbs (left[i], 1.0e-6));

    REQUIRE (rms (left, left.size() / 2) > 0.01);
}

TEST_CASE ("a stereo pedal before the amp works on the mono signal")
{
    SignalChain chain;
    auto& board = chain.getBoard();
    board.setPedals ({}, {});
    board.add (Pedalboard::beforeAmp, *findPedalType ("chorus"));
    board.add (Pedalboard::beforeAmp, *findPedalType ("reverb"));
    chain.prepare (sampleRate, blockSize);

    const auto [left, right] = play (chain, 200);
    REQUIRE (allFinite (left));
    REQUIRE (rms (left, left.size() / 2) > 0.01);

    // Nothing after the amp is stereo, so both ears match.
    for (size_t i = 0; i < left.size(); ++i)
        REQUIRE (juce::exactlyEqual (right[i], left[i]));
}

TEST_CASE ("a version 2 preset keeps the order, repeated pedals and their own settings")
{
    SignalChain source;
    auto& board = source.getBoard();
    board.setPedals ({}, {});
    auto first = board.add (Pedalboard::beforeAmp, *findPedalType ("od"));
    auto second = board.add (Pedalboard::beforeAmp, *findPedalType ("od"));
    board.add (Pedalboard::afterAmp, *findPedalType ("reverb"));
    board.add (Pedalboard::afterAmp, *findPedalType ("comp"));
    first->getEffect().getControls()[0]->set (2.0f);
    second->getEffect().getControls()[0]->set (9.0f);
    second->getEffect().enabled.set (0.0f);

    juce::var parsed;
    REQUIRE (juce::JSON::parse (juce::JSON::toString (PresetManager::capture (source, "Two drives")), parsed).wasOk());

    SignalChain target;
    REQUIRE (PresetManager::apply (target, parsed));
    REQUIRE (keysOn (target, Pedalboard::beforeAmp) == std::vector<juce::String> { "od", "od" });
    REQUIRE (keysOn (target, Pedalboard::afterAmp) == std::vector<juce::String> { "reverb", "comp" });

    const auto& loaded = target.getBoard().getPedals (Pedalboard::beforeAmp);
    REQUIRE_THAT (loaded[0]->getEffect().getControls()[0]->get(), WithinAbs (2.0, 1.0e-4));
    REQUIRE_THAT (loaded[1]->getEffect().getControls()[0]->get(), WithinAbs (9.0, 1.0e-4));
    REQUIRE (loaded[0]->getEffect().enabled.isOn());
    REQUIRE_FALSE (loaded[1]->getEffect().enabled.isOn());
}

TEST_CASE ("a version 1 preset loads into the V1 order and an unknown pedal is skipped")
{
    SignalChain chain;
    chain.getBoard().setPedals ({}, {});

    juce::var v1;
    REQUIRE (juce::JSON::parse (R"({ "format": "guitar-simulator-preset", "name": "Old",
                                     "parameters": { "od.enabled": 1, "od.drive": 7.0, "reverb.mix": 40.0 } })", v1).wasOk());
    REQUIRE (PresetManager::apply (chain, v1));
    REQUIRE (keysOn (chain, Pedalboard::beforeAmp) == std::vector<juce::String> { "gate", "comp", "od", "dist" });
    REQUIRE (chain.findPedal<Overdrive>()->enabled.isOn());
    REQUIRE_THAT (chain.findPedal<Overdrive>()->drive.get(), WithinAbs (7.0, 1.0e-6));
    REQUIRE_THAT (chain.findPedal<Reverb>()->mix.get(), WithinAbs (40.0, 1.0e-6));

    juce::var future;
    REQUIRE (juce::JSON::parse (R"({ "format": "guitar-simulator-preset", "version": 3, "parameters": {},
                                     "board": { "beforeAmp": [ { "type": "time-machine", "parameters": {} },
                                                               { "type": "gate", "parameters": { "gate.enabled": 1 } } ],
                                                "afterAmp": [] } })", future).wasOk());
    REQUIRE (PresetManager::apply (chain, future));
    REQUIRE (keysOn (chain, Pedalboard::beforeAmp) == std::vector<juce::String> { "gate" });
    REQUIRE (keysOn (chain, Pedalboard::afterAmp).empty());
}
