#include "JamSynths.h"

#include <cmath>

namespace jam::synth
{
namespace
{
constexpr int sineTableSize = 4096;
constexpr double twoPi = juce::MathConstants<double>::twoPi;

const std::array<float, sineTableSize + 1>& sineTable()
{
    static const auto table = []
    {
        std::array<float, sineTableSize + 1> t {};

        for (int i = 0; i <= sineTableSize; ++i)
            t[(size_t) i] = (float) std::sin (twoPi * i / sineTableSize);

        return t;
    }();

    return table;
}

/** The per-sample multiplier that makes a level fall to 37% in `seconds`. */
float decayFor (double sampleRate, double seconds)
{
    return (float) std::exp (-1.0 / (juce::jmax (0.0005, seconds) * sampleRate));
}

float onePole (double sampleRate, double hz)
{
    return (float) (1.0 - std::exp (-twoPi * hz / sampleRate));
}

float polyBlep (double t, double dt)
{
    if (t < dt)
    {
        t /= dt;
        return (float) (t + t - t * t - 1.0);
    }

    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return (float) (t * t + t + t + 1.0);
    }

    return 0.0f;
}

void addPanned (float* left, float* right, int i, float sample, float pan)
{
    left[i] += sample * juce::jmin (1.0f, 1.0f - pan);
    right[i] += sample * juce::jmin (1.0f, 1.0f + pan);
}

constexpr float silence = 1.0e-4f;
}

float fastSin (double phase) noexcept
{
    const auto& table = sineTable();
    const auto position = (phase - std::floor (phase)) * sineTableSize;
    const auto index = (int) position;
    const auto frac = (float) (position - index);
    return table[(size_t) index] + frac * (table[(size_t) index + 1] - table[(size_t) index]);
}

float midiToHz (int note) noexcept
{
    return 440.0f * std::pow (2.0f, (float) (note - 69) / 12.0f);
}

//==============================================================================
void DrumKit::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    sineTable();
    stopAll();
}

void DrumKit::stopAll() noexcept
{
    for (auto& p : pieces)
        p = {};
}

void DrumKit::trigger (Drum drum, float velocity) noexcept
{
    auto& p = pieces[(size_t) drum];
    const auto v = velocity;
    p.active = true;
    p.phase = 0.0;

    switch (drum)
    {
        case Drum::kick:
            p.level = v; p.decay = decayFor (sampleRate, 0.11);
            p.pitch = 150.0f; p.pitchTarget = 46.0f; p.pitchDecay = decayFor (sampleRate, 0.03);
            p.noiseLevel = v * 0.5f; p.noiseDecay = decayFor (sampleRate, 0.002);
            break;

        case Drum::snare:
            p.level = v * 0.6f; p.decay = decayFor (sampleRate, 0.05);
            p.pitch = 200.0f; p.pitchTarget = 180.0f; p.pitchDecay = decayFor (sampleRate, 0.02);
            p.noiseLevel = v * 0.8f; p.noiseDecay = decayFor (sampleRate, 0.075);
            break;

        case Drum::sideStick:
            p.level = v * 0.5f; p.decay = decayFor (sampleRate, 0.012);
            p.pitch = p.pitchTarget = 430.0f; p.pitchDecay = 1.0f;
            p.noiseLevel = v * 0.7f; p.noiseDecay = decayFor (sampleRate, 0.008);
            break;

        case Drum::closedHat:
        case Drum::pedalHat:
        {
            const auto pedal = drum == Drum::pedalHat;
            p.level = v * (pedal ? 0.3f : 0.5f); p.decay = decayFor (sampleRate, pedal ? 0.015 : 0.03);
            p.noiseLevel = v * (pedal ? 0.15f : 0.35f); p.noiseDecay = decayFor (sampleRate, 0.02);

            // Closing the hi-hat cuts off an open one.
            pieces[(size_t) Drum::openHat].decay = decayFor (sampleRate, 0.01);
            pieces[(size_t) Drum::openHat].noiseDecay = decayFor (sampleRate, 0.01);
            break;
        }

        case Drum::openHat:
            p.level = v * 0.5f; p.decay = decayFor (sampleRate, 0.2);
            p.noiseLevel = v * 0.35f; p.noiseDecay = decayFor (sampleRate, 0.16);
            break;

        case Drum::ride:
            p.level = v * 0.3f; p.decay = decayFor (sampleRate, 0.5);
            p.noiseLevel = v * 0.12f; p.noiseDecay = decayFor (sampleRate, 0.05);
            p.tone = 0.0f;
            break;

        case Drum::rideBell:
            p.level = v * 0.25f; p.decay = decayFor (sampleRate, 0.5);
            p.noiseLevel = v * 0.08f; p.noiseDecay = decayFor (sampleRate, 0.04);
            p.tone = v * 0.25f;
            break;

        case Drum::crash:
            p.level = v * 0.45f; p.decay = decayFor (sampleRate, 0.55);
            p.noiseLevel = v * 0.5f; p.noiseDecay = decayFor (sampleRate, 0.45);
            break;

        case Drum::highTom:
        case Drum::midTom:
        case Drum::lowTom:
        {
            const auto hz = drum == Drum::highTom ? 210.0f : drum == Drum::midTom ? 150.0f : 105.0f;
            p.level = v * 0.8f; p.decay = decayFor (sampleRate, 0.12);
            p.pitch = hz * 1.5f; p.pitchTarget = hz; p.pitchDecay = decayFor (sampleRate, 0.05);
            p.noiseLevel = v * 0.2f; p.noiseDecay = decayFor (sampleRate, 0.01);
            break;
        }
    }
}

