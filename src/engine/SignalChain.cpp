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

void SignalChain::prepare (double sampleRate, int maxBlockSize)
{
    scratch.assign ((size_t) juce::jmax (16, maxBlockSize), 0.0f);

    inputGain.reset (sampleRate, gainSmoothingSeconds);
    outputGain.reset (sampleRate, gainSmoothingSeconds);

    amp.prepare (sampleRate, (int) scratch.size());
    cabinet.prepare (sampleRate, (int) scratch.size());
    limiter.prepare (sampleRate);
    reset();
}

void SignalChain::reset()
{
    amp.reset();
    cabinet.reset();
    limiter.reset();

    inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (inputGainDb.load()));
    outputGain.setCurrentAndTargetValue (muted.load() ? 0.0f
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
    outputGain.setTargetValue (muted.load() ? 0.0f
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

    amp.process (mono, numSamples);
    cabinet.process (mono, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        // The limiter is last so nothing upstream can push the output past its ceiling.
        const float out = limiter.processSample (mono[i] * outputGain.getNextValue());
        blockOutputPeak = juce::jmax (blockOutputPeak, std::abs (out));

        left[i] = out;
        right[i] = out;
    }

    raisePeak (inputPeak, blockInputPeak);
    raisePeak (outputPeak, blockOutputPeak);

    if (clipped)
        inputClipped.store (true);
}
