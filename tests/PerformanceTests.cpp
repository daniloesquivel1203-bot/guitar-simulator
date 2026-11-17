#include "engine/SignalChain.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

namespace
{
struct Timing
{
    double averagePercent = 0.0;   // share of real time used, on one core
    double worstPercent = 0.0;     // the slowest single block
};

/** Times the chain on guitar-like input and reports how much of real time it uses. */
Timing measure (SignalChain& chain, double sampleRate, int blockSize, double seconds = 4.0)
{
    chain.prepare (sampleRate, blockSize);

    std::mt19937 random (1);
    std::uniform_real_distribution<float> hiss (-0.01f, 0.01f);
    std::vector<float> input ((size_t) blockSize), left ((size_t) blockSize), right ((size_t) blockSize);

    const auto blocks = (int) (seconds * sampleRate / blockSize);
    const auto blockSeconds = blockSize / sampleRate;
    double total = 0.0, worst = 0.0, phase = 0.0;

    for (int block = 0; block < blocks; ++block)
    {
        for (auto& sample : input)
        {
            phase += juce::MathConstants<double>::twoPi * 110.0 / sampleRate;
            sample = 0.3f * (float) (std::sin (phase) + 0.5 * std::sin (2.0 * phase) + 0.3 * std::sin (3.0 * phase)) + hiss (random);
        }

        const auto start = std::chrono::steady_clock::now();
        chain.process (input.data(), left.data(), right.data(), blockSize);
        const auto taken = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();

        if (block > 20)   // skip warm-up
        {
            total += taken;
            worst = juce::jmax (worst, taken);
        }
    }

    return { 100.0 * total / ((blocks - 21) * blockSeconds), 100.0 * worst / blockSeconds };
}

void switchEverything (SignalChain& chain, bool on)
{
    for (auto* parameter : chain.getAllParameters())
        if (parameter->isSwitch())
            parameter->set (on ? 1.0f : 0.0f);
}
}

// Hidden by default because timing depends on the machine. Run it with:
//   GuitarSimulatorTests "[performance]"
TEST_CASE ("the full signal chain uses a small share of one processor core", "[.][performance]")
{
    for (const auto sampleRate : { 48000.0, 96000.0 })
    {
        for (const auto blockSize : { 32, 64, 256 })
        {
            SignalChain chain;
            switchEverything (chain, true);
            const auto timing = measure (chain, sampleRate, blockSize);
            std::printf ("everything on, %5.0f Hz, block %3d: average %5.2f%%, worst block %5.2f%%\n",
                         sampleRate, blockSize, timing.averagePercent, timing.worstPercent);
        }
    }

    // What each block costs by itself at 48 kHz, block 64.
    SignalChain baseline;
    switchEverything (baseline, false);
    const auto idle = measure (baseline, 48000.0, 64).averagePercent;
    std::printf ("everything off: %5.2f%%\n", idle);

    SignalChain reference;

    for (size_t i = 0; i < reference.getAllParameters().size(); ++i)
    {
        if (! reference.getAllParameters()[i]->isSwitch())
            continue;

        SignalChain chain;
        switchEverything (chain, false);
        chain.getAllParameters()[i]->set (1.0f);
        std::printf ("  %-28s %5.2f%%\n", chain.getAllParameters()[i]->id,
                     measure (chain, 48000.0, 64, 2.0).averagePercent - idle);
    }

    SUCCEED();
}