float DrumKit::metal (double* phases, const float* ratios, double baseHz) noexcept
{
    float sum = 0.0f;

    for (int i = 0; i < 6; ++i)
    {
        phases[i] += ratios[i] * baseHz / sampleRate;
        phases[i] -= std::floor (phases[i]);
        sum += phases[i] < 0.5 ? 1.0f : -1.0f;
    }

    return sum / 6.0f;
}

void DrumKit::render (float* left, float* right, int numSamples) noexcept
{
    // Six square waves at clashing pitches make a metallic cymbal sound.
    static const float ratios[6] { 1.0f, 1.4827f, 1.8003f, 2.5461f, 2.6304f, 3.8968f };

    const auto hatHighCoeff = onePole (sampleRate, 6500.0);
    const auto rideHighCoeff = onePole (sampleRate, 3500.0);
    const auto crashHighCoeff = onePole (sampleRate, 4000.0);
    const auto snareHighCoeff = onePole (sampleRate, 1200.0);
    const auto stickLow = onePole (sampleRate, 600.0);
    const auto stickHigh = onePole (sampleRate, 3000.0);

    // The square waves are rich in harmonics; this keeps the cymbals from fizzing.
    const auto cymbalLow = onePole (sampleRate, 9000.0);

    const auto sweep = [this] (Piece& p) noexcept
    {
        const auto s = fastSin (p.phase);
        p.phase += p.pitch / sampleRate;
        p.pitch = p.pitchTarget + (p.pitch - p.pitchTarget) * p.pitchDecay;
        return s;
    };

    const auto fade = [] (Piece& p) noexcept
    {
        p.level *= p.decay;
        p.noiseLevel *= p.noiseDecay;

        if (p.level < silence && p.noiseLevel < silence)
            p.active = false;
    };

    for (int i = 0; i < numSamples; ++i)
    {
        const auto n = noise.next();

        if (auto& p = pieces[(size_t) Drum::kick]; p.active)
        {
            addPanned (left, right, i, sweep (p) * p.level + n * p.noiseLevel * 0.3f, 0.0f);
            fade (p);
        }

        if (auto& p = pieces[(size_t) Drum::snare]; p.active)
        {
            snareHigh += snareHighCoeff * (n - snareHigh);
            addPanned (left, right, i, sweep (p) * p.level + (n - snareHigh) * p.noiseLevel, 0.05f);
            fade (p);
        }

        if (auto& p = pieces[(size_t) Drum::sideStick]; p.active)
        {
            stickBand1 += stickHigh * (n - stickBand1);
            stickBand2 += stickLow * (n - stickBand2);
            addPanned (left, right, i, sweep (p) * p.level + (stickBand1 - stickBand2) * p.noiseLevel * 1.5f, 0.05f);
            fade (p);
        }

        {
            auto& closed = pieces[(size_t) Drum::closedHat];
            auto& open = pieces[(size_t) Drum::openHat];
            auto& pedal = pieces[(size_t) Drum::pedalHat];

            if (closed.active || open.active || pedal.active)
            {
                const auto raw = metal (hatPhases.data(), ratios, 2400.0) * (closed.level + open.level + pedal.level)
                                 + n * (closed.noiseLevel + open.noiseLevel + pedal.noiseLevel);
                hatHigh += hatHighCoeff * (raw - hatHigh);
                hatLow += cymbalLow * ((raw - hatHigh) - hatLow);
                addPanned (left, right, i, hatLow * 0.6f, 0.35f);

                for (auto* p : { &closed, &open, &pedal })
                    if (p->active)
                        fade (*p);
            }
        }

        {
            auto& ride = pieces[(size_t) Drum::ride];
            auto& bell = pieces[(size_t) Drum::rideBell];

            if (ride.active || bell.active)
            {
                const auto raw = metal (ridePhases.data(), ratios, 1300.0) * (ride.level + bell.level)
                                 + n * (ride.noiseLevel + bell.noiseLevel);
                rideHigh += rideHighCoeff * (raw - rideHigh);
                rideLow += cymbalLow * ((raw - rideHigh) - rideLow);
                auto sample = rideLow * 0.8f;

                if (bell.active)
                {
                    sample += (fastSin (bell.phase) + 0.6f * fastSin (bell.phase * 1.5)) * bell.tone;
                    bell.phase += 760.0 / sampleRate;
                    bell.tone *= bell.decay;
                }

                addPanned (left, right, i, sample, -0.3f);

                for (auto* p : { &ride, &bell })
                    if (p->active)
                        fade (*p);
            }
        }

        if (auto& p = pieces[(size_t) Drum::crash]; p.active)
        {
            const auto raw = metal (crashPhases.data(), ratios, 1700.0) * p.level + n * p.noiseLevel;
            crashHigh += crashHighCoeff * (raw - crashHigh);
            crashLow += cymbalLow * ((raw - crashHigh) - crashLow);
            addPanned (left, right, i, crashLow * 0.75f, -0.2f);
            fade (p);
        }

        for (auto [drum, pan] : { std::pair { Drum::highTom, 0.25f }, std::pair { Drum::midTom, 0.0f }, std::pair { Drum::lowTom, -0.25f } })
        {
            if (auto& p = pieces[(size_t) drum]; p.active)
            {
                addPanned (left, right, i, sweep (p) * p.level + n * p.noiseLevel * 0.3f, pan);
                fade (p);
            }
        }
    }
}

