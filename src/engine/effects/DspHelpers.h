#pragma once

#include "TimeEffects.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <vector>

/** Small building blocks shared by the pedals in the library. */
namespace pedal_dsp
{
constexpr float pi = juce::MathConstants<float>::pi;
constexpr float twoPi = juce::MathConstants<float>::twoPi;

/** The coefficient of a one-pole smoother or filter with the given corner. */
inline float onePoleCoefficient (double sampleRate, double hz) noexcept
{
    return (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * hz / sampleRate));
}

/** The coefficient of an envelope follower that moves 63% of the way in `ms`. */
inline float timeCoefficient (double sampleRate, double ms) noexcept
{
    return (float) (1.0 - std::exp (-1.0 / (juce::jmax (0.001, ms) * 0.001 * sampleRate)));
}

/** A one-pole low-pass. The high-pass output is the input minus the low-pass. */
struct OnePole
{
    float state = 0.0f;

    float lowPass (float x, float coefficient) noexcept    { state += coefficient * (x - state); return state; }
    float highPass (float x, float coefficient) noexcept   { return x - lowPass (x, coefficient); }
    void reset() noexcept                                  { state = 0.0f; }
};

/** Removes the slow offset that rectifying or biasing a signal leaves behind. */
struct DcBlocker
{
    float x1 = 0.0f, y1 = 0.0f, r = 0.995f;

    void prepare (double sampleRate) noexcept   { r = (float) std::exp (-juce::MathConstants<double>::twoPi * 12.0 / sampleRate); }
    void reset() noexcept                       { x1 = y1 = 0.0f; }

    float process (float x) noexcept
    {
        y1 = x - x1 + r * y1;
        x1 = x;
        return y1;
    }
};

/** A state-variable filter whose cutoff can move every sample without
    clicking (the "trapezoidal" form). Gives low-, band- and high-pass at once. */
struct StateVariableFilter
{
    struct Outputs { float lowPass, bandPass, highPass; };

    float ic1 = 0.0f, ic2 = 0.0f;

    void reset() noexcept   { ic1 = ic2 = 0.0f; }

    /** `q` must be above zero. The band-pass output peaks at `q`; multiply it
        by 1/q for a peak of 1. */
    Outputs process (float x, float cutoffHz, float q, double sampleRate) noexcept
    {
        const auto g = std::tan (pi * juce::jlimit (10.0f, (float) (sampleRate * 0.49), cutoffHz) / (float) sampleRate);
        const auto k = 1.0f / q;
        const auto a1 = 1.0f / (1.0f + g * (g + k));
        const auto a2 = g * a1;
        const auto a3 = g * a2;

        const auto v3 = x - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;

        return { v2, v1, x - k * v1 - v2 };
    }
};

/** A first-order all-pass: it passes every frequency at full level but
    delays some more than others. Phasers chain several of them. */
struct FirstOrderAllPass
{
    float state = 0.0f;

    void reset() noexcept   { state = 0.0f; }

    /** The coefficient that puts the 90-degree point at `hz`. */
    static float coefficientFor (float hz, double sampleRate) noexcept
    {
        const auto w = std::tan (pi * juce::jlimit (10.0f, (float) (sampleRate * 0.45), hz) / (float) sampleRate);
        return (w - 1.0f) / (w + 1.0f);
    }

    float process (float x, float a) noexcept
    {
        const auto y = a * x + state;
        state = x - a * y;
        return y;
    }
};

/** A delay-line all-pass, used to smear echoes into a dense wash in reverbs. */
class DiffusionAllPass
{
public:
    void setSize (int samples)
    {
        buffer.assign ((size_t) juce::jmax (1, samples), 0.0f);
        index = 0;
    }

    void clear() noexcept   { std::fill (buffer.begin(), buffer.end(), 0.0f); index = 0; }

    float process (float x, float gain) noexcept
    {
        const auto stored = buffer[(size_t) index];
        const auto y = stored - gain * x;
        buffer[(size_t) index] = x + gain * y;

        if (++index >= (int) buffer.size())
            index = 0;

        return y;
    }

private:
    std::vector<float> buffer = std::vector<float> (1);
    int index = 0;
};

/** A low-frequency oscillator. Phase runs from 0 to 1. */
struct Lfo
{
    double phase = 0.0;

    void reset (double startPhase = 0.0) noexcept   { phase = startPhase; }

    /** Moves on by one sample and returns the new phase. */
    double advance (double hz, double sampleRate) noexcept
    {
        phase += hz / sampleRate;
        phase -= std::floor (phase);
        return phase;
    }

    static float sine (double p) noexcept       { return (float) std::sin (juce::MathConstants<double>::twoPi * p); }
    static float triangle (double p) noexcept   { p -= std::floor (p); return (float) (p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p); }

