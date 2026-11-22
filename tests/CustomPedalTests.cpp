#include "TestSignals.h"
#include "engine/Pedalboard.h"
#include "engine/Presets.h"
#include "engine/SignalChain.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <set>

/** The Custom pedal: blocks you chain yourself. */

namespace
{
using custom_blocks::BlockType;

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 64;

std::unique_ptr<CustomPedal> makeCustom()
{
    auto pedal = std::make_unique<CustomPedal>();
    pedal->enabled.set (1.0f);
    pedal->prepare (sampleRate, blockSize);
    return pedal;
}

std::vector<float> run (CustomPedal& pedal, std::vector<float> signal)
{
    for (size_t offset = 0; offset < signal.size(); offset += blockSize)
        pedal.process (signal.data() + offset, (int) juce::jmin ((size_t) blockSize, signal.size() - offset));

    return signal;
}

/** The level change, in dB, the pedal gives a sine at `hz` once it has settled. */
double responseDb (CustomPedal& pedal, double hz)
{
    pedal.reset();
    const auto in = test::sine (hz, 0.1f, sampleRate, 24000);
    const auto out = run (pedal, in);
    return juce::Decibels::gainToDecibels (test::rms (out, 12000) / test::rms (in, 12000));
}

/** A pedal holding one block of `type`, already switched in. */
std::unique_ptr<CustomPedal> withBlock (BlockType type)
{
    auto pedal = makeCustom();
    REQUIRE (pedal->addBlock (type) >= 0);
    pedal->reset();
    return pedal;
}
}

TEST_CASE ("the band readout gives the width around the centre")
{
    const auto band = custom_blocks::getBand (800.0f, 2.0f);
    CHECK_THAT (band.getWidth(), Catch::Matchers::WithinAbs (400.0f, 0.1f));
    CHECK_THAT (band.lowHz * band.highHz, Catch::Matchers::WithinRel (800.0f * 800.0f, 0.001f));
    CHECK (custom_blocks::describeBand (800.0f, 2.0f) == "Width: about 400 Hz\n625 Hz to 1.02 kHz");

    // A higher Narrowness is a thinner band, and the top stops at 20 kHz.
    CHECK (custom_blocks::getBand (800.0f, 8.0f).getWidth() < band.getWidth());
    CHECK (custom_blocks::getBand (18000.0f, 0.3f).highHz == 20000.0f);
}

TEST_CASE ("the band readout matches where a band-pass block falls 3 dB")
{
    auto pedal = withBlock (BlockType::bandPass);
    auto& slot = pedal->getSlot (pedal->getBlocksInOrder().front());
    slot.frequency.set (800.0f);
    slot.resonance.set (2.0f);

    const auto band = custom_blocks::getBand (800.0f, 2.0f);
    CHECK_THAT (responseDb (*pedal, band.lowHz), Catch::Matchers::WithinAbs (-3.0, 0.25));
    CHECK_THAT (responseDb (*pedal, band.highHz), Catch::Matchers::WithinAbs (-3.0, 0.25));
}

TEST_CASE ("an empty custom pedal passes the signal through untouched")
{
    auto pedal = makeCustom();
    const auto in = test::sine (440.0, 0.3f, sampleRate, 4800);
    const auto out = run (*pedal, in);

    for (size_t i = 0; i < in.size(); ++i)
        REQUIRE_THAT (out[i], Catch::Matchers::WithinAbs (in[i], 1.0e-6));
}

TEST_CASE ("the block order packs into one number and back")
{
    std::set<int> seen;

    for (int code = 0; code < 40320; code += 37)
    {
        const auto order = CustomPedal::decodeOrder (code);
        std::set<int> slots (order.begin(), order.end());
        REQUIRE (slots.size() == (size_t) CustomPedal::maxBlocks);
        REQUIRE (CustomPedal::encodeOrder (order) == code);
        seen.insert (code);
    }

    REQUIRE (CustomPedal::encodeOrder ({ 0, 1, 2, 3, 4, 5, 6, 7 }) == 0);
    REQUIRE (CustomPedal::encodeOrder ({ 7, 6, 5, 4, 3, 2, 1, 0 }) == 40319);
}

