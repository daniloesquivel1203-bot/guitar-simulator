#include "TestSignals.h"
#include "engine/Pedalboard.h"
#include "engine/Presets.h"
#include "engine/SignalChain.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

/** The Patch pedal: blocks wired freely, loops included. */

namespace
{
using custom_blocks::BlockType;

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 100;   // not a multiple of the pedal's step, on purpose

std::unique_ptr<PatchPedal> makePatch()
{
    auto pedal = std::make_unique<PatchPedal>();
    pedal->enabled.set (1.0f);
    pedal->prepare (sampleRate, blockSize);
    return pedal;
}

std::vector<float> run (PatchPedal& pedal, std::vector<float> signal)
{
    for (size_t offset = 0; offset < signal.size(); offset += blockSize)
        pedal.process (signal.data() + offset, (int) juce::jmin ((size_t) blockSize, signal.size() - offset));

    return signal;
}

double responseDb (PatchPedal& pedal, double hz)
{
    pedal.reset();
    const auto in = test::sine (hz, 0.1f, sampleRate, 24000);
    const auto out = run (pedal, in);
    return juce::Decibels::gainToDecibels (test::rms (out, 12000) / test::rms (in, 12000));
}
}

TEST_CASE ("an empty patch passes the signal through untouched")
{
    auto pedal = makePatch();
    const auto in = test::sine (440.0, 0.3f, sampleRate, 4800);
    const auto out = run (*pedal, in);

    for (size_t i = 0; i < in.size(); ++i)
        REQUIRE_THAT (out[i], Catch::Matchers::WithinAbs (in[i], 1.0e-6));
}

TEST_CASE ("new patch blocks chain up before the output")
{
    auto pedal = makePatch();
    const auto hp = pedal->addNode (BlockType::highPass, 0.3f, 0.5f);
    const auto lp = pedal->addNode (BlockType::lowPass, 0.6f, 0.5f);
    pedal->getNode (hp).block.frequency.set (300.0f);
    pedal->getNode (lp).block.frequency.set (2000.0f);

    REQUIRE (pedal->getNode (hp).getSource (0) == PatchPedal::inputSource);
    REQUIRE (pedal->getNode (lp).getSource (0) == hp);
    REQUIRE (pedal->getOutputSource() == lp);

    REQUIRE (responseDb (*pedal, 50.0) < -20.0);
    REQUIRE (std::abs (responseDb (*pedal, 800.0)) < 1.5);
    REQUIRE (responseDb (*pedal, 12000.0) < -20.0);

    // Removing the first closes the chain up around it.
    pedal->removeNode (hp);
    REQUIRE (pedal->getNode (lp).getSource (0) == PatchPedal::inputSource);
    REQUIRE (responseDb (*pedal, 50.0) > -1.0);
}

TEST_CASE ("one output can feed two parallel blocks, joined by a mix")
{
    auto pedal = makePatch();
    const auto lp = pedal->addNode (BlockType::lowPass, 0.3f, 0.3f);
    const auto hp = pedal->addNode (BlockType::highPass, 0.3f, 0.7f);
    const auto mix = pedal->addNode (BlockType::mix, 0.7f, 0.5f);
    REQUIRE (pedal->connect (PatchPedal::inputSource, lp, 0));
    REQUIRE (pedal->connect (PatchPedal::inputSource, hp, 0));
    REQUIRE (pedal->connect (lp, mix, 0));
    REQUIRE (pedal->connect (hp, mix, 1));
    REQUIRE (pedal->getOutputSource() == mix);

    pedal->getNode (lp).block.frequency.set (200.0f);
    pedal->getNode (hp).block.frequency.set (5000.0f);
    pedal->getNode (mix).block.mix.set (50.0f);

    // Each band comes through one path at half level; the middle is cut by both.
    REQUIRE_THAT (responseDb (*pedal, 40.0), Catch::Matchers::WithinAbs (-6.0, 1.0));
    REQUIRE_THAT (responseDb (*pedal, 16000.0), Catch::Matchers::WithinAbs (-6.0, 1.0));
    REQUIRE (responseDb (*pedal, 1000.0) < -15.0);

    // Only a mix has a second input, and modulators carry no sound.
    REQUIRE_FALSE (pedal->connect (PatchPedal::inputSource, lp, 1));
    const auto lfo = pedal->addNode (BlockType::lfo, 0.5f, 0.1f);
    REQUIRE_FALSE (pedal->connect (lfo, mix, 0));
}

