#pragma once

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>

#include <cmath>
#include <vector>

/** Helpers for generating and measuring test signals. */
namespace test
{
inline std::vector<float> sine (double hz, float amplitude, double sampleRate, int numSamples)
{
    std::vector<float> out ((size_t) numSamples);

    for (int i = 0; i < numSamples; ++i)
        out[(size_t) i] = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / sampleRate);

    return out;
}

inline float peak (const std::vector<float>& signal, size_t from = 0)
{
    float result = 0.0f;

    for (size_t i = from; i < signal.size(); ++i)
        result = juce::jmax (result, std::abs (signal[i]));

    return result;
}

inline double rms (const std::vector<float>& signal, size_t from = 0)
{
    double sum = 0.0;

    for (size_t i = from; i < signal.size(); ++i)
        sum += (double) signal[i] * signal[i];

    return std::sqrt (sum / (double) (signal.size() - from));
}

inline bool allFinite (const std::vector<float>& signal)
{
    for (auto sample : signal)
        if (! std::isfinite (sample))
            return false;

    return true;
}

/** Amplitude of one frequency component, measured over signal[from..]. */
inline double amplitudeAt (const std::vector<float>& signal, double hz, double sampleRate, size_t from)
{
    double re = 0.0, im = 0.0;

    for (size_t i = from; i < signal.size(); ++i)
    {
        const auto phase = juce::MathConstants<double>::twoPi * hz * (double) i / sampleRate;
        re += signal[i] * std::cos (phase);
        im += signal[i] * std::sin (phase);
    }

    return 2.0 * std::sqrt (re * re + im * im) / (double) (signal.size() - from);
}

/** Share of the signal's energy that is not at the fundamental, from 0 to 1.
    Use a whole number of cycles in signal[from..] for an accurate result. */
inline double distortion (const std::vector<float>& signal, double fundamentalHz, double sampleRate, size_t from)
{
    const auto fundamental = amplitudeAt (signal, fundamentalHz, sampleRate, from) / std::sqrt (2.0);
    const auto total = rms (signal, from);
    return total <= 0.0 ? 0.0 : std::sqrt (juce::jmax (0.0, total * total - fundamental * fundamental)) / total;
}

/** Runs a mono in-place processor over a whole signal in fixed-size blocks. */
template <typename Processor>
std::vector<float> render (Processor& processor, std::vector<float> signal, int blockSize)
{
    for (size_t offset = 0; offset < signal.size(); offset += (size_t) blockSize)
        processor.process (signal.data() + offset, (int) juce::jmin ((size_t) blockSize, signal.size() - offset));

    return signal;
}
}

namespace test
{
/** A short guitar-like riff made with plucked-string synthesis: a strummed
    power chord left to ring, a few single notes and a long held note. The
    loudest hit peaks at `peakLevel`, about where a guitar lands on an
    interface with the input gain set sensibly. */
inline std::vector<float> guitarRiff (double sampleRate, float peakLevel = 0.3f)
{
    struct Note { double startSeconds, hz, lengthSeconds; float velocity; };

    const Note notes[] = {
        { 0.00, 82.41,  1.4, 1.0f }, { 0.012, 123.47, 1.4, 0.9f }, { 0.024, 164.81, 1.4, 0.8f },   // E5 chord
        { 1.50, 220.00, 0.4, 0.8f }, { 1.90, 261.63, 0.4, 0.6f }, { 2.30, 293.66, 0.4, 0.7f },
        { 2.70, 196.00, 2.0, 0.9f },
    };

    const auto total = (int) (5.6 * sampleRate);
    std::vector<float> out ((size_t) total, 0.0f);
    juce::Random random (42);

    for (const auto& note : notes)
    {
        // Karplus-Strong: a burst of noise circulating in a delay one period long,
        // softened a little on each trip, rings like a plucked string.
        const auto period = sampleRate / note.hz;
        const auto length = (size_t) period;
        const auto fraction = (float) (period - (double) length);
        std::vector<float> line (length + 1);

        float smoothed = 0.0f;

        for (auto& sample : line)
        {
            // A pick is brighter than a thumb: mostly raw noise, lightly smoothed.
            smoothed += 0.7f * ((2.0f * random.nextFloat() - 1.0f) - smoothed);
            sample = smoothed;
        }

        const auto start = (size_t) (note.startSeconds * sampleRate);
        const auto end = juce::jmin (out.size(), start + (size_t) (note.lengthSeconds * sampleRate));
        const auto decay = (float) std::pow (0.001, 1.0 / (6.0 * note.hz));   // about six seconds to -60 dB
        size_t index = 0;
        float previous = 0.0f;

        for (auto i = start; i < end; ++i)
        {
            const auto current = line[index];
            const auto next = line[(index + 1) % line.size()];
            const auto value = current + fraction * (next - current);
            line[index] = decay * 0.5f * (current + previous);
            previous = current;
            index = (index + 1) % line.size();

            // Damp the end of each note so it stops like a released string.
            const auto remaining = (double) (end - i) / sampleRate;
            const auto release = (float) juce::jmin (1.0, remaining / 0.03);
            out[i] += note.velocity * value * release;
        }
    }

    const auto loudest = peak (out);

    if (loudest > 0.0f)
        for (auto& sample : out)
            sample *= peakLevel / loudest;

    // A real pickup also hums and hisses a little, about 65 dB below the notes.
    for (size_t i = 0; i < out.size(); ++i)
    {
        const auto t = (double) i / sampleRate;
        const auto hum = std::sin (juce::MathConstants<double>::twoPi * 60.0 * t) + 0.5 * std::sin (juce::MathConstants<double>::twoPi * 180.0 * t);
        out[i] += 0.0003f * (float) hum + 0.0002f * (2.0f * random.nextFloat() - 1.0f);
    }

    return out;
}

/** Energy in third-octave bands from 63 Hz to 10 kHz, in dB. Uses one
    long FFT over the whole signal, so it describes the average tone. */
inline std::vector<double> bandLevelsDb (const std::vector<float>& signal, double sampleRate)
{
    constexpr int order = 16;
    constexpr int size = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<double> power (size / 2, 0.0);
    std::vector<float> frame ((size_t) size * 2);

    // Average power spectra over half-overlapping, Hann-windowed frames.
    int frames = 0;

    for (size_t start = 0; start + size <= signal.size() || frames == 0; start += size / 2)
    {
        std::fill (frame.begin(), frame.end(), 0.0f);

        for (int i = 0; i < size && start + (size_t) i < signal.size(); ++i)
            frame[(size_t) i] = signal[start + (size_t) i] * (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / size));

        fft.performFrequencyOnlyForwardTransform (frame.data());

        for (int i = 0; i < size / 2; ++i)
            power[(size_t) i] += (double) frame[(size_t) i] * frame[(size_t) i];

        ++frames;

        if (start + size >= signal.size())
            break;
    }

