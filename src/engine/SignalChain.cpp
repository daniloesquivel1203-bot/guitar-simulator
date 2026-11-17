#include "SignalChain.h"

#include <cmath>

namespace
{
constexpr double gainSmoothingSeconds = 0.02;

void raisePeak (std::atomic<float>& peak, float candidate) noexcept
{
    auto current = peak.load();

    while (candidate > current && ! peak.compare_exchange_weak (current, candidate))
    {
    }
}
}

SignalChain::SignalChain()
{
    Pedalboard::PedalList before, after;
    Pedalboard::makeClassicLayout (before, after);
    board.setPedals (std::move (before), std::move (after));
}

void SignalChain::prepare (double sampleRate, int maxBlockSize)
{
    scratch.assign ((size_t) juce::jmax (16, maxBlockSize), 0.0f);
    scratchRight.assign (scratch.size(), 0.0f);

    inputGain.reset (sampleRate, gainSmoothingSeconds);
    outputGain.reset (sampleRate, gainSmoothingSeconds);

    const auto blockSize = (int) scratch.size();

    board.prepare (sampleRate, blockSize);
    amp.prepare (sampleRate, blockSize);
    cabinet.prepare (sampleRate, blockSize);
    limiter.prepare (sampleRate);
    reset();
}

std::vector<Parameter*> SignalChain::getAllParameters()
{
    std::vector<Parameter*> all;

    const auto addSide = [this, &all] (Pedalboard::Side side)
    {
        for (auto& pedal : board.getPedals (side))
            for (auto* parameter : pedal->getParameters())
                all.push_back (parameter);
    };

    addSide (Pedalboard::beforeAmp);
    for (auto* parameter : amp.getParameters()) all.push_back (parameter);
    for (auto* parameter : cabinet.getParameters()) all.push_back (parameter);
    addSide (Pedalboard::afterAmp);

    return all;
}

void SignalChain::reset()
{
    board.reset();
    amp.reset();
    cabinet.reset();
    limiter.reset();

    inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (inputGainDb.load()));
    outputGain.setCurrentAndTargetValue ((muted.load() || tunerMuted.load()) ? 0.0f
                                                      : juce::Decibels::decibelsToGain (outputGainDb.load()));
}

void SignalChain::setInputGainDb (float db) noexcept
{
    inputGainDb.store (juce::jlimit (minInputGainDb, maxInputGainDb, db));
}

void SignalChain::setOutputGainDb (float db) noexcept
{
    outputGainDb.store (juce::jlimit (minOutputGainDb, maxOutputGainDb, db));
}

void SignalChain::process (const float* input, float* left, float* right, int numSamples) noexcept
{
    inputGain.setTargetValue (juce::Decibels::decibelsToGain (inputGainDb.load()));
    outputGain.setTargetValue ((muted.load() || tunerMuted.load()) ? 0.0f
                                            : juce::Decibels::decibelsToGain (outputGainDb.load()));

    // A device may deliver more samples than it promised, so work in chunks
    // that always fit the buffers set up in prepare().
    const auto chunkSize = (int) scratch.size();

    for (int offset = 0; offset < numSamples; offset += chunkSize)
        processChunk (input != nullptr ? input + offset : nullptr, left + offset, right + offset,
                      juce::jmin (chunkSize, numSamples - offset));
}

void SignalChain::processChunk (const float* input, float* left, float* right, int numSamples) noexcept
{
    auto* mono = scratch.data();
    board.beginChunk();

    float blockInputPeak = 0.0f;
    float blockOutputPeak = 0.0f;
    bool clipped = false;

    for (int i = 0; i < numSamples; ++i)
    {
        const float raw = input != nullptr ? input[i] : 0.0f;
        clipped = clipped || std::abs (raw) >= clipThreshold;

        mono[i] = raw * inputGain.getNextValue();
        blockInputPeak = juce::jmax (blockInputPeak, std::abs (mono[i]));
    }

    board.processBeforeAmp (mono, numSamples);
    amp.process (mono, numSamples);
    cabinet.process (mono, numSamples);

    // The board turns the signal into stereo.
    auto* monoRight = scratchRight.data();
    board.processAfterAmp (mono, monoRight, numSamples);

    const bool monoOutput = left == right;
    const bool fading = board.isFading();

    for (int i = 0; i < numSamples; ++i)
    {
        auto gain = outputGain.getNextValue();

        // Briefly fades out and in while the pedal order changes.
        if (fading)
            gain *= board.getNextFadeGain();

        auto outLeft = mono[i] * gain;
        auto outRight = monoRight[i] * gain;

        // The limiter is last so nothing upstream can push the output past its ceiling.
        limiter.processStereo (outLeft, outRight);
        blockOutputPeak = juce::jmax (blockOutputPeak, std::abs (outLeft), std::abs (outRight));

        if (monoOutput)
        {
            left[i] = 0.5f * (outLeft + outRight);
        }
        else
        {
            left[i] = outLeft;
            right[i] = outRight;
        }
    }

    raisePeak (inputPeak, blockInputPeak);
    raisePeak (outputPeak, blockOutputPeak);

    if (clipped)
        inputClipped.store (true);
}
