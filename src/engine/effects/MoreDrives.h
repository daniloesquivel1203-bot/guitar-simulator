#pragma once

#include "DspHelpers.h"
#include "Drives.h"

/** A bright, cutting boost that lifts the treble into the amp, as the early
    single-transistor boosters did. Range picks where the boost begins. */
class TrebleBooster final : public DrivePedal
{
public:
    TrebleBooster() : DrivePedal ("Treble Booster", "treble.enabled") {}

    Parameter boost { "treble.boost", "Boost", 0.0f, 10.0f, 6.0f };
    Parameter range { "treble.range", "Range", 0.0f, 10.0f, 5.0f };
    Parameter level { "treble.level", "Level", 0.0f, 10.0f, 5.0f };

    std::vector<Parameter*> getControls() override   { return { &boost, &range, &level }; }

private:
    void prepareShaper (double oversampledRate) override   { osRate = oversampledRate; dc.prepare (osRate); }
    void resetShaper() override                            { highPass.reset(); dc.reset(); }
    void shape (float* samples, int numSamples) noexcept override;
    float getToneHz() const noexcept override              { return 8000.0f; }
    float getLevelKnob() const noexcept override           { return level.get(); }

    double osRate = 192000.0;
    pedal_dsp::OnePole highPass;
    pedal_dsp::DcBlocker dc;
};

/** A low-gain, open-sounding drive that keeps the guitar's own character,
    for edge-of-breakup tones and pushing an amp that is already clipping. */
class BluesDrive final : public DrivePedal
{
public:
    BluesDrive() : DrivePedal ("Blues Drive", "blues.enabled") {}

    Parameter gain  { "blues.gain",  "Gain",  0.0f, 10.0f, 4.0f };
    Parameter tone  { "blues.tone",  "Tone",  0.0f, 10.0f, 6.0f };
    Parameter level { "blues.level", "Level", 0.0f, 10.0f, 5.0f };

    std::vector<Parameter*> getControls() override   { return { &gain, &tone, &level }; }

private:
    void prepareShaper (double oversampledRate) override;
    void resetShaper() override   { highPass.reset(); dc.reset(); }
    void shape (float* samples, int numSamples) noexcept override;
    float getToneHz() const noexcept override;
    float getLevelKnob() const noexcept override   { return level.get(); }

    float highPassCoefficient = 0.0f;
    pedal_dsp::OnePole highPass;
    pedal_dsp::DcBlocker dc;
};

/** A thick, woolly two-transistor fuzz. Turning Bias down starves the
    circuit, so notes break up and sputter as they fade. */
class VintageFuzz final : public DrivePedal
{
public:
    VintageFuzz() : DrivePedal ("Vintage Fuzz", "fuzz.enabled") {}

    Parameter fuzz  { "fuzz.fuzz",  "Fuzz",  0.0f, 10.0f, 7.0f };
    Parameter bias  { "fuzz.bias",  "Bias",  0.0f, 10.0f, 8.0f };
    Parameter tone  { "fuzz.tone",  "Tone",  0.0f, 10.0f, 6.0f };
    Parameter level { "fuzz.level", "Level", 0.0f, 10.0f, 5.0f };

    std::vector<Parameter*> getControls() override   { return { &fuzz, &bias, &tone, &level }; }

private:
    void prepareShaper (double oversampledRate) override;
    void resetShaper() override   { highPass.reset(); dc.reset(); }
    void shape (float* samples, int numSamples) noexcept override;
    float getToneHz() const noexcept override;
    float getLevelKnob() const noexcept override   { return level.get(); }

    float highPassCoefficient = 0.0f;
    pedal_dsp::OnePole highPass;
    pedal_dsp::DcBlocker dc;
};

/** A huge, singing fuzz with long sustain. Its tone control sweeps from a
    dark, bassy sound through a scooped middle to a bright, cutting one. */
class SustainFuzz final : public DrivePedal
{
public:
    SustainFuzz() : DrivePedal ("Sustain Fuzz", "bigfuzz.enabled") {}

    Parameter sustain { "bigfuzz.sustain", "Sustain", 0.0f, 10.0f, 6.0f };
    Parameter tone    { "bigfuzz.tone",    "Tone",    0.0f, 10.0f, 5.0f };
    Parameter level   { "bigfuzz.level",   "Level",   0.0f, 10.0f, 5.0f };