TEST_CASE ("a feedback loop repeats and dies away, and can never run away")
{
    // input -> mix (A) -> output, and mix -> gain -> back into mix (B): an echo every 32 samples.
    auto pedal = makePatch();
    const auto mix = pedal->addNode (BlockType::mix, 0.4f, 0.5f);
    const auto gain = pedal->addNode (BlockType::gain, 0.7f, 0.8f);
    REQUIRE (pedal->connect (PatchPedal::inputSource, mix, 0));
    REQUIRE (pedal->connect (gain, mix, 1));
    REQUIRE (pedal->connect (mix, gain, 0));
    REQUIRE (pedal->connectOutput (mix));
    REQUIRE (pedal->isLoopWire (mix, 1));
    REQUIRE_FALSE (pedal->isLoopWire (gain, 0));

    pedal->getNode (mix).block.mix.set (50.0f);
    pedal->getNode (gain).block.gainDb.set (0.0f);
    pedal->reset();

    std::vector<float> impulse (4800, 0.0f);
    impulse[0] = 0.2f;
    auto out = run (*pedal, impulse);
    REQUIRE (test::allFinite (out));
    REQUIRE_THAT (out[0], Catch::Matchers::WithinAbs (0.1, 0.01));
    REQUIRE (std::abs (out[32]) > 0.03f);           // the first repeat
    REQUIRE (std::abs (out[32]) < std::abs (out[0]));
    REQUIRE (test::peak (out, 2400) < 0.001f);      // gone half a second later

    // Turn the loop gain right up: it stays bounded.
    pedal->getNode (gain).block.gainDb.set (24.0f);
    pedal->getNode (mix).block.mix.set (100.0f);
    out = run (*pedal, test::sine (440.0, 0.3f, sampleRate, 48000));
    REQUIRE (test::allFinite (out));
    REQUIRE (test::peak (out) < 20.0f);
}

TEST_CASE ("an LFO in a patch makes a tremolo on the block it moves")
{
    auto pedal = makePatch();
    const auto gain = pedal->addNode (BlockType::gain, 0.5f, 0.5f);
    const auto lfo = pedal->addNode (BlockType::lfo, 0.5f, 0.2f);
    REQUIRE (pedal->getNode (lfo).block.getTargetSlot() == gain);
    pedal->getNode (lfo).block.rate.set (5.0f);
    pedal->getNode (lfo).block.depth.set (100.0f);
    pedal->reset();

    const auto out = run (*pedal, test::sine (440.0, 0.2f, sampleRate, 48000));
    float low = 1.0f, high = 0.0f;

    for (size_t start = 4800; start + 480 < out.size(); start += 480)
    {
        float p = 0.0f;
        for (size_t i = start; i < start + 480; ++i)
            p = std::max (p, std::abs (out[i]));
        low = std::min (low, p);
        high = std::max (high, p);
    }

    REQUIRE (high > 0.3f);
    REQUIRE (low < 0.05f);
}

TEST_CASE ("rewiring a patch while playing never clicks")
{
    auto pedal = makePatch();
    const auto lp = pedal->addNode (BlockType::lowPass, 0.5f, 0.5f);
    pedal->getNode (lp).block.frequency.set (400.0f);
    pedal->reset();

    auto signal = test::sine (220.0, 0.2f, sampleRate, 48000);
    float biggestJump = 0.0f, last = 0.0f;

    for (size_t offset = 0; offset < signal.size(); offset += blockSize)
    {
        if (offset == 9600)
            pedal->connectOutput (PatchPedal::inputSource);
        if (offset == 19200)
            pedal->addNode (BlockType::clipper, 0.7f, 0.5f);
        if (offset == 28800)
            pedal->removeNode (lp);

        pedal->process (signal.data() + offset, blockSize);

        for (size_t i = offset; i < offset + blockSize; ++i)
        {
            if (offset > 0 || i > 0)
                biggestJump = std::max (biggestJump, std::abs (signal[i] - last));
            last = signal[i];
        }
    }

    // A 220 Hz sine at 0.2 moves at most about 0.006 per sample.
    REQUIRE (biggestJump < 0.03f);
}

