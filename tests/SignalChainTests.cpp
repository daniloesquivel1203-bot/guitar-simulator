#include "engine/SignalChain.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <vector>

using Catch::Matchers::WithinAbs;

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 128;

struct Rig
{
    Rig()
    {
        chain.prepare (sampleRate, blockSize);
    }

    /** Runs enough blocks of a constant input for gain smoothing to settle,
        and leaves the last block in `left` and `right`. */
    void run (float inputValue, bool withInput = true)
    {
        input.assign ((size_t) blockSize, inputValue);

        for (int block = 0; block < 40; ++block)
            chain.process (withInput ? input.data() : nullptr, left.data(), right.data(), blockSize);
    }

    SignalChain chain;
    std::vector<float> input;
    std::vector<float> left = std::vector<float> ((size_t) blockSize, 99.0f);
    std::vector<float> right = std::vector<float> ((size_t) blockSize, 99.0f);
};
}

TEST_CASE ("mono input is copied unchanged to both outputs at unity gain")
{
    Rig rig;
    rig.run (0.25f);

    for (int i = 0; i < blockSize; ++i)
    {
        REQUIRE_THAT (rig.left[(size_t) i], WithinAbs (0.25, 1.0e-6));
        REQUIRE_THAT (rig.right[(size_t) i], WithinAbs (0.25, 1.0e-6));
    }
}

TEST_CASE ("input and output gains are applied in decibels")
{
    Rig rig;
    rig.chain.setInputGainDb (6.0f);
    rig.chain.setOutputGainDb (-12.0f);
    rig.run (0.25f);

    const auto expected = 0.25 * juce::Decibels::decibelsToGain (-6.0);
    REQUIRE_THAT (rig.left.back(), WithinAbs (expected, 1.0e-5));
    REQUIRE_THAT (rig.right.back(), WithinAbs (expected, 1.0e-5));
}

TEST_CASE ("gain changes are smoothed instead of jumping")
{
    Rig rig;
    rig.run (0.5f);

    rig.chain.setOutputGainDb (-60.0f);
    rig.chain.process (rig.input.data(), rig.left.data(), rig.right.data(), blockSize);

    for (int i = 1; i < blockSize; ++i)
        REQUIRE (std::abs (rig.left[(size_t) i] - rig.left[(size_t) i - 1]) < 0.01f);

    REQUIRE (rig.left.front() > 0.4f);
}

TEST_CASE ("mute silences the output and unmute restores it")
{
    Rig rig;
    rig.chain.setMuted (true);
    rig.run (0.5f);
    REQUIRE_THAT (rig.left.back(), WithinAbs (0.0, 1.0e-7));

    rig.chain.setMuted (false);
    rig.run (0.5f);
    REQUIRE_THAT (rig.left.back(), WithinAbs (0.5, 1.0e-6));
}

TEST_CASE ("a missing input produces silence")
{
    Rig rig;
    rig.run (0.0f, false);

    for (int i = 0; i < blockSize; ++i)
    {
        REQUIRE_THAT (rig.left[(size_t) i], WithinAbs (0.0, 0.0));
        REQUIRE_THAT (rig.right[(size_t) i], WithinAbs (0.0, 0.0));
    }
}

TEST_CASE ("output never exceeds full scale")
{
    Rig rig;
    rig.chain.setInputGainDb (24.0f);
    rig.chain.setOutputGainDb (6.0f);
    rig.run (-0.9f);

    REQUIRE_THAT (rig.left.back(), WithinAbs (-1.0, 0.0));
    REQUIRE_THAT (rig.right.back(), WithinAbs (-1.0, 0.0));
}

TEST_CASE ("left and right may share one buffer")
{
    Rig rig;
    rig.input.assign ((size_t) blockSize, 0.3f);

    for (int block = 0; block < 40; ++block)
        rig.chain.process (rig.input.data(), rig.left.data(), rig.left.data(), blockSize);

    REQUIRE_THAT (rig.left.back(), WithinAbs (0.3, 1.0e-6));
}

TEST_CASE ("meters report peaks since the last read and then clear")
{
    Rig rig;
    rig.chain.setOutputGainDb (-6.0f);
    rig.run (0.5f);

    REQUIRE_THAT (rig.chain.readInputPeak(), WithinAbs (0.5, 1.0e-5));
    REQUIRE (rig.chain.readOutputPeak() >= 0.25f);
    REQUIRE_THAT (rig.chain.readInputPeak(), WithinAbs (0.0, 0.0));
    REQUIRE_THAT (rig.chain.readOutputPeak(), WithinAbs (0.0, 0.0));
}

TEST_CASE ("the clip flag follows the raw input, not the trimmed level")
{
    Rig rig;
    rig.chain.setInputGainDb (24.0f);
    rig.run (0.5f);
    REQUIRE_FALSE (rig.chain.readInputClipped());

    rig.chain.setInputGainDb (-24.0f);
    rig.run (1.0f);
    REQUIRE (rig.chain.readInputClipped());
    REQUIRE_FALSE (rig.chain.readInputClipped());
}

TEST_CASE ("gain setters clamp to their documented ranges")
{
    SignalChain chain;
    chain.setInputGainDb (100.0f);
    REQUIRE_THAT (chain.getInputGainDb(), WithinAbs (SignalChain::maxInputGainDb, 0.0));
    chain.setOutputGainDb (-200.0f);
    REQUIRE_THAT (chain.getOutputGainDb(), WithinAbs (SignalChain::minOutputGainDb, 0.0));
}
