#include "TestSignals.h"
#include "engine/CustomPedalLibrary.h"
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

    for (int code = 0; code < 3628800; code += 3637)
    {
        const auto order = CustomPedal::decodeOrder (code);
        std::set<int> slots (order.begin(), order.end());
        REQUIRE (slots.size() == (size_t) CustomPedal::maxBlocks);
        REQUIRE (CustomPedal::encodeOrder (order) == code);
        seen.insert (code);
    }

    REQUIRE (CustomPedal::encodeOrder ({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 }) == 0);
    REQUIRE (CustomPedal::encodeOrder ({ 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 }) == 3628799);
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
    custom.label = "Fuzz wah";
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
    REQUIRE (loaded.label == "Fuzz wah");
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

//==============================================================================
TEST_CASE ("a new modulator moves the block before it, and removing that block frees it")
{
    auto pedal = makeCustom();
    const auto filter = pedal->addBlock (BlockType::lowPass);
    const auto lfo = pedal->addBlock (BlockType::lfo);
    REQUIRE (pedal->getSlot (lfo).getTargetSlot() == filter);

    // Modulators cannot move themselves or another modulator.
    const auto env = pedal->addBlock (BlockType::envelope);
    REQUIRE (pedal->getSlot (env).getTargetSlot() == filter);
    REQUIRE_FALSE (pedal->setTarget (env, lfo));
    REQUIRE_FALSE (pedal->setTarget (env, env));

    pedal->removeBlock (filter);
    REQUIRE (pedal->getSlot (lfo).getTargetSlot() == -1);
    REQUIRE (pedal->getSlot (env).getTargetSlot() == -1);
}

TEST_CASE ("modulators make no sound of their own")
{
    auto pedal = makeCustom();
    pedal->addBlock (BlockType::lfo);
    pedal->addBlock (BlockType::envelope);
    pedal->reset();

    const auto in = test::sine (440.0, 0.3f, sampleRate, 4800);
    const auto out = run (*pedal, in);

    for (size_t i = 0; i < in.size(); ++i)
        REQUIRE_THAT (out[i], Catch::Matchers::WithinAbs (in[i], 1.0e-6));
}

TEST_CASE ("an LFO on a gain block makes a tremolo at its rate")
{
    auto pedal = makeCustom();
    pedal->addBlock (BlockType::gain);
    const auto lfo = pedal->addBlock (BlockType::lfo);
    pedal->getSlot (lfo).rate.set (4.0f);
    pedal->getSlot (lfo).depth.set (100.0f);
    pedal->reset();

    const auto out = run (*pedal, test::sine (1000.0, 0.2f, sampleRate, 48000));

    // Peak level in 5 ms windows swings from near silence to about double.
    float lowest = 1.0f, highest = 0.0f;

    for (size_t start = 2400; start + 240 <= out.size(); start += 240)
    {
        float windowPeak = 0.0f;

        for (size_t i = start; i < start + 240; ++i)
            windowPeak = juce::jmax (windowPeak, std::abs (out[i]));

        lowest = juce::jmin (lowest, windowPeak);
        highest = juce::jmax (highest, windowPeak);
    }

    REQUIRE (lowest < 0.03f);
    REQUIRE (highest > 0.35f);
}

TEST_CASE ("an LFO sweeps a filter's cutoff")
{
    auto pedal = makeCustom();
    const auto filter = pedal->addBlock (BlockType::lowPass);
    pedal->getSlot (filter).frequency.set (1000.0f);
    const auto lfo = pedal->addBlock (BlockType::lfo);
    pedal->getSlot (lfo).rate.set (2.0f);
    pedal->getSlot (lfo).depth.set (100.0f);
    pedal->reset();

    // A 2 kHz tone is cut hard when the cutoff dips to 125 Hz and passes when it rises to 8 kHz.
    const auto out = run (*pedal, test::sine (2000.0, 0.2f, sampleRate, 48000));
    float lowest = 1.0f, highest = 0.0f;

    for (size_t start = 4800; start + 480 <= out.size(); start += 480)
    {
        float windowPeak = 0.0f;

        for (size_t i = start; i < start + 480; ++i)
            windowPeak = juce::jmax (windowPeak, std::abs (out[i]));

        lowest = juce::jmin (lowest, windowPeak);
        highest = juce::jmax (highest, windowPeak);
    }

    REQUIRE (lowest < 0.01f);
    REQUIRE (highest > 0.18f);
    REQUIRE (test::allFinite (out));
}

