#include "JamTrack.h"

#include <algorithm>
#include <cmath>

using jam::Instrument;

namespace
{
struct PartMix
{
    float defaultDb;
    bool defaultOn;
    float pan;        // -1 left to 1 right
    float reverb;     // how much goes to the room
};

const std::array<PartMix, jam::numInstruments> mix { {
    { -3.0f, true,  0.0f,  0.10f },   // drums (panned inside the kit)
    { -2.0f, true,  0.0f,  0.0f  },   // bass
    { -4.0f, true, -0.35f, 0.18f },   // rhythm guitar
    { -5.0f, true,  0.3f,  0.28f },   // piano
    { -9.0f, false, 0.1f,  0.30f },   // organ
} };

constexpr double stopFadeSeconds = 0.05;
}

JamTrack::JamTrack()
{
    for (size_t i = 0; i < mix.size(); ++i)
    {
        levelsDb[i].store (mix[i].defaultDb);
        enabled[i].store (mix[i].defaultOn);
    }
}

JamTrack::~JamTrack()
{
    delete pending.exchange (nullptr);
    delete retired.exchange (nullptr);
}

void JamTrack::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;

    drums.prepare (sampleRate);
    bass.prepare (sampleRate);
    guitar.prepare (sampleRate);
    keys.prepare (sampleRate);
    organ.prepare (sampleRate);

    room.setSampleRate (sampleRate);
    juce::Reverb::Parameters params;
    params.roomSize = 0.45f;
    params.damping = 0.55f;
    params.wetLevel = 1.0f;
    params.dryLevel = 0.0f;
    params.width = 1.0f;
    room.setParameters (params);
    room.reset();

    gains.fill (0.0f);
    fade = 1.0f;
    stopping = false;

    if (current != nullptr)
    {
        guitar.setDrive (current->guitarDrive);
        bass.setUpright (current->uprightBass);
        organ.setPercussive (current->percussiveOrgan);
    }
}

void JamTrack::setLevelDb (Instrument instrument, float db) noexcept
{
    levelsDb[(size_t) instrument].store (juce::jlimit (minLevelDb, maxLevelDb, db));
}

void JamTrack::setArrangement (jam::Arrangement arrangement)
{
    collectGarbage();
    lengthBeats.store (arrangement.lengthBeats);
    delete pending.exchange (new jam::Arrangement (std::move (arrangement)));
}

void JamTrack::collectGarbage()
{
    delete retired.exchange (nullptr);
}

//==============================================================================
void JamTrack::swapArrangement() noexcept
{
    // The old loop goes to `retired` for the message thread to free, so wait until that is empty.
    if (retired.load() != nullptr)
        return;

    auto* fresh = pending.exchange (nullptr);

    if (fresh == nullptr)
        return;

    retired.store (current.release());
    current.reset (fresh);

    guitar.setDrive (current->guitarDrive);
    bass.setUpright (current->uprightBass);
    organ.setPercussive (current->percussiveOrgan);

    if (running && position >= 0.0)
        seekTo (std::fmod (position, current->lengthBeats));
    else if (running)
        seekTo (position);
}

void JamTrack::seekTo (double beat) noexcept
{
    position = beat;
    cursor = 0;

    if (current == nullptr)
        return;

    const auto target = juce::jmax (0.0, beat);
    const auto it = std::lower_bound (current->events.begin(), current->events.end(), target,
                                      [] (const jam::NoteEvent& e, double b) { return e.beat < b; });
    cursor = (size_t) std::distance (current->events.begin(), it);
}

void JamTrack::stopAllVoices() noexcept
{
    drums.stopAll();
    bass.stopAll();
    guitar.stopAll();
    keys.stopAll();
    organ.stopAll();
}

void JamTrack::applyRequests() noexcept
{
    switch (request.exchange (Request::none))
    {
        case Request::start:
            stopAllVoices();
            fade = 1.0f;
            stopping = false;
            running = true;
            seekTo (requestedCountIn.load() ? -(double) countInBeats : 0.0);
            playing.store (true);
            justStarted.store (true);
            break;

        case Request::stop:
            if (running)
                stopping = true;   // fades out, then silences the voices

            running = false;
            playing.store (false);
            break;

        case Request::none:
            break;
    }
}

//==============================================================================
void JamTrack::fire (const jam::NoteEvent& e, double samplesPerBeat) noexcept
{
    if (! enabled[(size_t) e.instrument].load (std::memory_order_relaxed))
        return;

    const auto length = (int) (e.lengthBeats * samplesPerBeat);

    switch (e.instrument)
    {
        case Instrument::drums:
            drums.trigger ((jam::Drum) e.kind, e.velocity);
            break;

        case Instrument::bass:
            if (e.numNotes > 0)
                bass.noteOn (e.notes[0], e.velocity, length);
            break;

        case Instrument::guitar:
            guitar.play (e, length);
            break;

        case Instrument::keys:
            for (int i = 0; i < e.numNotes; ++i)
                keys.noteOn (e.notes[(size_t) i], e.velocity, length);
            break;

        case Instrument::organ:
            for (int i = 0; i < e.numNotes; ++i)
                organ.noteOn (e.notes[(size_t) i], e.velocity, length);
            break;
    }
}

