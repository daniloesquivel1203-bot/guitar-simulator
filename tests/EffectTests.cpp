#include "TestSignals.h"
#include "engine/SafetyLimiter.h"
#include "engine/effects/Drives.h"
#include "engine/effects/Dynamics.h"
#include "engine/effects/Equaliser.h"
#include "engine/effects/TimeEffects.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <memory>

using Catch::Matchers::WithinAbs;

namespace
{
constexpr int blockSize = 128;
constexpr double sampleRate = 48000.0;

struct Stereo
{
    std::vector<float> left, right;
};

Stereo renderStereo (StereoEffect& effect, std::vector<float> left, std::vector<float> right)
{
    for (size_t offset = 0; offset < left.size(); offset += (size_t) blockSize)
        effect.process (left.data() + offset, right.data() + offset,
                        (int) juce::jmin ((size_t) blockSize, left.size() - offset));

    return { std::move (left), std::move (right) };
}

std::vector<float> impulse (int numSamples, float value = 1.0f)
{
    std::vector<float> out ((size_t) numSamples, 0.0f);
    out[0] = value;
    return out;
}

template <typename EffectType>
std::unique_ptr<EffectType> makeOn()
{
    auto effect = std::make_unique<EffectType>();
    effect->enabled.set (1.0f);
    effect->prepare (sampleRate, blockSize);
    return effect;
}

double levelDb (const std::vector<float>& signal)
{
    return juce::Decibels::gainToDecibels (test::rms (signal, signal.size() / 2));
}

std::vector<std::unique_ptr<MonoEffect>> allMonoEffects()
{
    std::vector<std::unique_ptr<MonoEffect>> effects;
    effects.push_back (std::make_unique<NoiseGate>());
    effects.push_back (std::make_unique<Compressor>());
    effects.push_back (std::make_unique<Overdrive>());
    effects.push_back (std::make_unique<Distortion>());
    effects.push_back (std::make_unique<Equaliser>());
    return effects;
}

std::vector<std::unique_ptr<StereoEffect>> allStereoEffects()
{
    std::vector<std::unique_ptr<StereoEffect>> effects;
    effects.push_back (std::make_unique<Chorus>());
    effects.push_back (std::make_unique<Delay>());
    effects.push_back (std::make_unique<Reverb>());
    return effects;
}
}

//==============================================================================
TEST_CASE ("every pedal is off by default and then passes the signal through untouched")
{
    const auto in = test::sine (220.0, 0.3f, sampleRate, 4800);

    for (auto& effect : allMonoEffects())
    {
        INFO (effect->name);
        REQUIRE_FALSE (effect->enabled.isOn());
        effect->prepare (sampleRate, blockSize);
        REQUIRE (test::render (*effect, in, blockSize) == in);
    }

    for (auto& effect : allStereoEffects())
    {
        INFO (effect->name);
        REQUIRE_FALSE (effect->enabled.isOn());
        effect->prepare (sampleRate, blockSize);
        const auto out = renderStereo (*effect, in, in);
        REQUIRE (out.left == in);
        REQUIRE (out.right == in);
    }
}

TEST_CASE ("every pedal turns silence into silence")
{
    const std::vector<float> silence (48000, 0.0f);

    for (auto& effect : allMonoEffects())
    {
        INFO (effect->name);
        effect->enabled.set (1.0f);
        effect->prepare (sampleRate, blockSize);
        REQUIRE (test::peak (test::render (*effect, silence, blockSize)) < 1.0e-6f);
    }

    for (auto& effect : allStereoEffects())
    {
        INFO (effect->name);
        effect->enabled.set (1.0f);
        effect->prepare (sampleRate, blockSize);
        const auto out = renderStereo (*effect, silence, silence);
        REQUIRE (test::peak (out.left) < 1.0e-6f);
        REQUIRE (test::peak (out.right) < 1.0e-6f);
    }
}

TEST_CASE ("every pedal stays finite and bounded with all controls at maximum")
{
    const auto in = test::sine (220.0, 1.0f, sampleRate, 96000);

    for (auto& effect : allMonoEffects())
    {
        INFO (effect->name);
        effect->enabled.set (1.0f);

        for (auto* control : effect->getControls())
            control->set (control->maxValue);

        effect->prepare (sampleRate, blockSize);
        const auto out = test::render (*effect, in, blockSize);
        REQUIRE (test::allFinite (out));
        REQUIRE (test::peak (out) < 100.0f);
    }

    for (auto& effect : allStereoEffects())
    {
        INFO (effect->name);
        effect->enabled.set (1.0f);

        for (auto* control : effect->getControls())
            control->set (control->maxValue);

        effect->prepare (sampleRate, blockSize);
        const auto out = renderStereo (*effect, in, in);
        REQUIRE (test::allFinite (out.left));
        REQUIRE (test::allFinite (out.right));
        REQUIRE (test::peak (out.left) < 100.0f);
        REQUIRE (test::peak (out.right) < 100.0f);
    }
}