    std::vector<double> bands;

    for (double centre = 63.0; centre <= 10000.0; centre *= std::pow (2.0, 1.0 / 3.0))
    {
        const auto lo = (int) (centre / std::pow (2.0, 1.0 / 6.0) * size / sampleRate);
        const auto hi = (int) (centre * std::pow (2.0, 1.0 / 6.0) * size / sampleRate);
        double sum = 1.0e-12;

        for (int bin = juce::jmax (1, lo); bin <= juce::jmin (size / 2 - 1, hi); ++bin)
            sum += power[(size_t) bin];

        bands.push_back (10.0 * std::log10 (sum));
    }

    return bands;
}

/** How different two sounds' tone is: the average gap between their band
    levels in dB, after matching their overall loudness. 1 dB is subtle,
    3 dB is clearly audible. */
inline double toneDifferenceDb (const std::vector<float>& a, const std::vector<float>& b, double sampleRate)
{
    const auto bandsA = bandLevelsDb (a, sampleRate);
    const auto bandsB = bandLevelsDb (b, sampleRate);

    double offset = 0.0;

    for (size_t i = 0; i < bandsA.size(); ++i)
        offset += bandsA[i] - bandsB[i];

    offset /= (double) bandsA.size();

    double total = 0.0;

    for (size_t i = 0; i < bandsA.size(); ++i)
        total += std::abs (bandsA[i] - bandsB[i] - offset);

    return total / (double) bandsA.size();
}

/** Loudness every 20 ms, in dB. */
inline std::vector<double> loudnessCurveDb (const std::vector<float>& signal, double sampleRate)
{
    const auto hop = (size_t) (0.02 * sampleRate);
    std::vector<double> curve;

    for (size_t start = 0; start + hop <= signal.size(); start += hop)
    {
        double sum = 1.0e-12;

        for (size_t i = start; i < start + hop; ++i)
            sum += (double) signal[i] * signal[i];

        curve.push_back (10.0 * std::log10 (sum / (double) hop));
    }

    return curve;
}

/** How different two sounds' dynamics are: the average gap between their
    loudness curves in dB, after matching overall loudness, over the parts
    that are not near silence. */
inline double dynamicsDifferenceDb (const std::vector<float>& a, const std::vector<float>& b, double sampleRate)
{
    const auto curveA = loudnessCurveDb (a, sampleRate);
    const auto curveB = loudnessCurveDb (b, sampleRate);
    const auto loudest = juce::jmax (*std::max_element (curveA.begin(), curveA.end()), *std::max_element (curveB.begin(), curveB.end()));

    double offset = 0.0;
    int count = 0;

    for (size_t i = 0; i < curveA.size(); ++i)
        if (curveA[i] > loudest - 50.0 && curveB[i] > loudest - 50.0)
        {
            offset += curveA[i] - curveB[i];
            ++count;
        }

    if (count == 0)
        return 0.0;

    offset /= count;
    double total = 0.0;

    for (size_t i = 0; i < curveA.size(); ++i)
        if (curveA[i] > loudest - 50.0 && curveB[i] > loudest - 50.0)
            total += std::abs (curveA[i] - curveB[i] - offset);

    return total / count;
}
}