TEST_CASE ("an envelope opens a filter when you play harder")
{
    auto pedal = makeCustom();
    const auto filter = pedal->addBlock (BlockType::lowPass);
    pedal->getSlot (filter).frequency.set (300.0f);
    const auto env = pedal->addBlock (BlockType::envelope);
    pedal->getSlot (env).depth.set (100.0f);

    const auto levelOf = [&] (float amplitude)
    {
        pedal->reset();
        const auto in = test::sine (2000.0, amplitude, sampleRate, 24000);
        const auto out = run (*pedal, in);
        return test::rms (out, 12000) / test::rms (in, 12000);
    };

    // Soft playing stays dark; hard playing lets the 2 kHz tone through much more.
    REQUIRE (levelOf (0.3f) > 8.0 * levelOf (0.005f));
}

TEST_CASE ("modulators are saved in presets with their targets")
{
    SignalChain chain;
    chain.prepare (sampleRate, blockSize);

    Pedalboard::PedalList before { std::make_shared<Pedal> (*findPedalType ("custom")) }, after;
    auto& custom = dynamic_cast<CustomPedal&> (before[0]->getEffect());
    const auto filter = custom.addBlock (BlockType::bandPass);
    const auto env = custom.addBlock (BlockType::envelope);
    custom.getSlot (env).release.set (300.0f);
    chain.getBoard().setPedals (before, after);

    const auto saved = PresetManager::capture (chain, "Auto-wah");
    Pedalboard::PedalList empty;
    chain.getBoard().setPedals (empty, empty);
    REQUIRE (PresetManager::apply (chain, saved));

    auto& loaded = dynamic_cast<CustomPedal&> (chain.getBoard().getPedals (Pedalboard::beforeAmp)[0]->getEffect());
    REQUIRE (loaded.getSlot (env).getType() == BlockType::envelope);
    REQUIRE (loaded.getSlot (env).getTargetSlot() == filter);
    REQUIRE_THAT (loaded.getSlot (env).release.get(), Catch::Matchers::WithinAbs (300.0, 0.01));
}

TEST_CASE ("adding a modulator while playing never clicks")
{
    auto pedal = makeCustom();
    const auto filter = pedal->addBlock (BlockType::lowPass);
    pedal->getSlot (filter).frequency.set (2000.0f);
    pedal->reset();

    auto out = test::sine (110.0, 0.3f, sampleRate, 48000);
    const auto maxStep = 0.3f * juce::MathConstants<float>::twoPi * 110.0f / (float) sampleRate;

    for (size_t offset = 0; offset < out.size(); offset += blockSize)
    {
        if (offset == 4800)   pedal->addBlock (BlockType::lfo);
        if (offset == 24000)  pedal->removeBlock (pedal->getBlocksInOrder()[1]);

        pedal->process (out.data() + offset, blockSize);
    }

    float worst = 0.0f;

    for (size_t i = 1; i < out.size(); ++i)
        worst = juce::jmax (worst, std::abs (out[i] - out[i - 1]));

    REQUIRE (worst < maxStep * 2.0f);
}

//==============================================================================
// Milestone 3: delay, all-pass, reducer, two paths, saved pedals, the graph.

namespace
{
std::vector<float> impulse (int numSamples)
{
    std::vector<float> signal ((size_t) numSamples, 0.0f);
    signal[0] = 1.0f;
    return signal;
}
}

TEST_CASE ("the delay block repeats the signal after its time, fading with feedback")
{
    auto pedal = makeCustom();
    const auto slot = pedal->addBlock (BlockType::delay);
    auto& s = pedal->getSlot (slot);
    s.time.set (10.0f);
    s.feedback.set (50.0f);
    s.mix.set (100.0f);
    pedal->reset();

    const auto out = run (*pedal, impulse (4800));
    REQUIRE_THAT (out[0], Catch::Matchers::WithinAbs (1.0, 0.01));
    REQUIRE_THAT (out[480], Catch::Matchers::WithinAbs (1.0, 0.02));
    REQUIRE_THAT (out[960], Catch::Matchers::WithinAbs (0.5, 0.02));
    REQUIRE_THAT (out[1440], Catch::Matchers::WithinAbs (0.25, 0.02));
    REQUIRE (std::abs (out[240]) < 0.001f);
}