TEST_CASE ("every parameter id is unique")
{
    std::vector<juce::String> ids;

    const auto collect = [&ids] (Effect& effect)
    {
        ids.push_back (effect.enabled.id);

        for (auto* control : effect.getControls())
            ids.push_back (control->id);
    };

    for (auto& effect : allMonoEffects())    collect (*effect);
    for (auto& effect : allStereoEffects())  collect (*effect);

    for (size_t a = 0; a < ids.size(); ++a)
        for (size_t b = a + 1; b < ids.size(); ++b)
            REQUIRE (ids[a] != ids[b]);
}

//==============================================================================
TEST_CASE ("the noise gate removes quiet hum and passes playing")
{
    auto gate = makeOn<NoiseGate>();
    gate->threshold.set (-50.0f);

    const auto hum = test::render (*gate, test::sine (60.0, 0.0005f, sampleRate, 48000), blockSize);   // -66 dB
    REQUIRE (test::peak (hum, hum.size() / 2) < 1.0e-5f);

    gate->reset();
    const auto note = test::sine (220.0, 0.2f, sampleRate, 48000);
    const auto played = test::render (*gate, note, blockSize);
    REQUIRE_THAT (test::rms (played, played.size() / 2), WithinAbs (test::rms (note, note.size() / 2), 1.0e-4));
}

TEST_CASE ("the noise gate opens fast enough to keep the start of a note")
{
    auto gate = makeOn<NoiseGate>();
    const auto out = test::render (*gate, test::sine (220.0, 0.3f, sampleRate, 4800), blockSize);

    // Within 5 ms the gate is letting nearly everything through.
    REQUIRE (test::peak (out, 240) > 0.28f);
}

TEST_CASE ("the noise gate closes after the note ends, over about the release time")
{
    auto gate = makeOn<NoiseGate>();
    gate->release.set (100.0f);

    auto signal = test::sine (220.0, 0.3f, sampleRate, 24000);
    const auto tail = test::sine (220.0, 0.0002f, sampleRate, 48000);
    signal.insert (signal.end(), tail.begin(), tail.end());

    const auto out = test::render (*gate, signal, blockSize);

    REQUIRE (test::peak (out, out.size() - 4800) < 1.0e-6f);
}

//==============================================================================
TEST_CASE ("the compressor leaves quiet signals alone and turns loud ones down by the ratio")
{
    auto compressor = makeOn<Compressor>();
    compressor->threshold.set (-20.0f);
    compressor->ratio.set (4.0f);

    const auto quietIn = test::sine (220.0, 0.01f, sampleRate, 48000);   // -40 dB
    const auto quietOut = test::render (*compressor, quietIn, blockSize);
    REQUIRE_THAT (levelDb (quietOut), WithinAbs (levelDb (quietIn), 0.2));

    compressor->reset();
    const auto loudIn = test::sine (220.0, 0.5f, sampleRate, 48000);     // peaks at -6 dB, 14 dB over
    const auto loudOut = test::render (*compressor, loudIn, blockSize);
    const auto reduction = levelDb (loudIn) - levelDb (loudOut);

    // 14 dB over at 4:1 asks for 10.5 dB. The detector follows a moving wave, so allow some slack.
    REQUIRE (reduction > 7.0);
    REQUIRE (reduction < 11.0);
}

TEST_CASE ("a ratio of one does nothing and makeup gain raises the level")
{
    auto compressor = makeOn<Compressor>();
    compressor->ratio.set (1.0f);

    const auto in = test::sine (220.0, 0.5f, sampleRate, 48000);
    REQUIRE_THAT (levelDb (test::render (*compressor, in, blockSize)), WithinAbs (levelDb (in), 0.01));

    compressor->makeup.set (6.0f);
    compressor->reset();
    REQUIRE_THAT (levelDb (test::render (*compressor, in, blockSize)), WithinAbs (levelDb (in) + 6.0, 0.05));
}