    /** A square wave with rounded edges, so it pulses without clicking. */
    static float softSquare (double p) noexcept   { return std::tanh (6.0f * sine (p)) / std::tanh (6.0f); }
};

/** Shifts pitch by reading a delay line at a different speed, with two
    read heads that take turns so the jump back is never heard. This is how
    the first digital pitch shifters worked.

    Each time a head jumps back, it searches a short range for the spot whose
    waveform best lines up with what the other head is playing, so the
    cross-fade joins two matching cycles instead of warbling. */
class GrainPitchShifter
{
public:
    void prepare (double sampleRate, float windowMs)
    {
        window = juce::jmax (64.0f, (float) (windowMs * 0.001 * sampleRate));
        maxLag = (int) juce::jmin (window * 0.45f, (float) (0.013 * sampleRate));
        matchLength = juce::jmax (16, (int) (0.006 * sampleRate));
        line.setMaxSamples ((int) window + maxLag + matchLength + 16);
        reset();
    }

    void reset() noexcept
    {
        line.clear();
        phase = 0.0f;
        offsets[0] = offsets[1] = 0.0f;
    }

    /** `ratio` 2 is an octave up, 0.5 an octave down. */
    float process (float x, float ratio) noexcept
    {
        line.write (x);

        // The delay grows by (1 - ratio) samples every sample.
        const auto previous = phase;
        phase += (1.0f - ratio) / window;
        phase -= std::floor (phase);

        const auto phaseB = wrapPhase (phase + 0.5f);

        if (std::abs (phase - previous) > 0.5f)
            offsets[0] = findBestOffset (phase, ratio, delayFor (phaseB, offsets[1]));

        if (std::abs (phaseB - wrapPhase (previous + 0.5f)) > 0.5f)
            offsets[1] = findBestOffset (phaseB, ratio, delayFor (phase, offsets[0]));

        // Raised-cosine windows that always add up to one.
        const auto fadeA = std::sin (pi * phase);
        const auto gainA = fadeA * fadeA;

        return gainA * line.read (delayFor (phase, offsets[0])) + (1.0f - gainA) * line.read (delayFor (phaseB, offsets[1]));
    }

private:
    static float wrapPhase (float p) noexcept   { return p - std::floor (p); }

    float delayFor (float headPhase, float offset) const noexcept   { return 1.0f + headPhase * window + offset; }

    /** Picks where a head that has just jumped should start, so that what it
        reads matches what the other head is reading. */
    float findBestOffset (float headPhase, float ratio, float otherDelay) const noexcept
    {
        // A head going up in pitch restarts at the longest delay and may only
        // move earlier; going down it restarts at the shortest and moves later.
        const auto base = (int) delayFor (headPhase, 0.0f);
        const auto other = (int) otherDelay;
        const int from = ratio > 1.0f ? -maxLag : 0;
        const int to = ratio > 1.0f ? 0 : maxLag;

        const auto score = [this, base, other] (int lag, int stride)
        {
            float dot = 0.0f, energy = 1.0e-9f;

            for (int j = 0; j < matchLength; j += stride)
            {
                const auto a = line.readWhole (other + j);
                const auto b = line.readWhole (base + lag + j);
                dot += a * b;
                energy += b * b;
            }

            return dot / std::sqrt (energy);
        };

        // A coarse search first, then a fine one around the best match.
        int best = 0;
        float bestScore = -1.0e30f;

        for (int lag = from; lag <= to; lag += 4)
        {
            if (base + lag < 1)
                continue;

            if (const auto s = score (lag, 4); s > bestScore)
            {
                bestScore = s;
                best = lag;
            }
        }

        const auto coarse = best;
        bestScore = -1.0e30f;

        for (int lag = juce::jmax (from, coarse - 3); lag <= juce::jmin (to, coarse + 3); ++lag)
        {
            if (base + lag < 1)
                continue;

            if (const auto s = score (lag, 2); s > bestScore)
            {
                bestScore = s;
                best = lag;
            }
        }

        return (float) best;
    }

    FractionalDelay line;
    float window = 2048.0f;
    float phase = 0.0f;
    int maxLag = 0, matchLength = 16;
    float offsets[2] {};
};

/** Semitones to a playback-speed ratio. */
inline float semitonesToRatio (float semitones) noexcept
{
    return std::exp2 (semitones / 12.0f);
}

/** A cheap stand-in for tanh, accurate to about 2%, for hot loops that
    run at the oversampled rate. */
inline float fastTanh (float x) noexcept
{
    x = juce::jlimit (-3.0f, 3.0f, x);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}
}