TEST_CASE ("low-pass and high-pass blocks keep their own side of the cutoff")
{
    auto low = withBlock (BlockType::lowPass);
    low->getSlot (low->getBlocksInOrder()[0]).frequency.set (1000.0f);
    low->reset();
    REQUIRE (std::abs (responseDb (*low, 100.0)) < 0.5);
    REQUIRE_THAT (responseDb (*low, 1000.0), Catch::Matchers::WithinAbs (-3.0, 0.6));
    REQUIRE (responseDb (*low, 8000.0) < -30.0);

    auto high = withBlock (BlockType::highPass);
    high->getSlot (high->getBlocksInOrder()[0]).frequency.set (1000.0f);
    high->reset();
    REQUIRE (std::abs (responseDb (*high, 10000.0)) < 0.5);
    REQUIRE_THAT (responseDb (*high, 1000.0), Catch::Matchers::WithinAbs (-3.0, 0.6));
    REQUIRE (responseDb (*high, 125.0) < -30.0);
}

TEST_CASE ("the 24 dB slope cuts twice as steeply as 12 dB")
{
    auto pedal = withBlock (BlockType::lowPass);
    auto& slot = pedal->getSlot (pedal->getBlocksInOrder()[0]);
    slot.frequency.set (500.0f);
    pedal->reset();
    const auto gentle = responseDb (*pedal, 4000.0);

    slot.slope.set (1.0f);
    pedal->reset();
    const auto steep = responseDb (*pedal, 4000.0);

    REQUIRE_THAT (gentle, Catch::Matchers::WithinAbs (-36.0, 2.0));   // 3 octaves at 12 dB
    REQUIRE_THAT (steep, Catch::Matchers::WithinAbs (-72.0, 3.0));    // 3 octaves at 24 dB
    REQUIRE_THAT (responseDb (*pedal, 500.0), Catch::Matchers::WithinAbs (-3.0, 0.6));
}

TEST_CASE ("resonance adds a peak at the cutoff")
{
    auto pedal = withBlock (BlockType::lowPass);
    auto& slot = pedal->getSlot (pedal->getBlocksInOrder()[0]);
    slot.frequency.set (1000.0f);
    slot.resonance.set (4.0f);
    pedal->reset();
    REQUIRE_THAT (responseDb (*pedal, 1000.0), Catch::Matchers::WithinAbs (12.0, 0.6));   // 20 log10 (4)
}

TEST_CASE ("band-pass and notch blocks act around their centre")
{
    auto band = withBlock (BlockType::bandPass);
    band->getSlot (band->getBlocksInOrder()[0]).frequency.set (1000.0f);
    band->reset();
    REQUIRE (std::abs (responseDb (*band, 1000.0)) < 0.5);
    REQUIRE (responseDb (*band, 100.0) < -12.0);
    REQUIRE (responseDb (*band, 10000.0) < -12.0);

    auto notch = withBlock (BlockType::notch);
    notch->getSlot (notch->getBlocksInOrder()[0]).frequency.set (1000.0f);
    notch->reset();
    REQUIRE (responseDb (*notch, 1000.0) < -30.0);
    REQUIRE (std::abs (responseDb (*notch, 100.0)) < 0.5);
    REQUIRE (std::abs (responseDb (*notch, 10000.0)) < 0.5);
}

TEST_CASE ("peak and shelf blocks boost and cut by the amount set")
{
    for (auto gain : { 9.0f, -9.0f })
    {
        auto peak = withBlock (BlockType::peak);
        auto& p = peak->getSlot (peak->getBlocksInOrder()[0]);
        p.frequency.set (1000.0f);
        p.gainDb.set (gain);
        peak->reset();
        REQUIRE_THAT (responseDb (*peak, 1000.0), Catch::Matchers::WithinAbs (gain, 0.3));
        REQUIRE (std::abs (responseDb (*peak, 60.0)) < 0.6);

        auto lowShelf = withBlock (BlockType::lowShelf);
        auto& l = lowShelf->getSlot (lowShelf->getBlocksInOrder()[0]);
        l.frequency.set (300.0f);
        l.gainDb.set (gain);
        lowShelf->reset();
        REQUIRE_THAT (responseDb (*lowShelf, 40.0), Catch::Matchers::WithinAbs (gain, 0.5));
        REQUIRE (std::abs (responseDb (*lowShelf, 8000.0)) < 0.5);

        auto highShelf = withBlock (BlockType::highShelf);
        auto& h = highShelf->getSlot (highShelf->getBlocksInOrder()[0]);
        h.frequency.set (2000.0f);
        h.gainDb.set (gain);
        highShelf->reset();
        REQUIRE_THAT (responseDb (*highShelf, 16000.0), Catch::Matchers::WithinAbs (gain, 0.6));
        REQUIRE (std::abs (responseDb (*highShelf, 100.0)) < 0.5);
    }
}