//==============================================================================
void Bass::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    releaseCoeff = decayFor (sampleRate, 0.02);
    filterCoeff = decayFor (sampleRate, 0.08);
    stopAll();
}

void Bass::stopAll() noexcept
{
    for (auto& v : voices)
        v = {};
}

void Bass::noteOn (int note, float velocity, int lengthSamples) noexcept
{
    // One note at a time, but the last one fades out on the other voice instead of clicking.
    voices[(size_t) (1 - nextVoice)].releasing = true;

    auto& v = voices[(size_t) nextVoice];
    nextVoice = 1 - nextVoice;

    v.active = true;
    v.releasing = false;
    v.step = midiToHz (note) / sampleRate;
    v.velocity = velocity;
    v.level = 0.0f;
    v.env = 0.0f;
    v.filterEnv = 1.0f;
    v.samplesLeft = lengthSamples;
    v.ic1 = v.ic2 = 0.0f;

    attackStep = 1.0f / (float) (sampleRate * (upright ? 0.006 : 0.003));
    decayCoeff = decayFor (sampleRate, upright ? 0.6 : 1.6);
}

void Bass::render (float* out, int numSamples) noexcept
{
    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        for (int i = 0; i < numSamples; ++i)
        {
            float raw;

            if (upright)
            {
                raw = fastSin (v.phase) + 0.3f * fastSin (v.phase * 2.0) + 0.12f * fastSin (v.phase * 3.0);
            }
            else
            {
                const auto saw = (float) (2.0 * v.phase - 1.0) - polyBlep (v.phase, v.step);
                raw = 0.55f * saw + 0.7f * fastSin (v.phase);
            }

            v.phase += v.step;
            v.phase -= std::floor (v.phase);

            // A low-pass that closes after the pluck, like a string losing its brightness.
            const auto cutoff = (upright ? 220.0f : 320.0f) + v.filterEnv * v.velocity * (upright ? 900.0f : 2400.0f);
            const auto g = std::tan (juce::MathConstants<float>::pi * juce::jmin (cutoff, (float) sampleRate * 0.45f) / (float) sampleRate);
            const auto k = 1.4f;
            const auto a1 = 1.0f / (1.0f + g * (g + k));
            const auto v3 = raw - v.ic2;
            const auto v1 = a1 * v.ic1 + g * a1 * v3;
            const auto v2 = v.ic2 + g * a1 * v.ic1 + g * g * a1 * v3;
            v.ic1 = 2.0f * v1 - v.ic1;
            v.ic2 = 2.0f * v2 - v.ic2;
            v.filterEnv *= filterCoeff;

            if (v.releasing)
            {
                v.env *= releaseCoeff;
            }
            else if (v.level < 1.0f)
            {
                v.level = juce::jmin (1.0f, v.level + attackStep);
                v.env = v.level;
            }
            else
            {
                v.env *= decayCoeff;
            }

            if (! v.releasing && --v.samplesLeft <= 0)
                v.releasing = true;

            out[i] += v2 * v.env * v.velocity * (upright ? 0.4f : 0.6f);

            if (v.releasing && v.env < silence)
            {
                v.active = false;
                break;
            }
        }
    }
}