    std::vector<Parameter*> getControls() override   { return { &sustain, &tone, &level }; }

private:
    void prepareShaper (double oversampledRate) override;
    void resetShaper() override;
    void shape (float* samples, int numSamples) noexcept override;
    void postShape (float* samples, int numSamples) noexcept override;
    float getToneHz() const noexcept override      { return 0.0f; }
    float getLevelKnob() const noexcept override   { return level.get(); }

    float inputHighPass = 0.0f, interstageLowPass = 0.0f, toneLow = 0.0f, toneHigh = 0.0f;
    pedal_dsp::OnePole highPass, interstage, lowBand, highBand;
    juce::SmoothedValue<float> toneBlend { 0.5f };
};

/** A fuzz that folds the signal over itself, adding a ringing note an
    octave above what you play. Best on the neck pickup above the 12th fret. */
class OctaveFuzz final : public DrivePedal
{
public:
    OctaveFuzz() : DrivePedal ("Octave Fuzz", "octfuzz.enabled") {}

    Parameter fuzz   { "octfuzz.fuzz",   "Fuzz",   0.0f, 10.0f, 6.0f };
    Parameter octave { "octfuzz.octave", "Octave", 0.0f, 10.0f, 7.0f };
    Parameter tone   { "octfuzz.tone",   "Tone",   0.0f, 10.0f, 6.0f };
    Parameter level  { "octfuzz.level",  "Level",  0.0f, 10.0f, 5.0f };

    std::vector<Parameter*> getControls() override   { return { &fuzz, &octave, &tone, &level }; }

private:
    void prepareShaper (double oversampledRate) override;
    void resetShaper() override   { highPass.reset(); dc.reset(); }
    void shape (float* samples, int numSamples) noexcept override;
    float getToneHz() const noexcept override;
    float getLevelKnob() const noexcept override   { return level.get(); }

    float highPassCoefficient = 0.0f;
    pedal_dsp::OnePole highPass;
    pedal_dsp::DcBlocker dc;
};

/** A very high-gain distortion with a tight low end and its own three-band
    EQ, for modern rock and metal rhythm tones. */
class MetalDistortion final : public DrivePedal
{
public:
    MetalDistortion() : DrivePedal ("Metal Distortion", "metal.enabled") {}

    Parameter gain  { "metal.gain",  "Gain",  0.0f,   10.0f, 6.0f };
    Parameter low   { "metal.low",   "Low",   -12.0f, 12.0f, 2.0f,  0.1f, " dB" };
    Parameter mid   { "metal.mid",   "Mid",   -12.0f, 12.0f, -4.0f, 0.1f, " dB" };
    Parameter high  { "metal.high",  "High",  -12.0f, 12.0f, 2.0f,  0.1f, " dB" };
    Parameter level { "metal.level", "Level", 0.0f,   10.0f, 5.0f };

    std::vector<Parameter*> getControls() override   { return { &gain, &low, &mid, &high, &level }; }

private:
    void prepareShaper (double oversampledRate) override;
    void resetShaper() override;
    void shape (float* samples, int numSamples) noexcept override;
    void postShape (float* samples, int numSamples) noexcept override;
    float getToneHz() const noexcept override      { return 0.0f; }
    float getLevelKnob() const noexcept override   { return level.get(); }

    void updateEq() noexcept;

    double baseRate = 48000.0;
    float tightCoefficient = 0.0f, interstageCoefficient = 0.0f;
    pedal_dsp::OnePole tight, interstage;
    pedal_dsp::DcBlocker dc;
    Filter lowShelf, midPeak, highShelf, fizzCut;
    float lastLow = -100.0f, lastMid = -100.0f, lastHigh = -100.0f;
};

/** Lo-fi digital grit: fewer bits make it buzzy and broken, a lower sample
    rate makes it metallic and aliased. */
class BitCrusher final : public MonoEffect
{
public:
    BitCrusher() : MonoEffect ("Bit Crusher", "crusher.enabled") {}

    Parameter bits { "crusher.bits", "Bits",        2.0f,   16.0f,    6.0f,    0.1f };
    Parameter rate { "crusher.rate", "Sample rate", 500.0f, 48000.0f, 8000.0f, 1.0f, " Hz", true };
    Parameter mix  { "crusher.mix",  "Mix",         0.0f,   100.0f,   100.0f,  1.0f, " %" };

    std::vector<Parameter*> getControls() override   { return { &bits, &rate, &mix }; }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (float* samples, int numSamples) noexcept override;

private:
    double sampleRate = 48000.0;
    double holdPhase = 1.0;
    float held = 0.0f;
    juce::SmoothedValue<float> wet { 1.0f };
};