TEST_CASE ("the gain block scales and inverts")
{
    auto pedal = withBlock (BlockType::gain);
    auto& slot = pedal->getSlot (pedal->getBlocksInOrder()[0]);
    slot.gainDb.set (-6.0f);
    slot.invert.set (1.0f);
    pedal->reset();

    const auto in = test::sine (440.0, 0.3f, sampleRate, 4800);
    const auto out = run (*pedal, in);

    for (size_t i = 0; i < in.size(); ++i)
        REQUIRE_THAT (out[i], Catch::Matchers::WithinAbs (-0.501187f * in[i], 1.0e-4));
}

TEST_CASE ("the clipper adds distortion and stays under its ceiling")
{
    for (int shape = 0; shape < 4; ++shape)
    {
        auto pedal = withBlock (BlockType::clipper);
        auto& slot = pedal->getSlot (pedal->getBlocksInOrder()[0]);
        slot.shape.set ((float) shape);
        slot.driveDb.set (30.0f);
        slot.gainDb.set (0.0f);
        pedal->reset();

        const auto in = test::sine (220.0, 0.5f, sampleRate, 48000);
        const auto out = run (*pedal, in);
        INFO ("shape " << shape);
        REQUIRE (test::allFinite (out));
        REQUIRE (test::peak (out, 24000) < 0.3f);

        // A sine clipped hard is close to a square: its RMS rises towards its peak.
        const auto crest = test::peak (out, 24000) / test::rms (out, 24000);
        REQUIRE (crest < 1.25);
    }
}

TEST_CASE ("blocks run in the order set, and the order matters")
{
    // Clipping before a low-pass sounds different from a low-pass before clipping.
    auto pedal = makeCustom();
    const auto filter = pedal->addBlock (BlockType::lowPass);
    const auto clipper = pedal->addBlock (BlockType::clipper);
    pedal->getSlot (filter).frequency.set (400.0f);
    pedal->getSlot (clipper).driveDb.set (30.0f);
    pedal->reset();
    REQUIRE (pedal->getBlocksInOrder() == std::vector<int> { filter, clipper });

    const auto in = test::sine (220.0, 0.3f, sampleRate, 24000);
    const auto filterFirst = run (*pedal, in);

    pedal->moveBlock (1, 0);
    REQUIRE (pedal->getBlocksInOrder() == std::vector<int> { clipper, filter });
    pedal->reset();
    const auto clipFirst = run (*pedal, in);

    // Filtering last removes the clipper's buzz, so far less high end remains.
    // The sample-to-sample change is a rough measure of high end.
    const auto brightness = [] (const std::vector<float>& signal)
    {
        double sum = 0.0;

        for (size_t i = 12001; i < signal.size(); ++i)
            sum += (double) (signal[i] - signal[i - 1]) * (signal[i] - signal[i - 1]);

        return std::sqrt (sum / (double) (signal.size() - 12001)) / test::rms (signal, 12000);
    };

    REQUIRE (brightness (clipFirst) < 0.5 * brightness (filterFirst));
}

TEST_CASE ("adding and removing blocks keeps the rest in order")
{
    auto pedal = makeCustom();
    std::vector<int> added;

    for (int i = 0; i < CustomPedal::maxBlocks; ++i)
        added.push_back (pedal->addBlock (BlockType::gain));

    REQUIRE (pedal->addBlock (BlockType::gain) == -1);
    REQUIRE (pedal->getBlocksInOrder() == added);

    pedal->removeBlock (added[2]);
    added.erase (added.begin() + 2);
    REQUIRE (pedal->getBlocksInOrder() == added);

    // The freed slot is reused, at the end, with fresh settings.
    pedal->getSlot (added[0]).gainDb.set (-12.0f);
    const auto again = pedal->addBlock (BlockType::highPass);
    added.push_back (again);
    REQUIRE (pedal->getBlocksInOrder() == added);
    REQUIRE_THAT (pedal->getSlot (again).gainDb.get(), Catch::Matchers::WithinAbs (0.0, 1.0e-6));
}