//==============================================================================
void Guitar::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    const auto size = juce::nextPowerOfTwo ((int) (sampleRate / 30.0) + 8);

    for (auto& s : strings)
    {
        s = {};
        s.buffer.assign ((size_t) size, 0.0f);
    }

    preHigh = postLow1 = postLow2 = dcX = dcY = 0.0f;
}

void Guitar::stopAll() noexcept
{
    for (auto& s : strings)
    {
        std::fill (s.buffer.begin(), s.buffer.end(), 0.0f);
        s.active = false;
        s.pendingNote = -1;
        s.loopState = 0.0f;
    }
}

float Guitar::feedbackFor (double hz, double seconds) const noexcept
{
    // Each trip round the loop loses this much, so the note is 60 dB down after `seconds`.
    return (float) std::pow (0.001, 1.0 / (juce::jmax (0.01, seconds) * hz));
}

void Guitar::pluck (String& s, int note, float velocity, Articulation articulation) noexcept
{
    const auto hz = (double) midiToHz (note);
    const auto palm = articulation == Articulation::palmMute;
    const auto dead = articulation == Articulation::dead;

    s.brightness = dead ? 0.2f : palm ? 0.3f : 0.55f + 0.35f * velocity;
    s.delay = juce::jlimit (2.0, (double) s.buffer.size() - 4.0,
                            sampleRate / hz - (1.0 - s.brightness) / s.brightness);

    const auto ring = juce::jlimit (1.5, 5.0, 900.0 / hz);
    s.feedback = feedbackFor (hz, dead ? 0.03 : palm ? 0.3 : ring);

    // Fill one period of the string with a burst of filtered noise: the pick.
    const auto mask = (int) s.buffer.size() - 1;
    const auto length = (int) s.delay + 2;
    const auto pick = onePole (sampleRate, (dead ? 1500.0 : palm ? 1800.0 : 2500.0 + 6000.0 * velocity));
    float state = 0.0f;

    for (int i = 0; i < length; ++i)
    {
        state += pick * (noise.next() - state);
        auto& slot = s.buffer[(size_t) ((s.writeIndex - length + i) & mask)];
        slot = slot * 0.2f + state * velocity * 1.6f;
    }

    s.loopState = 0.0f;
    s.active = true;
    s.silentFor = 0;
}

void Guitar::damp (String& s, double seconds) noexcept
{
    if (! s.active)
        return;

    s.feedback = juce::jmin (s.feedback, feedbackFor (sampleRate / juce::jmax (2.0, s.delay), seconds));
    s.brightness = juce::jmin (s.brightness, 0.3f);
}