//==============================================================================
TEST_CASE ("the drive pedals add more distortion as they are turned up")
{
    const auto measure = [] (MonoEffect& pedal, Parameter& amount, float setting)
    {
        amount.set (setting);
        pedal.prepare (sampleRate, blockSize);
        const auto out = test::render (pedal, test::sine (200.0, 0.2f, sampleRate, 48000), blockSize);
        return test::distortion (out, 200.0, sampleRate, out.size() / 2);
    };

    Overdrive overdrive;
    overdrive.enabled.set (1.0f);
    REQUIRE (measure (overdrive, overdrive.drive, 10.0f) > measure (overdrive, overdrive.drive, 0.0f));

    Distortion distortion;
    distortion.enabled.set (1.0f);
    const auto low = measure (distortion, distortion.gain, 0.0f);
    const auto high = measure (distortion, distortion.gain, 10.0f);
    REQUIRE (high > low);
    REQUIRE (high > 0.25);

    // The distortion pedal is the harsher of the two at full tilt.
    REQUIRE (high > measure (overdrive, overdrive.drive, 10.0f));
}

TEST_CASE ("the drive pedal tone knob brightens and the level knob sets volume")
{
    const auto trebleLevel = [] (float tone)
    {
        auto pedal = makeOn<Distortion>();
        pedal->tone.set (tone);
        const auto out = test::render (*pedal, test::sine (200.0, 0.2f, sampleRate, 48000), blockSize);
        return test::amplitudeAt (out, 5000.0, sampleRate, out.size() / 2);   // the 25th harmonic
    };

    REQUIRE (trebleLevel (10.0f) > 2.0 * trebleLevel (0.0f));

    const auto loudness = [] (float level)
    {
        auto pedal = makeOn<Overdrive>();
        pedal->level.set (level);
        return levelDb (test::render (*pedal, test::sine (200.0, 0.2f, sampleRate, 48000), blockSize));
    };

    REQUIRE_THAT (loudness (7.0f) - loudness (5.0f), WithinAbs (6.0, 0.1));
    REQUIRE (loudness (0.0f) <= -100.0);
}

TEST_CASE ("the drive pedals keep their output level sensible at default settings")
{
    for (auto* pedal : std::initializer_list<MonoEffect*> { new Overdrive(), new Distortion() })
    {
        const std::unique_ptr<MonoEffect> owner (pedal);
        INFO (pedal->name);
        pedal->enabled.set (1.0f);
        pedal->prepare (sampleRate, blockSize);

        const auto in = test::sine (200.0, 0.2f, sampleRate, 48000);
        const auto change = levelDb (test::render (*pedal, in, blockSize)) - levelDb (in);
        REQUIRE (change > -6.0);
        REQUIRE (change < 12.0);
    }
}

//==============================================================================
TEST_CASE ("the EQ bands boost and cut their own ranges and are flat at zero")
{
    const auto responseDb = [] (double hz, Parameter Equaliser::* band, float setting)
    {
        auto eq = makeOn<Equaliser>();
        ((*eq).*band).set (setting);
        const auto in = test::sine (hz, 0.1f, sampleRate, 48000);
        return levelDb (test::render (*eq, in, blockSize)) - levelDb (in);
    };

    REQUIRE_THAT (responseDb (1000.0, &Equaliser::low, 0.0f), WithinAbs (0.0, 0.01));
    REQUIRE (responseDb (50.0, &Equaliser::low, 12.0f) > 9.0);
    REQUIRE (responseDb (50.0, &Equaliser::low, -12.0f) < -9.0);
    REQUIRE (responseDb (800.0, &Equaliser::mid, 12.0f) > 11.0);
    REQUIRE (responseDb (10000.0, &Equaliser::high, 12.0f) > 9.0);
    REQUIRE (std::abs (responseDb (10000.0, &Equaliser::low, 12.0f)) < 0.5);
}

TEST_CASE ("the EQ mid frequency control moves the mid band")
{
    auto eq = makeOn<Equaliser>();
    eq->mid.set (12.0f);
    eq->midFreq.set (3000.0f);

    const auto at = [&] (double hz)
    {
        eq->reset();
        const auto in = test::sine (hz, 0.1f, sampleRate, 48000);
        return levelDb (test::render (*eq, in, blockSize)) - levelDb (in);
    };

    REQUIRE (at (3000.0) > 11.0);
    REQUIRE (at (800.0) < 3.0);
}

//==============================================================================
TEST_CASE ("the chorus changes the sound and makes left and right differ")
{
    auto chorus = makeOn<Chorus>();
    const auto in = test::sine (440.0, 0.3f, sampleRate, 48000);
    const auto out = renderStereo (*chorus, in, in);

    REQUIRE (out.left != in);
    REQUIRE (out.left != out.right);
}

TEST_CASE ("the chorus at zero mix leaves the signal unchanged")
{
    auto chorus = makeOn<Chorus>();
    chorus->mix.set (0.0f);
    chorus->reset();

    const auto in = test::sine (440.0, 0.3f, sampleRate, 4800);
    const auto out = renderStereo (*chorus, in, in);
    REQUIRE (out.left == in);
    REQUIRE (out.right == in);
}