void JamTrack::fireEvents (double from, double to, double samplesPerBeat) noexcept
{
    // The count-in: a stick click on each beat before the loop starts.
    if (from < 0.0)
        for (auto beat = std::ceil (from); beat < juce::jmin (to, 0.0); beat += 1.0)
            if (enabled[(size_t) Instrument::drums].load (std::memory_order_relaxed))
                drums.trigger (jam::Drum::sideStick, beat <= -(double) countInBeats ? 1.0f : 0.8f);

    if (to <= 0.0 || current == nullptr || current->lengthBeats <= 0.0)
    {
        position = to;
        return;
    }

    const auto& events = current->events;
    const auto length = current->lengthBeats;

    for (;;)
    {
        const auto end = juce::jmin (to, length);

        while (cursor < events.size() && events[cursor].beat < end)
            fire (events[cursor++], samplesPerBeat);

        if (to < length)
            break;

        if (! current->loops)
        {
            // A song that plays once: let the last notes ring and stop.
            running = false;
            playing.store (false);
            position = length;
            return;
        }

        // Round to the top of the loop.
        to -= length;
        cursor = 0;

        if (to <= 0.0)
            break;
    }

    position = to;
}

void JamTrack::renderChunk (float* left, float* right, int numSamples, double samplesPerBeat) noexcept
{
    for (auto& part : parts)
        std::fill (part.begin(), part.begin() + numSamples, 0.0f);

    std::fill (drumRight.begin(), drumRight.begin() + numSamples, 0.0f);
    std::fill (sendLeft.begin(), sendLeft.begin() + numSamples, 0.0f);
    std::fill (sendRight.begin(), sendRight.begin() + numSamples, 0.0f);

    if (running && current != nullptr)
        fireEvents (position, position + numSamples / samplesPerBeat, samplesPerBeat);

    drums.render (parts[(size_t) Instrument::drums].data(), drumRight.data(), numSamples);
    bass.render (parts[(size_t) Instrument::bass].data(), numSamples);
    guitar.render (parts[(size_t) Instrument::guitar].data(), numSamples);
    keys.render (parts[(size_t) Instrument::keys].data(), numSamples);
    organ.render (parts[(size_t) Instrument::organ].data(), numSamples);

    // Stopping fades the band out quickly instead of cutting it off.
    auto fadeEnd = fade;

    if (stopping)
    {
        fadeEnd = juce::jmax (0.0f, fade - (float) (numSamples / (stopFadeSeconds * sampleRate)));

        if (fadeEnd <= 0.0f)
        {
            stopAllVoices();
            stopping = false;
        }
    }

    const auto master = juce::Decibels::decibelsToGain (masterDb.load (std::memory_order_relaxed));
    const bool mono = left == right;

    for (size_t p = 0; p < parts.size(); ++p)
    {
        const auto target = enabled[p].load (std::memory_order_relaxed)
                                ? juce::Decibels::decibelsToGain (levelsDb[p].load (std::memory_order_relaxed)) * master
                                : 0.0f;
        const auto startGain = gains[p];
        const auto step = (target - startGain) / (float) numSamples;
        const auto fadeStep = (fadeEnd - fade) / (float) numSamples;
        const auto& part = parts[p];
        const auto pan = mix[p].pan;
        const auto send = mix[p].reverb;
        const auto panLeft = juce::jmin (1.0f, 1.0f - pan);
        const auto panRight = juce::jmin (1.0f, 1.0f + pan);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto g = (startGain + step * (float) (i + 1)) * (fade + fadeStep * (float) (i + 1));
            float l, r;

            if (p == (size_t) Instrument::drums)
            {
                l = part[(size_t) i] * g;
                r = drumRight[(size_t) i] * g;
            }
            else
            {
                l = part[(size_t) i] * g * panLeft;
                r = part[(size_t) i] * g * panRight;
            }

            sendLeft[(size_t) i] += l * send;
            sendRight[(size_t) i] += r * send;

            if (mono)
            {
                left[i] += 0.5f * (l + r);
            }
            else
            {
                left[i] += l;
                right[i] += r;
            }
        }

        gains[p] = target;
    }

    fade = stopping || fadeEnd > 0.0f ? fadeEnd : 1.0f;

    if (! stopping && ! running)
        fade = 1.0f;

    room.processStereo (sendLeft.data(), sendRight.data(), numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        if (mono)
        {
            left[i] += 0.5f * (sendLeft[(size_t) i] + sendRight[(size_t) i]);
        }
        else
        {
            left[i] += sendLeft[(size_t) i];
            right[i] += sendRight[(size_t) i];
        }
    }
}

double JamTrack::tempoAt (double beat, double fallback) const noexcept
{
    if (current == nullptr || current->tempoMap.empty())
        return fallback;

    const auto& map = current->tempoMap;
    const auto it = std::upper_bound (map.begin(), map.end(), beat,
                                      [] (double b, const jam::Arrangement::TempoChange& t) { return b < t.beat; });

    return it == map.begin() ? map.front().bpm : std::prev (it)->bpm;
}

void JamTrack::process (float* left, float* right, int numSamples, double bpm) noexcept
{
    swapArrangement();
    applyRequests();

    for (int offset = 0; offset < numSamples; offset += chunk)
    {
        const auto count = juce::jmin (chunk, numSamples - offset);
        const auto samplesPerBeat = 60.0 * sampleRate / juce::jlimit (20.0, 400.0, tempoAt (position, bpm));
        renderChunk (left + offset, right + offset, count, samplesPerBeat);
    }

    positionBeats.store (position);
}