TEST_CASE ("the all-pass block cuts a notch at its frequency")
{
    auto pedal = withBlock (BlockType::allPass);
    auto& s = pedal->getSlot (pedal->getBlocksInOrder()[0]);
    s.frequency.set (1000.0f);
    s.stages.set (0.0f);   // two stages: half a turn of phase at the frequency
    s.mix.set (100.0f);
    pedal->reset();

    REQUIRE (responseDb (*pedal, 1000.0) < -20.0);
    REQUIRE (responseDb (*pedal, 100.0) > -1.0);

    s.mix.set (0.0f);
    REQUIRE (std::abs (responseDb (*pedal, 1000.0)) < 0.5);
}

TEST_CASE ("the reducer block steps the level and holds samples")
{
    auto pedal = withBlock (BlockType::reducer);
    auto& s = pedal->getSlot (pedal->getBlocksInOrder()[0]);
    s.bits.set (3.0f);
    s.downsample.set (4.0f);
    s.mix.set (100.0f);
    pedal->reset();

    const auto out = run (*pedal, test::sine (440.0, 0.8f, sampleRate, 4800));
    std::set<float> levels (out.begin() + 2400, out.end());
    REQUIRE (levels.size() <= 9);

    for (size_t i = 2400; i + 4 <= out.size(); i += 4)
        REQUIRE (std::abs (out[i] - out[i + 3]) < 1.0e-6f);
}

TEST_CASE ("split, path B and mix run two paths and blend them")
{
    SECTION ("without a Path B, path B is the clean signal")
    {
        auto pedal = makeCustom();
        pedal->addBlock (BlockType::split);
        const auto filter = pedal->addBlock (BlockType::lowPass);
        const auto mix = pedal->addBlock (BlockType::mix);
        pedal->getSlot (filter).frequency.set (200.0f);

        pedal->getSlot (mix).mix.set (0.0f);
        pedal->reset();
        REQUIRE (responseDb (*pedal, 5000.0) < -24.0);

        pedal->getSlot (mix).mix.set (100.0f);
        pedal->reset();
        REQUIRE (std::abs (responseDb (*pedal, 5000.0)) < 0.5);
    }

    SECTION ("an inverted path B blended evenly cancels path A")
    {
        auto pedal = makeCustom();
        pedal->addBlock (BlockType::split);
        pedal->addBlock (BlockType::pathB);
        const auto gain = pedal->addBlock (BlockType::gain);
        const auto mix = pedal->addBlock (BlockType::mix);
        pedal->getSlot (gain).invert.set (1.0f);
        pedal->getSlot (gain).gainDb.set (0.0f);
        pedal->getSlot (mix).mix.set (50.0f);
        pedal->reset();

        REQUIRE (responseDb (*pedal, 440.0) < -60.0);
    }
}

TEST_CASE ("a block order saved by the first 8-block builds still loads")
{
    auto pedal = makeCustom();
    const auto first = pedal->addBlock (BlockType::lowPass);
    const auto second = pedal->addBlock (BlockType::clipper);
    REQUIRE (pedal->getBlocksInOrder() == std::vector<int> { first, second });

    // The old form: only `order` set, reversing the first eight slots.
    pedal->order2.set (0.0f);
    pedal->order.set (40319.0f);
    REQUIRE (pedal->getBlocksInOrder() == std::vector<int> { second, first });

    // A new edit moves to the new form.
    pedal->moveBlock (0, 1);
    REQUIRE (pedal->order.getIndex() == 0);
    REQUIRE (pedal->getBlocksInOrder() == std::vector<int> { first, second });
}

TEST_CASE ("custom pedals saved by name load back with their name")
{
    const auto folder = juce::File::createTempFile ("custom-pedals");
    CustomPedalLibrary library (folder);

    CustomPedal original;
    const auto filter = original.addBlock (BlockType::bandPass);
    original.addBlock (BlockType::delay);
    original.getSlot (filter).frequency.set (777.0f);
    original.level.set (-5.0f);

    REQUIRE (library.save ("Wobbly echo", original).isEmpty());
    REQUIRE (original.label == "Wobbly echo");
    REQUIRE (library.getNames().contains ("Wobbly echo"));

    CustomPedal copy;
    REQUIRE (library.load ("Wobbly echo", copy));
    REQUIRE (copy.label == "Wobbly echo");
    REQUIRE (copy.getBlocksInOrder() == original.getBlocksInOrder());
    REQUIRE_THAT (copy.getSlot (filter).frequency.get(), Catch::Matchers::WithinAbs (777.0, 0.01));
    REQUIRE_THAT (copy.level.get(), Catch::Matchers::WithinAbs (-5.0, 0.01));

    REQUIRE_FALSE (library.load ("Not there", copy));
    REQUIRE (library.remove ("Wobbly echo").isEmpty());
    REQUIRE_FALSE (library.getNames().contains ("Wobbly echo"));
    folder.deleteRecursively();
}