void Guitar::play (const NoteEvent& e, int lengthSamples) noexcept
{
    const auto count = juce::jmin ((int) e.numNotes, numStrings);

    if (count == 0)
        return;

    const auto articulation = (Articulation) e.kind;
    const auto gap = (int) (sampleRate * (articulation == Articulation::dead ? 0.004 : e.strumUp ? 0.007 : 0.010));

    for (int i = 0; i < numStrings; ++i)
    {
        auto& s = strings[(size_t) i];

        if (i >= count)
        {
            // Strings that are not in the chord are muted by the fretting hand.
            s.pendingNote = -1;
            damp (s, 0.04);
            continue;
        }

        const auto order = e.strumUp ? count - 1 - i : i;
        s.pendingNote = e.notes[(size_t) i];
        s.pendingDelay = count > 1 ? order * gap : 0;
        s.pendingVelocity = e.velocity * (e.strumUp ? 0.85f : 1.0f);
        s.pendingArticulation = articulation;
        s.samplesLeft = lengthSamples + s.pendingDelay;
    }
}

void Guitar::render (float* out, int numSamples) noexcept
{
    const auto mask = [] (const String& s) { return (int) s.buffer.size() - 1; };
    const auto preHighCoeff = onePole (sampleRate, 140.0);
    const auto postCoeff = onePole (sampleRate, drive > 0.0f ? 3200.0 : 6500.0);
    const auto dcCoeff = decayFor (sampleRate, 0.02);
    const auto gain = 1.0f + drive * 30.0f;
    const auto makeUp = drive > 0.0f ? 0.42f / std::tanh (juce::jmin (gain * 0.3f, 3.0f)) : 1.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float sum = 0.0f;

        for (auto& s : strings)
        {
            if (s.pendingNote >= 0 && --s.pendingDelay <= 0)
            {
                pluck (s, s.pendingNote, s.pendingVelocity, s.pendingArticulation);
                s.pendingNote = -1;
            }

            if (! s.active)
                continue;

            if (s.samplesLeft > 0 && --s.samplesLeft == 0)
                damp (s, 0.06);

            const auto m = mask (s);
            const auto readPos = (double) s.writeIndex - s.delay;
            const auto whole = (int) std::floor (readPos);
            const auto frac = (float) (readPos - whole);
            const auto a = s.buffer[(size_t) (whole & m)];
            const auto b = s.buffer[(size_t) ((whole + 1) & m)];
            const auto read = a + frac * (b - a);

            s.loopState += s.brightness * (read - s.loopState);
            const auto y = s.loopState * s.feedback;
            s.buffer[(size_t) (s.writeIndex & m)] = y;
            s.writeIndex = (s.writeIndex + 1) & m;
            sum += y;

            if (std::abs (y) < 1.0e-5f)
            {
                if (++s.silentFor > (int) s.buffer.size())
                    s.active = false;
            }
            else
            {
                s.silentFor = 0;
            }
        }

        auto x = sum * 0.7f;

        if (drive > 0.0f)
        {
            // An overdriven amp: tighten the lows, clip softly, then roll off the fizz.
            preHigh += preHighCoeff * (x - preHigh);
            x = std::tanh ((x - preHigh) * gain) * makeUp;
        }

        postLow1 += postCoeff * (x - postLow1);
        postLow2 += postCoeff * (postLow1 - postLow2);

        // Remove any DC the clipping leaves.
        const auto y = postLow2 - dcX + dcCoeff * dcY;
        dcX = postLow2;
        dcY = y;
        out[i] += y;
    }
}

//==============================================================================
void Keys::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    releaseCoeff = decayFor (sampleRate, 0.06);
    stopAll();
}

void Keys::stopAll() noexcept
{
    for (auto& v : voices)
        v = {};
}