TEST_CASE ("a patch and its wiring are saved in presets and come back")
{
    SignalChain chain;
    chain.prepare (sampleRate, 64);

    Pedalboard::PedalList before { std::make_shared<Pedal> (*findPedalType ("patch")) }, after;
    auto& patch = dynamic_cast<PatchPedal&> (before[0]->getEffect());
    patch.enabled.set (1.0f);
    const auto lp = patch.addNode (BlockType::lowPass, 0.25f, 0.75f);
    const auto mix = patch.addNode (BlockType::mix, 0.8f, 0.4f);
    patch.connect (PatchPedal::inputSource, mix, 1);
    patch.getNode (lp).block.frequency.set (640.0f);
    chain.getBoard().setPedals (before, after);

    const auto saved = PresetManager::capture (chain, "Patch test");

    Pedalboard::PedalList classicBefore, classicAfter;
    Pedalboard::makeClassicLayout (classicBefore, classicAfter);
    chain.getBoard().setPedals (classicBefore, classicAfter);
    REQUIRE (PresetManager::apply (chain, saved));

    const auto& pedals = chain.getBoard().getPedals (Pedalboard::beforeAmp);
    REQUIRE (pedals.size() == 1);
    auto& loaded = dynamic_cast<PatchPedal&> (pedals[0]->getEffect());
    REQUIRE (loaded.getNodesInUse() == std::vector<int> { lp, mix });
    REQUIRE (loaded.getNode (mix).getSource (0) == lp);
    REQUIRE (loaded.getNode (mix).getSource (1) == PatchPedal::inputSource);
    REQUIRE (loaded.getOutputSource() == mix);
    REQUIRE_THAT (loaded.getNode (lp).block.frequency.get(), Catch::Matchers::WithinAbs (640.0, 0.01));
    REQUIRE_THAT (loaded.getNode (lp).x.get(), Catch::Matchers::WithinAbs (0.25, 0.001));
}

TEST_CASE ("the patch response graph matches what you hear")
{
    auto pedal = makePatch();
    bool hasBlocks = true;
    pedal->getResponseDb ({ 1000.0f }, hasBlocks);
    REQUIRE_FALSE (hasBlocks);

    const auto lp = pedal->addNode (BlockType::lowPass, 0.3f, 0.3f);
    const auto mix = pedal->addNode (BlockType::mix, 0.7f, 0.5f);
    pedal->connect (PatchPedal::inputSource, lp, 0);
    pedal->connect (lp, mix, 0);
    pedal->connect (PatchPedal::inputSource, mix, 1);
    pedal->getNode (lp).block.frequency.set (300.0f);
    pedal->getNode (mix).block.mix.set (25.0f);

    const std::vector<float> frequencies { 100.0f, 1000.0f, 8000.0f };
    const auto db = pedal->getResponseDb (frequencies, hasBlocks);
    REQUIRE (hasBlocks);

    for (size_t i = 0; i < frequencies.size(); ++i)
        REQUIRE_THAT (db[i], Catch::Matchers::WithinAbs (responseDb (*pedal, frequencies[i]), 0.6));
}

TEST_CASE ("a full patch uses little processor time", "[.][performance]")
{
    auto pedal = makePatch();

    for (int i = 0; i < PatchPedal::maxNodes; ++i)
        pedal->addNode (i % 4 == 3 ? BlockType::clipper : BlockType::peak, 0.5f, 0.5f);

    auto signal = test::sine (220.0, 0.2f, sampleRate, (int) sampleRate * 10);
    const auto start = juce::Time::getMillisecondCounterHiRes();
    run (*pedal, signal);
    const auto share = (juce::Time::getMillisecondCounterHiRes() - start) / 10000.0 * 100.0;
    WARN ("16 blocks: " << share << "% of one core");
}