TEST_CASE ("the response graph follows the filter blocks")
{
    auto pedal = makeCustom();
    bool hasFilters = true;
    const std::vector<float> frequencies { 100.0f, 1000.0f, 10000.0f };

    pedal->getResponseDb (frequencies, hasFilters);
    REQUIRE_FALSE (hasFilters);

    const auto filter = pedal->addBlock (BlockType::lowPass);
    pedal->getSlot (filter).frequency.set (1000.0f);
    const auto gain = pedal->addBlock (BlockType::gain);
    pedal->getSlot (gain).gainDb.set (6.0f);
    const auto db = pedal->getResponseDb (frequencies, hasFilters);

    REQUIRE (hasFilters);
    REQUIRE_THAT (db[0], Catch::Matchers::WithinAbs (6.0, 0.5));
    REQUIRE_THAT (db[1], Catch::Matchers::WithinAbs (3.0, 0.5));   // -3 dB at the cutoff, +6 from the gain
    REQUIRE (db[2] < -30.0f);
}

TEST_CASE ("a split comes with its lower path and +, and goes with them")
{
    auto pedal = makeCustom();
    const auto filter = pedal->addBlock (BlockType::lowPass);
    const auto split = pedal->addSplit();
    REQUIRE (split >= 0);

    auto order = pedal->getBlocksInOrder();
    REQUIRE (order.size() == 4);
    REQUIRE (pedal->getSlot (order[1]).getType() == BlockType::split);
    REQUIRE (pedal->getSlot (order[2]).getType() == BlockType::pathB);
    REQUIRE (pedal->getSlot (order[3]).getType() == BlockType::mix);

    // A block dropped onto the lower path: after the hidden Path B marker.
    const auto delay = pedal->addBlock (BlockType::delay);
    pedal->moveBlock (4, 3);
    REQUIRE (pedal->getSplitGroup (order[3]) == std::vector<int> { order[1], order[2], order[3] });

    // Removing the + removes the whole branch, keeping the blocks in line.
    pedal->removeBlock (order[3]);
    REQUIRE (pedal->getBlocksInOrder() == std::vector<int> { filter, delay });

    // No room for three markers: nothing is added.
    for (int i = 0; i < 6; ++i)
        pedal->addBlock (BlockType::gain);

    REQUIRE (pedal->addSplit() == -1);
    REQUIRE (pedal->getBlocksInOrder().size() == 8);
}

TEST_CASE ("the response graph follows split paths and the + blend")
{
    // Low-pass on the top path, clean bottom path, blended evenly: the highs
    // come back at half level (-6 dB) instead of disappearing.
    auto pedal = makeCustom();
    pedal->addSplit();
    auto order = pedal->getBlocksInOrder();   // split, path B, mix
    const auto filter = pedal->addBlock (BlockType::lowPass);
    pedal->getSlot (filter).frequency.set (300.0f);
    pedal->moveBlock (3, 1);                  // onto the top path
    order = pedal->getBlocksInOrder();
    REQUIRE (pedal->getSlot (order[3]).getType() == BlockType::mix);
    pedal->getSlot (order[3]).mix.set (50.0f);

    bool hasFilters = false;
    const std::vector<float> frequencies { 50.0f, 10000.0f };
    auto db = pedal->getResponseDb (frequencies, hasFilters);
    REQUIRE (hasFilters);
    REQUIRE_THAT (db[0], Catch::Matchers::WithinAbs (0.0, 0.5));
    REQUIRE_THAT (db[1], Catch::Matchers::WithinAbs (-6.0, 0.5));

    // The graph agrees with the sound.
    pedal->reset();
    REQUIRE_THAT (responseDb (*pedal, 10000.0), Catch::Matchers::WithinAbs (db[1], 0.5));

    // All of the top path: the highs are gone.
    pedal->getSlot (order[3]).mix.set (0.0f);
    db = pedal->getResponseDb (frequencies, hasFilters);
    REQUIRE (db[1] < -30.0f);
}
