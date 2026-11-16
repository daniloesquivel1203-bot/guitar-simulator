#include "Tuner.h"

#include <cmath>

namespace
{
constexpr double silenceRms = 0.001;      // about -60 dB
constexpr double clarityNeeded = 0.85;    // how closely the wave must repeat to count as a note
constexpr double firstPeakRatio = 0.9;    // accept the first match this close to the best one
}

std::optional<double> PitchDetector::detect (const float* samples, int numSamples, double sampleRate)
{
    const auto minLag = juce::jmax (2, (int) std::floor (sampleRate / maxHz));
    const auto maxLag = juce::jmin (numSamples / 2, (int) std::ceil (sampleRate / minHz));

    if (samples == nullptr || maxLag <= minLag + 2)
        return {};

    double energy = 0.0;

    for (int i = 0; i < numSamples; ++i)
        energy += (double) samples[i] * samples[i];

    if (std::sqrt (energy / numSamples) < silenceRms)
        return {};

    // The normalised square difference function: +1 means the wave matches a
    // copy of itself shifted by `lag` samples perfectly, and -1 means it is inverted.
    std::vector<double> match ((size_t) maxLag + 2, 0.0);

    for (int lag = 1; lag <= maxLag + 1; ++lag)
    {
        double correlation = 0.0, power = 0.0;

        for (int i = 0; i + lag < numSamples; ++i)
        {
            const double a = samples[i], b = samples[i + lag];
            correlation += a * b;
            power += a * a + b * b;
        }

        match[(size_t) lag] = power > 0.0 ? 2.0 * correlation / power : 0.0;
    }

    // Skip the initial region where the wave still matches its unshifted self.
    int lag = 1;

    while (lag <= maxLag && match[(size_t) lag] > 0.0)
        ++lag;

    // Collect the highest point of each positive stretch after that.
    std::vector<int> peaks;
    double best = 0.0;

    while (lag <= maxLag)
    {
        while (lag <= maxLag && match[(size_t) lag] <= 0.0)
            ++lag;

        int peak = -1;

        while (lag <= maxLag && match[(size_t) lag] > 0.0)
        {
            if (peak < 0 || match[(size_t) lag] > match[(size_t) peak])
                peak = lag;

            ++lag;
        }

        if (peak >= minLag)
        {
            peaks.push_back (peak);
            best = juce::jmax (best, match[(size_t) peak]);
        }
    }

    if (peaks.empty() || best < clarityNeeded)
        return {};

    // The first strong peak is the true period. Later ones are multiples of it.
    for (auto peak : peaks)
    {
        if (match[(size_t) peak] < firstPeakRatio * best)
            continue;

        // Fit a parabola through the peak and its neighbours for sub-sample accuracy.
        const auto before = match[(size_t) peak - 1], at = match[(size_t) peak], after = match[(size_t) peak + 1];
        const auto curve = before - 2.0 * at + after;
        const auto offset = std::abs (curve) > 1.0e-12 ? 0.5 * (before - after) / curve : 0.0;
        const auto hz = sampleRate / ((double) peak + juce::jlimit (-1.0, 1.0, offset));

        if (hz >= minHz && hz <= maxHz)
            return hz;

        return {};
    }

    return {};
}

TunerReading TunerReading::fromHz (double hz)
{
    static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    const auto exact = 69.0 + 12.0 * std::log2 (hz / 440.0);
    const auto nearest = (int) std::lround (exact);

    TunerReading reading;
    reading.note = names[((nearest % 12) + 12) % 12];
    reading.octave = (int) std::floor (nearest / 12.0) - 1;
    reading.cents = (exact - nearest) * 100.0;
    reading.hz = hz;
    return reading;
}

namespace
{
const char* const noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

const std::vector<Tuning>& Tuning::getAll()
{
    static const std::vector<Tuning> tunings {
        { "Standard (E A D G B E)",        { 40, 45, 50, 55, 59, 64 } },
        { "Drop D (D A D G B E)",          { 38, 45, 50, 55, 59, 64 } },
        { "Half step down (Eb)",           { 39, 44, 49, 54, 58, 63 } },
        { "Drop C# (C# G# C# F# A# D#)",   { 37, 44, 49, 54, 58, 63 } },
        { "Whole step down (D G C F A D)", { 38, 43, 48, 53, 57, 62 } },
        { "Drop C (C G C F A D)",          { 36, 43, 48, 53, 57, 62 } },
        { "Drop B (B F# B E G# C#)",       { 35, 42, 47, 52, 56, 61 } },
        { "Double drop D (D A D G B D)",   { 38, 45, 50, 55, 59, 62 } },
        { "DADGAD",                        { 38, 45, 50, 55, 57, 62 } },
        { "Open D (D A D F# A D)",         { 38, 45, 50, 54, 57, 62 } },
        { "Open E (E B E G# B E)",         { 40, 47, 52, 56, 59, 64 } },
        { "Open G (D G D G B D)",          { 38, 43, 50, 55, 59, 62 } },
    };

    return tunings;
}

juce::String Tuning::noteName (int midiNote, bool withOctave)
{
    juce::String name (noteNames[((midiNote % 12) + 12) % 12]);

    if (withOctave)
        name << ((int) std::floor (midiNote / 12.0) - 1);

    return name;
}

double Tuning::noteToHz (int midiNote)
{
    return 440.0 * std::pow (2.0, (midiNote - 69) / 12.0);
}

double Tuning::centsFrom (double hz, int midiNote)
{
    return 1200.0 * std::log2 (hz / noteToHz (midiNote));
}

int Tuning::closestString (double hz) const
{
    int best = 0;

    for (int i = 1; i < (int) notes.size(); ++i)
        if (std::abs (centsFrom (hz, notes[(size_t) i])) < std::abs (centsFrom (hz, notes[(size_t) best])))
            best = i;

    return best;
}
