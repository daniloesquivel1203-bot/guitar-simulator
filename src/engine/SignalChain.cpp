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

void SignalChain::prepare (double sampleRate, int)
{
    inputGain.reset (sampleRate, gainSmoothingSeconds);
    outputGain.reset (sampleRate, gainSmoothingSeconds);
    reset();
}

void SignalChain::reset()
{
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

    float blockInputPeak = 0.0f;
    float blockOutputPeak = 0.0f;
    bool clipped = false;

    for (int i = 0; i < numSamples; ++i)
    {
        const float raw = input != nullptr ? input[i] : 0.0f;
        clipped = clipped || std::abs (raw) >= clipThreshold;

        const float trimmed = raw * inputGain.getNextValue();
        blockInputPeak = juce::jmax (blockInputPeak, std::abs (trimmed));

        // Pedals, amp and cabinet go here in later milestones.

        // The hard clamp protects headphones until the limiter arrives in milestone 3.
        const float out = juce::jlimit (-1.0f, 1.0f, trimmed * outputGain.getNextValue());
        blockOutputPeak = juce::jmax (blockOutputPeak, std::abs (out));

        left[i] = out;
        right[i] = out;
    }

    raisePeak (inputPeak, blockInputPeak);
    raisePeak (outputPeak, blockOutputPeak);

    if (clipped)
        inputClipped.store (true);
}