void Keys::noteOn (int note, float velocity, int lengthSamples) noexcept
{
    Voice* chosen = nullptr;

    for (auto& v : voices)
        if (! v.active || v.note == note)
        {
            chosen = &v;
            break;
        }

    if (chosen == nullptr)
    {
        chosen = &voices[0];

        for (auto& v : voices)
            if (v.age < chosen->age)
                chosen = &v;
    }

    auto& v = *chosen;
    const auto hz = midiToHz (note);
    v.active = true;
    v.releasing = false;
    v.note = note;
    v.age = ++counter;
    v.step = hz / sampleRate;
    v.phase = v.modPhase = v.tinePhase = 0.0;
    v.level = velocity * 0.35f;
    v.decay = decayFor (sampleRate, juce::jlimit (0.6, 3.0, 3.0 - (note - 48) * 0.05));
    v.modIndex = 0.5f + velocity * 1.6f;
    v.modDecay = decayFor (sampleRate, 0.4);
    v.tineLevel = hz * 14.0f < sampleRate * 0.45 ? velocity * 0.12f : 0.0f;
    v.tineDecay = decayFor (sampleRate, 0.03);
    v.samplesLeft = lengthSamples;
}

void Keys::render (float* out, int numSamples) noexcept
{
    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto modulator = fastSin (v.modPhase);
            const auto sample = fastSin (v.phase + modulator * v.modIndex * 0.159) * v.level
                                + fastSin (v.tinePhase) * v.tineLevel * v.level;

            v.phase += v.step;
            v.modPhase += v.step;
            v.tinePhase += v.step * 14.0;
            v.modIndex = juce::jmax (0.25f, v.modIndex * v.modDecay);
            v.tineLevel *= v.tineDecay;
            v.level *= v.releasing ? releaseCoeff : v.decay;

            if (! v.releasing && --v.samplesLeft <= 0)
                v.releasing = true;

            out[i] += sample;

            if (v.level < silence)
            {
                v.active = false;
                break;
            }
        }

        v.phase -= std::floor (v.phase);
        v.modPhase -= std::floor (v.modPhase);
        v.tinePhase -= std::floor (v.tinePhase);
    }
}

//==============================================================================
void Organ::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    attackCoeff = 1.0f - decayFor (sampleRate, 0.004);
    releaseCoeff = 1.0f - decayFor (sampleRate, 0.025);
    percussionDecay = decayFor (sampleRate, 0.15);
    stopAll();
}

void Organ::stopAll() noexcept
{
    for (auto& v : voices)
        v = {};
}

void Organ::noteOn (int note, float velocity, int lengthSamples) noexcept
{
    Voice* chosen = nullptr;

    for (auto& v : voices)
        if (! v.active || v.note == note)
        {
            chosen = &v;
            break;
        }

    if (chosen == nullptr)
    {
        chosen = &voices[0];

        for (auto& v : voices)
            if (v.age < chosen->age)
                chosen = &v;
    }

    auto& v = *chosen;

    if (! v.active || v.note != note)
    {
        v.phase = 0.0;
        v.level = 0.0f;
    }

    v.active = true;
    v.note = note;
    v.age = ++counter;
    v.step = midiToHz (note) / sampleRate;
    v.target = velocity * 0.09f;
    v.percussion = percussive ? velocity * 0.12f : 0.0f;
    v.samplesLeft = lengthSamples;
}

void Organ::render (float* out, int numSamples) noexcept
{
    const auto bodyCoeff = onePole (sampleRate, 5000.0);
    const auto swirlStep = 5.8 / sampleRate;

    for (int i = 0; i < numSamples; ++i)
    {
        float sum = 0.0f;

        for (auto& v : voices)
        {
            if (! v.active)
                continue;

            const auto p = v.phase;
            const auto tone = 0.7f * fastSin (p * 0.5) + fastSin (p) + 0.6f * fastSin (p * 2.0)
                              + 0.35f * fastSin (p * 3.0) + 0.25f * fastSin (p * 4.0);

            sum += tone * v.level + fastSin (p * 3.0) * v.percussion;

            v.phase += v.step;

            if (v.phase > 64.0)
                v.phase -= 64.0;

            if (v.samplesLeft > 0 && --v.samplesLeft == 0)
                v.target = 0.0f;

            v.level += (v.target > v.level ? attackCoeff : releaseCoeff) * (v.target - v.level);
            v.percussion *= percussionDecay;

            if (v.target <= 0.0f && v.level < silence)
                v.active = false;
        }

        // A gentle swirl, like a slow rotating speaker.
        swirlPhase += swirlStep;
        swirlPhase -= std::floor (swirlPhase);
        sum *= 1.0f + 0.12f * fastSin (swirlPhase);

        bodyState += bodyCoeff * (sum - bodyState);
        out[i] += bodyState;
    }
}
}