//==============================================================================
TEST_CASE ("the delay repeats a sound after the set time, quieter each time")
{
    auto delay = makeOn<Delay>();
    delay->time.set (100.0f);       // 4800 samples
    delay->feedback.set (50.0f);
    delay->mix.set (100.0f);
    delay->reset();

    const auto in = impulse (24000);
    const auto out = renderStereo (*delay, in, in);

    const auto peakNear = [&] (const std::vector<float>& signal, size_t centre)
    {
        float result = 0.0f;

        for (size_t i = centre - 8; i <= centre + 8; ++i)
            result = juce::jmax (result, std::abs (signal[i]));

        return result;
    };

    REQUIRE_THAT (out.left[0], WithinAbs (1.0, 1.0e-6));          // the dry click is untouched
    REQUIRE (test::peak ({ out.left.begin() + 16, out.left.begin() + 4700 }) < 1.0e-4f);

    const auto first = peakNear (out.left, 4800);
    const auto second = peakNear (out.left, 9600);
    const auto third = peakNear (out.left, 14400);

    REQUIRE (first > 0.3f);
    REQUIRE (second < first);
    REQUIRE (second > 0.05f);
    REQUIRE (third < second);

    // Without ping-pong both sides carry the same echoes.
    REQUIRE (out.left == out.right);
}

TEST_CASE ("ping-pong delay alternates echoes between left and right")
{
    auto delay = makeOn<Delay>();
    delay->time.set (100.0f);
    delay->feedback.set (60.0f);
    delay->mix.set (100.0f);
    delay->pingPong.set (1.0f);
    delay->reset();

    const auto in = impulse (24000);
    const auto out = renderStereo (*delay, in, in);

    const auto energyNear = [] (const std::vector<float>& signal, size_t centre)
    {
        double sum = 0.0;

        for (size_t i = centre - 200; i <= centre + 200; ++i)
            sum += (double) signal[i] * signal[i];

        return sum;
    };

    // First echo on the left, second on the right, third on the left again.
    REQUIRE (energyNear (out.left, 4800) > 100.0 * energyNear (out.right, 4800));
    REQUIRE (energyNear (out.right, 9600) > 100.0 * energyNear (out.left, 9600));
    REQUIRE (energyNear (out.left, 14400) > 100.0 * energyNear (out.right, 14400));
}

TEST_CASE ("the delay at zero mix leaves the signal unchanged")
{
    auto delay = makeOn<Delay>();
    delay->mix.set (0.0f);
    delay->reset();

    const auto in = test::sine (440.0, 0.3f, sampleRate, 48000);
    const auto out = renderStereo (*delay, in, in);
    REQUIRE (out.left == in);
    REQUIRE (out.right == in);
}

//==============================================================================
TEST_CASE ("the reverb leaves a tail that dies away, and a bigger size lasts longer")
{
    const auto tailLevel = [] (float size, size_t from)
    {
        auto reverb = makeOn<Reverb>();
        reverb->size.set (size);
        reverb->mix.set (100.0f);
        reverb->prepare (sampleRate, blockSize);

        auto in = test::sine (440.0, 0.5f, sampleRate, 4800);
        in.resize (48000 * 12, 0.0f);
        const auto out = renderStereo (*reverb, in, in);

        std::vector<float> window (out.left.begin() + (long) from, out.left.begin() + (long) from + 4800);
        return test::rms (window);
    };

    REQUIRE (tailLevel (5.0f, 9600) > 1.0e-4);                              // still ringing shortly after
    REQUIRE (tailLevel (10.0f, 48000) > 4.0 * tailLevel (2.0f, 48000));     // bigger rooms ring longer
    REQUIRE (tailLevel (10.0f, 48000 * 11) < 1.0e-3);                       // even the biggest dies away
}

TEST_CASE ("the reverb at zero mix keeps the dry level")
{
    auto reverb = makeOn<Reverb>();
    reverb->mix.set (0.0f);
    reverb->prepare (sampleRate, blockSize);

    const auto in = test::sine (440.0, 0.3f, sampleRate, 48000);
    const auto out = renderStereo (*reverb, in, in);
    REQUIRE_THAT (levelDb (out.left), WithinAbs (levelDb (in), 0.05));
}

//==============================================================================
TEST_CASE ("the stereo limiter turns both sides down together")
{
    SafetyLimiter limiter;
    limiter.prepare (sampleRate);

    float left = 2.0f, right = 0.5f;
    limiter.processStereo (left, right);

    REQUIRE_THAT (left, WithinAbs (SafetyLimiter::ceiling, 1.0e-6));
    REQUIRE_THAT (right / left, WithinAbs (0.25, 1.0e-6));
}
