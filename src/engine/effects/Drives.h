#pragma once

#include "Effect.h"

#include <juce_dsp/juce_dsp.h>

/** Shared parts of the two drive pedals: oversampling and the tone and level controls. */
class DrivePedal : public MonoEffect
{
public:
    using MonoEffect::MonoEffect;

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* samples, int numSamples) noexcept final;

protected:
    using Filter = juce::dsp::IIR::Filter<float>;
    using Coefficients = juce::dsp::IIR::Coefficients<float>;

    /** Called when the sample rate is known. `oversampledRate` is 4x the device rate. */
    virtual void prepareShaper (double oversampledRate) = 0;
    virtual void resetShaper() = 0;

    /** Distorts a block that has been upsampled to 4x. */
    virtual void shape (float* samples, int numSamples) noexcept = 0;

    /** The cutoff of the low-pass tone control. Return 0 for pedals that
        shape their tone in postShape() instead. */
    virtual float getToneHz() const noexcept = 0;

    /** Runs at the device rate after the distortion, before the level control. */
    virtual void postShape (float*, int) noexcept {}
    virtual float getLevelKnob() const noexcept = 0;

private:
    juce::dsp::Oversampling<float> oversampling { 1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                                                  true, false };
    Filter toneFilter;
    juce::SmoothedValue<float> level { 1.0f };
    double rate = 48000.0;
    float lastToneHz = -1.0f;
};

/** A smooth, mid-focused overdrive in the style of the classic green pedal.

    Only the mids and highs are clipped and the clean signal is mixed back in,
    so the low end stays tight. Used in front of an amp it pushes the amp
    harder and adds a vocal midrange.
*/
class Overdrive final : public DrivePedal
{
public:
    Overdrive() : DrivePedal ("Overdrive", "od.enabled") {}

    Parameter drive { "od.drive", "Drive", 0.0f, 10.0f, 5.0f };
    Parameter tone  { "od.tone",  "Tone",  0.0f, 10.0f, 5.0f };
    Parameter level { "od.level", "Level", 0.0f, 10.0f, 5.0f };

    std::vector<Parameter*> getControls() override   { return { &drive, &tone, &level }; }

private:
    void prepareShaper (double oversampledRate) override;
    void resetShaper() override;
    void shape (float* samples, int numSamples) noexcept override;
    float getToneHz() const noexcept override;
    float getLevelKnob() const noexcept override   { return level.get(); }

    Filter highPass;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> driveGain { 1.0f };
};

/** A harder, more aggressive distortion with a full frequency range. */
class Distortion final : public DrivePedal
{
public:
    Distortion() : DrivePedal ("Distortion", "dist.enabled") {}

    Parameter gain  { "dist.gain",  "Gain",  0.0f, 10.0f, 5.0f };
    Parameter tone  { "dist.tone",  "Tone",  0.0f, 10.0f, 5.0f };
    Parameter level { "dist.level", "Level", 0.0f, 10.0f, 5.0f };

    std::vector<Parameter*> getControls() override   { return { &gain, &tone, &level }; }

private:
    void prepareShaper (double oversampledRate) override;
    void resetShaper() override;
    void shape (float* samples, int numSamples) noexcept override;
    float getToneHz() const noexcept override;
    float getLevelKnob() const noexcept override   { return level.get(); }

    Filter highPass, lowPass;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> driveGain { 1.0f };
};