TEST_CASE ("changing blocks while playing never clicks")
{
    // A steady low note through a gain block; then a filter is added, blocks
    // are reordered and removed mid-stream. No sample may jump much more than
    // the sine itself does from one sample to the next.
    auto pedal = makeCustom();
    pedal->addBlock (BlockType::gain);
    pedal->reset();

    const auto in = test::sine (110.0, 0.3f, sampleRate, 48000);
    std::vector<float> out = in;
    const auto maxStep = 0.3f * juce::MathConstants<float>::twoPi * 110.0f / (float) sampleRate;

    for (size_t offset = 0; offset < out.size(); offset += blockSize)
    {
        if (offset == 4800)   pedal->addBlock (BlockType::highPass);
        if (offset == 14400)  pedal->moveBlock (1, 0);
        if (offset == 24000)  pedal->addBlock (BlockType::peak);
        if (offset == 33600)  pedal->removeBlock (pedal->getBlocksInOrder()[0]);

        pedal->process (out.data() + offset, blockSize);
    }

    float worst = 0.0f;

    for (size_t i = 1; i < out.size(); ++i)
        worst = juce::jmax (worst, std::abs (out[i] - out[i - 1]));

    REQUIRE (worst < maxStep * 2.0f);
}

TEST_CASE ("after the amp, the hidden twin follows the blocks on the right side")
{
    Pedal pedal (*findPedalType ("custom"));
    auto& custom = dynamic_cast<CustomPedal&> (pedal.getEffect());
    custom.enabled.set (1.0f);
    pedal.prepare (sampleRate, blockSize);

    const auto slot = custom.addBlock (BlockType::highPass);
    custom.getSlot (slot).frequency.set (2000.0f);

    auto left = test::sine (150.0, 0.3f, sampleRate, 24000);
    auto right = left;

    for (size_t offset = 0; offset < left.size(); offset += blockSize)
        pedal.processStereo (left.data() + offset, right.data() + offset, blockSize);

    // Both sides are filtered alike.
    REQUIRE (test::rms (left, 12000) < 0.01);
    REQUIRE_THAT (test::rms (right, 12000), Catch::Matchers::WithinAbs (test::rms (left, 12000), 1.0e-4));
}

TEST_CASE ("a custom pedal's blocks are saved in presets and come back")
{
    SignalChain chain;
    chain.prepare (sampleRate, blockSize);

    Pedalboard::PedalList before { std::make_shared<Pedal> (*findPedalType ("custom")) }, after;
    auto& custom = dynamic_cast<CustomPedal&> (before[0]->getEffect());
    custom.enabled.set (1.0f);
    const auto clip = custom.addBlock (BlockType::clipper);
    const auto filter = custom.addBlock (BlockType::bandPass);
    custom.getSlot (clip).driveDb.set (22.0f);
    custom.getSlot (filter).frequency.set (640.0f);
    custom.moveBlock (1, 0);
    custom.level.set (-3.0f);
    chain.getBoard().setPedals (before, after);

    const auto saved = PresetManager::capture (chain, "Custom test");

    // Reload into a fresh board with the classic pedals.
    Pedalboard::PedalList classicBefore, classicAfter;
    Pedalboard::makeClassicLayout (classicBefore, classicAfter);
    chain.getBoard().setPedals (classicBefore, classicAfter);
    REQUIRE (PresetManager::apply (chain, saved));

    const auto& pedals = chain.getBoard().getPedals (Pedalboard::beforeAmp);
    REQUIRE (pedals.size() == 1);
    REQUIRE (juce::String (pedals[0]->type.key) == "custom");

    auto& loaded = dynamic_cast<CustomPedal&> (pedals[0]->getEffect());
    REQUIRE (loaded.getBlocksInOrder() == std::vector<int> { filter, clip });
    REQUIRE (loaded.getSlot (filter).getType() == BlockType::bandPass);
    REQUIRE_THAT (loaded.getSlot (filter).frequency.get(), Catch::Matchers::WithinAbs (640.0, 0.01));
    REQUIRE_THAT (loaded.getSlot (clip).driveDb.get(), Catch::Matchers::WithinAbs (22.0, 0.01));
    REQUIRE_THAT (loaded.level.get(), Catch::Matchers::WithinAbs (-3.0, 0.01));
}

TEST_CASE ("a full custom pedal uses little processor time", "[.][performance]")
{
    auto pedal = makeCustom();

    for (auto type : { BlockType::highPass, BlockType::peak, BlockType::clipper, BlockType::lowPass,
                       BlockType::bandPass, BlockType::lowShelf, BlockType::highShelf, BlockType::gain })
        pedal->addBlock (type);

    pedal->reset();
    auto signal = test::sine (220.0, 0.3f, sampleRate, (int) sampleRate * 10);

    const auto start = juce::Time::getMillisecondCounterHiRes();
    run (*pedal, signal);
    const auto ms = juce::Time::getMillisecondCounterHiRes() - start;

    const auto share = ms / 10000.0 * 100.0;
    WARN ("8 blocks: " << share << "% of one core");
    REQUIRE (share < 5.0);
}
