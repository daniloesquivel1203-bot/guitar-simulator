#include "Practice.h"

#include <cmath>

namespace
{
constexpr double clickSeconds = 0.04;
constexpr double clickDecaySeconds = 0.008;
constexpr double accentHz = 1500.0;
constexpr double beatHz = 1000.0;
constexpr float accentLevel = 1.0f;
constexpr float beatLevel = 0.6f;

constexpr double minLoopSeconds = 0.25;
constexpr double loopEdgeFadeSeconds = 0.01;
constexpr double levelSmoothingSeconds = 0.02;

constexpr double layerFadeSeconds = 0.005;

// Folding and clearing layers happen a piece at a time, so a long loop never
// makes one audio block late.
constexpr int backgroundSamplesPerBlock = 96000;

constexpr int recorderBufferSeconds = 4;
}

//==============================================================================
void Metronome::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    wasRunning = false;
    clickSamplesLeft = 0;
    currentBeat.store (-1);
}

void Metronome::startClick (bool accented) noexcept
{
    clickPhase = 0.0;
    clickPhaseStep = juce::MathConstants<double>::twoPi * (accented ? accentHz : beatHz) / sampleRate;
    clickLevel = accented ? accentLevel : beatLevel;
    clickDecay = (float) std::exp (-1.0 / (clickDecaySeconds * sampleRate));
    clickSamplesLeft = (int) (clickSeconds * sampleRate);
}

void Metronome::process (float* left, float* right, int numSamples) noexcept
{
    const bool run = running.load();

    if (run && ! wasRunning)
    {
        // Starting always begins on the first beat of the bar, straight away.
        samplesToNextBeat = 0.0;
        beat = 0;
    }

    wasRunning = run;

    if (restartRequested.exchange (false) && run)
    {
        samplesToNextBeat = 0.0;
        beat = 0;
    }

    if (! run)
        currentBeat.store (-1);

    const auto gain = juce::Decibels::decibelsToGain (volumeDb.load());
    const auto beats = beatsPerBar.load();
    const auto samplesPerBeat = 60.0 * sampleRate / tempo.load();

    for (int i = 0; i < numSamples; ++i)
    {
        if (run)
        {
            if (samplesToNextBeat <= 0.0)
            {
                beat %= beats;
                startClick (beat == 0 && beats > 1 && accent.load());
                currentBeat.store (beat);
                samplesToNextBeat += samplesPerBeat;
                ++beat;
            }

            samplesToNextBeat -= 1.0;
        }

        if (clickSamplesLeft > 0)
        {
            const auto sample = (float) std::sin (clickPhase) * clickLevel * gain;
            clickPhase += clickPhaseStep;
            clickLevel *= clickDecay;
            --clickSamplesLeft;

            left[i] += sample;

            if (right != left)
                right[i] += sample;
        }
    }
}

//==============================================================================
std::optional<double> TapTempo::tap (double seconds)
{
    if (lastTap.has_value())
    {
        const auto gap = seconds - *lastTap;

        if (gap <= 0.0 || gap > maxGapSeconds)
        {
            intervals.clear();
        }
        else
        {
            intervals.push_back (gap);

            if ((int) intervals.size() > maxIntervals)
                intervals.erase (intervals.begin());
        }
    }

    lastTap = seconds;

    if (intervals.empty())
        return std::nullopt;

    double total = 0.0;

    for (auto interval : intervals)
        total += interval;

    return juce::jlimit (Metronome::minBpm, Metronome::maxBpm, 60.0 * (double) intervals.size() / total);
}

//==============================================================================
void Looper::prepare (double newSampleRate)
{
    level.reset (newSampleRate, levelSmoothingSeconds);
    level.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (levelDb.load()));

    if (juce::exactlyEqual (newSampleRate, sampleRate) && capacity > 0)
        return;

    sampleRate = newSampleRate;
    capacity = (int) (maxLoopSeconds * sampleRate);

    // Zeroed allocations only take up real memory once they are used, so the
    // layers cost nothing until they are recorded. If the memory is not there
    // the longest loop gets shorter instead of the app failing.
    for (;;)
    {
        bool allocated = true;

        for (auto& slot : slots)
        {
            slot.memory.free();
            slot.memory.calloc ((size_t) capacity * 2);
            allocated = allocated && slot.memory != nullptr;
        }

        if (allocated)
            break;

        capacity /= 2;

        if (capacity < (int) (minLoopSeconds * sampleRate))
        {
            for (auto& slot : slots)
                slot.memory.free();

            capacity = 0;
            break;
        }
    }

    for (auto& slot : slots)
    {
        slot.left = slot.memory.get();
        slot.right = capacity > 0 ? slot.memory.get() + capacity : nullptr;
        slot.use = SlotUse::free;
        slot.dirty = slot.cleared = 0;
        slot.gain = 1.0f;
    }

    fadeStep = 1.0f / juce::jmax (1.0f, (float) (layerFadeSeconds * sampleRate));
    length = pos = 0;
    liveSlot = foldSource = foldTarget = -1;
    heldCommand.reset();
    state.store (State::empty);
    loopLength.store (0);
    position.store (0);
    capacityInSamples.store (capacity);
    resetLayerDisplay();
}

void Looper::resetLayerDisplay() noexcept
{
    numLanes.store (0, std::memory_order_release);
    foldedLayers.store (0);
    binsFilled.store (0, std::memory_order_release);
    samplesPerRecordedBin = 1;
    undoAvailable.store (false);
}

void Looper::post (Command command, int layerId)
{
    const auto scope = commandFifo.write (1);

    if (scope.blockSize1 > 0)
        commands[(size_t) scope.startIndex1] = { command, layerId };
    else if (scope.blockSize2 > 0)
        commands[(size_t) scope.startIndex2] = { command, layerId };
}

int Looper::findFreeSlot() const noexcept
{
    for (int index = 0; index < numSlots; ++index)
        if (slots[(size_t) index].use == SlotUse::free)
            return index;

    return -1;
}

bool Looper::needsFreeSlot (const Message& message) const noexcept
{
    const auto current = state.load();

    if (message.command == Command::main)
        return current == State::empty;

    return message.command == Command::overdub
           && (current == State::recording || current == State::playing || current == State::stopped);
}

void Looper::doBackgroundWork() noexcept
{
    auto budget = backgroundSamplesPerBlock;

    // Moving a layer into another one a sample at a time keeps their sum, and
    // so the sound, exactly the same throughout.
    if (foldSource >= 0)
    {
        auto& source = slots[(size_t) foldSource];
        auto& target = slots[(size_t) foldTarget];
        const auto todo = juce::jmin (budget, length - foldDone);

        for (int k = foldDone; k < foldDone + todo; ++k)
        {
            target.left[k] += source.left[k];
            target.right[k] += source.right[k];
            source.left[k] = source.right[k] = 0.0f;
        }

        foldDone += todo;
        budget -= todo;

        if (foldDone >= length)
        {
            source.use = SlotUse::free;
            source.dirty = 0;
            foldSource = foldTarget = -1;
        }
    }

    for (auto& slot : slots)
    {
        if (slot.use != SlotUse::clearing || budget <= 0)
            continue;

        const auto todo = juce::jmin (budget, slot.dirty - slot.cleared);
        juce::FloatVectorOperations::clear (slot.left + slot.cleared, todo);
        juce::FloatVectorOperations::clear (slot.right + slot.cleared, todo);
        slot.cleared += todo;
        budget -= todo;

        if (slot.cleared >= slot.dirty)
        {
            slot.use = SlotUse::free;
            slot.dirty = slot.cleared = 0;
        }
    }
}

void Looper::runCommands() noexcept
{
    doBackgroundWork();

    // A command waiting for a free layer runs first, to keep commands in order.
    if (heldCommand.has_value())
    {
        if (needsFreeSlot (*heldCommand) && findFreeSlot() < 0)
            return;

        const auto message = *heldCommand;
        heldCommand.reset();
        handle (message);
    }

    while (commandFifo.getNumReady() > 0)
    {
        Message message {};
        const auto scope = commandFifo.read (1);

        if (scope.blockSize1 > 0)
            message = commands[(size_t) scope.startIndex1];
        else if (scope.blockSize2 > 0)
            message = commands[(size_t) scope.startIndex2];
        else
            break;

        if (needsFreeSlot (message) && findFreeSlot() < 0)
        {
            heldCommand = message;
            return;
        }

        handle (message);
    }
}

void Looper::handle (const Message& message) noexcept
{
    if (capacity == 0)
        return;

    const auto current = state.load();

    switch (message.command)
    {
        case Command::main:
            if (current == State::empty)
            {
                pos = length = 0;
                resetLayerDisplay();
                liveSlot = claimSlot();
                appendLane (liveSlot);
                state.store (State::recording);
            }
            else if (current == State::recording)
            {
                finishFirstRecording();
            }
            else if (current == State::overdubbing)
            {
                finishOverdub();
                state.store (State::playing);
            }
            else if (current == State::stopped)
            {
                pos = 0;
                state.store (State::playing);
            }
            break;

        case Command::overdub:
            if (current == State::recording)
            {
                finishFirstRecording();

                // A recording too short to keep leaves nothing to overdub on.
                if (state.load() == State::playing)
                    beginOverdub();
            }
            else if (current == State::playing)
            {
                beginOverdub();
            }
            else if (current == State::overdubbing)
            {
                finishOverdub();
                state.store (State::playing);
            }
            else if (current == State::stopped)
            {
                pos = 0;
                beginOverdub();
            }
            break;

        case Command::stop:
            if (current == State::recording)
                finishFirstRecording();
            else if (current == State::overdubbing)
                finishOverdub();

            if (state.load() != State::empty)
            {
                pos = 0;
                state.store (State::stopped);
            }
            break;

        case Command::undo:
            if (current == State::overdubbing)
            {
                finishOverdub();
                state.store (State::playing);
            }

            if (numLanes.load() > 1)
                removeLaneAt (numLanes.load() - 1);
            break;

        case Command::removeLayer:
            removeLayerById (message.layerId);
            break;

        case Command::clear:
            pos = length = 0;
            liveSlot = foldSource = foldTarget = -1;

            for (auto& slot : slots)
                if (slot.use != SlotUse::free && slot.use != SlotUse::clearing)
                    retire (slot);

            resetLayerDisplay();
            state.store (State::empty);
            break;
    }
}

int Looper::claimSlot() noexcept
{
    const auto index = findFreeSlot();
    jassert (index >= 0);

    auto& slot = slots[(size_t) index];
    slot.use = SlotUse::layer;
    slot.gain = 1.0f;
    slot.dirty = slot.cleared = 0;
    slotIds[(size_t) index].store (nextLayerId++, std::memory_order_relaxed);

    for (auto& peak : layerPeaks[(size_t) index])
        peak.store (0.0f, std::memory_order_relaxed);

    return index;
}

void Looper::retire (Slot& slot) noexcept
{
    slot.cleared = 0;
    slot.use = slot.dirty > 0 ? SlotUse::clearing : SlotUse::free;
}

void Looper::appendLane (int slotIndex) noexcept
{
    const auto lanes = numLanes.load();
    laneSlots[(size_t) lanes].store (slotIndex, std::memory_order_relaxed);
    numLanes.store (lanes + 1, std::memory_order_release);
    undoAvailable.store (lanes + 1 > 1);
}

void Looper::dropLaneEntry (int lane) noexcept
{
    const auto lanes = numLanes.load();

    for (int k = lane; k < lanes - 1; ++k)
        laneSlots[(size_t) k].store (laneSlots[(size_t) k + 1].load (std::memory_order_relaxed), std::memory_order_relaxed);

    numLanes.store (lanes - 1, std::memory_order_release);
    undoAvailable.store (lanes - 1 > 1);
}

void Looper::removeLaneAt (int lane) noexcept
{
    const auto index = laneSlots[(size_t) lane].load (std::memory_order_relaxed);

    if (lane == 0)
    {
        foldedLayers.store (0);

        // A layer still being folded into this lane is part of it, so it goes too.
        if (foldSource >= 0 && foldTarget == index)
        {
            slots[(size_t) foldSource].use = SlotUse::fading;
            foldSource = foldTarget = -1;
        }
    }

    slots[(size_t) index].use = SlotUse::fading;
    dropLaneEntry (lane);
}

void Looper::removeLayerById (int layerId) noexcept
{
    auto lanes = numLanes.load();
    auto lane = -1;

    for (int k = 0; k < lanes; ++k)
        if (slotIds[(size_t) laneSlots[(size_t) k].load (std::memory_order_relaxed)].load (std::memory_order_relaxed) == layerId)
            lane = k;

    // The first recording has to finish, and the last layer stays: Clear removes it.
    if (lane < 0 || state.load() == State::recording || lanes < 2)
        return;

    if (state.load() == State::overdubbing && laneSlots[(size_t) lane].load (std::memory_order_relaxed) == liveSlot)
    {
        const auto wroteAudio = overdubWrote;
        finishOverdub();
        state.store (State::playing);

        if (! wroteAudio)
            return;
    }

    removeLaneAt (lane);
}

void Looper::beginOverdub() noexcept
{
    if (numLanes.load() >= maxLayerLanes)
    {
        // Out of lanes: fold the oldest overdub into the first lane.
        foldTarget = laneSlots[0].load (std::memory_order_relaxed);
        foldSource = laneSlots[1].load (std::memory_order_relaxed);
        foldDone = 0;
        slots[(size_t) foldSource].use = SlotUse::folding;

        for (int bin = 0; bin < layerBins; ++bin)
        {
            auto& peak = layerPeaks[(size_t) foldTarget][(size_t) bin];
            peak.store (juce::jmax (peak.load (std::memory_order_relaxed),
                                    layerPeaks[(size_t) foldSource][(size_t) bin].load (std::memory_order_relaxed)),
                        std::memory_order_relaxed);
        }

        dropLaneEntry (1);
        foldedLayers.fetch_add (1);
    }

    liveSlot = claimSlot();
    slots[(size_t) liveSlot].dirty = length;
    overdubWrote = false;
    appendLane (liveSlot);
    state.store (State::overdubbing);
}

void Looper::noteRecordedPeak (float peak) noexcept
{
    auto& lane = layerPeaks[(size_t) liveSlot];
    auto bin = pos / samplesPerRecordedBin;

    if (bin >= layerBins)
    {
        // Out of bins: halve the detail so the recording fits again.
        for (int b = 0; b < layerBins / 2; ++b)
            lane[(size_t) b].store (juce::jmax (lane[(size_t) (2 * b)].load (std::memory_order_relaxed),
                                                lane[(size_t) (2 * b + 1)].load (std::memory_order_relaxed)),
                                    std::memory_order_relaxed);

        for (int b = layerBins / 2; b < layerBins; ++b)
            lane[(size_t) b].store (0.0f, std::memory_order_relaxed);

        samplesPerRecordedBin *= 2;
        bin = pos / samplesPerRecordedBin;
    }

    auto& slot = lane[(size_t) bin];

    if (peak > slot.load (std::memory_order_relaxed))
        slot.store (peak, std::memory_order_relaxed);

    binsFilled.store (bin + 1, std::memory_order_release);
}

void Looper::noteOverdubPeak (float peak) noexcept
{
    const auto bin = (size_t) ((juce::int64) pos * layerBins / length);
    auto& slot = layerPeaks[(size_t) liveSlot][bin];

    if (peak > slot.load (std::memory_order_relaxed))
        slot.store (peak, std::memory_order_relaxed);
}

void Looper::stretchRecordedLayer() noexcept
{
    // Spread the bins in use across the whole lane, so a bin's place matches
    // its place in the loop. Going backwards never reads a bin already written.
    auto& lane = layerPeaks[(size_t) liveSlot];
    const auto used = juce::jmax (1, binsFilled.load());

    for (int target = layerBins - 1; target >= 0; --target)
        lane[(size_t) target].store (lane[(size_t) (target * used / layerBins)].load (std::memory_order_relaxed),
                                     std::memory_order_relaxed);

    binsFilled.store (layerBins, std::memory_order_release);
}

void Looper::finishFirstRecording() noexcept
{
    auto& slot = slots[(size_t) liveSlot];
    length = pos;
    pos = 0;

    if (length < (int) (minLoopSeconds * sampleRate))
    {
        // Too short to be a loop, most likely a double press.
        length = 0;
        retire (slot);
        liveSlot = -1;
        resetLayerDisplay();
        state.store (State::empty);
        return;
    }

    stretchRecordedLayer();

    // Fade the ends a little so the seam does not click.
    const auto fade = juce::jmin (length / 4, (int) (loopEdgeFadeSeconds * sampleRate));

    for (int k = 0; k < fade; ++k)
    {
        const auto gain = (float) k / (float) fade;

        for (auto* channel : { slot.left, slot.right })
        {
            channel[k] *= gain;
            channel[length - 1 - k] *= gain;
        }
    }

    liveSlot = -1;
    state.store (State::playing);
}

void Looper::finishOverdub() noexcept
{
    if (! overdubWrote)
    {
        // Nothing was recorded, so there is no new layer to keep.
        const auto lanes = numLanes.load();

        for (int lane = lanes - 1; lane >= 0; --lane)
        {
            if (laneSlots[(size_t) lane].load (std::memory_order_relaxed) == liveSlot)
            {
                dropLaneEntry (lane);
                break;
            }
        }

        slots[(size_t) liveSlot].dirty = 0;
        slots[(size_t) liveSlot].use = SlotUse::free;
    }

    liveSlot = -1;
}

void Looper::process (const float* inLeft, const float* inRight, float* outLeft, float* outRight, int numSamples) noexcept
{
    runCommands();

    level.setTargetValue (juce::Decibels::decibelsToGain (levelDb.load()));

    const auto current = state.load();
    const bool monoOutput = outLeft == outRight;
    const bool playsBack = current == State::playing || current == State::overdubbing;
    bool reachedEnd = false;

    // Everything that should be heard, including layers on their way out.
    std::array<Slot*, numSlots> sounding {};
    int numSounding = 0;

    if (playsBack)
        for (auto& slot : slots)
            if (slot.use == SlotUse::layer || slot.use == SlotUse::fading || slot.use == SlotUse::folding)
                sounding[(size_t) numSounding++] = &slot;

    auto* live = liveSlot >= 0 ? &slots[(size_t) liveSlot] : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        // Read the input first: it may be the same buffer as the output.
        const auto inL = inLeft[i];
        const auto inR = inRight[i];
        float playL = 0.0f, playR = 0.0f;

        if (current == State::recording && ! reachedEnd)
        {
            live->left[pos] = inL;
            live->right[pos] = inR;
            live->dirty = pos + 1;
            noteRecordedPeak (juce::jmax (std::abs (inL), std::abs (inR)));

            if (++pos >= capacity)
                reachedEnd = true;
        }
        else if (playsBack)
        {
            for (int s = 0; s < numSounding; ++s)
            {
                auto& slot = *sounding[(size_t) s];

                if (slot.use == SlotUse::fading)
                    slot.gain = juce::jmax (0.0f, slot.gain - fadeStep);

                playL += slot.left[pos] * slot.gain;
                playR += slot.right[pos] * slot.gain;
            }

            if (current == State::overdubbing)
            {
                live->left[pos] += inL;
                live->right[pos] += inR;
                overdubWrote = true;
                noteOverdubPeak (juce::jmax (std::abs (inL), std::abs (inR)));
            }

            if (++pos >= length)
                pos = 0;
        }

        const auto gain = level.getNextValue();

        if (monoOutput)
        {
            outLeft[i] += 0.5f * (playL + playR) * gain;
        }
        else
        {
            outLeft[i] += playL * gain;
            outRight[i] += playR * gain;
        }
    }

    // A removed layer has faded out, or nothing is playing it.
    for (auto& slot : slots)
        if (slot.use == SlotUse::fading && (slot.gain <= 0.0f || ! playsBack))
            retire (slot);

    // The longest loop has been reached, so it starts playing.
    if (reachedEnd)
        finishFirstRecording();

    loopLength.store (state.load() == State::recording ? pos : length);
    position.store (pos);
}

//==============================================================================
TakeRecorder::TakeRecorder() = default;

TakeRecorder::~TakeRecorder()
{
    stop();
    writerThread.stopThread (4000);
}

void TakeRecorder::prepare (double sampleRate, int maxBlockSize)
{
    deviceRate.store (sampleRate);

    if ((int) silence.size() < maxBlockSize)
    {
        // Only resized while no writer is attached, since prepare runs before audio starts.
        const juce::SpinLock::ScopedLockType lock (writerLock);
        silence.assign ((size_t) maxBlockSize, 0.0f);
    }
}

juce::String TakeRecorder::start (const juce::File& folder, const juce::String& baseName)
{
    if (recording.load())
        return "Already recording.";

    const auto rate = deviceRate.load();

    if (rate <= 0.0)
        return "No audio device is running.";

    if (! folder.createDirectory())
        return "Could not create the folder " + folder.getFullPathName();

    juce::WavAudioFormat wav;
    const auto makeWriter = [&] (const juce::File& file, int channels) -> std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter>
    {
        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();

        if (stream == nullptr)
            return {};

        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                       .withSampleRate (rate)
                                                       .withNumChannels (channels)
                                                       .withBitsPerSample (24));

        if (writer == nullptr)
            return {};

        return std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), writerThread,
                                                                          (int) rate * recorderBufferSeconds);
    };

    Take take;
    take.processed = folder.getNonexistentChildFile (baseName, ".wav");
    take.dry = folder.getNonexistentChildFile (baseName + " (dry)", ".wav");

    auto processed = makeWriter (take.processed, 2);
    auto dry = makeWriter (take.dry, 1);

    if (processed == nullptr || dry == nullptr)
    {
        processed.reset();
        dry.reset();
        take.processed.deleteFile();
        take.dry.deleteFile();
        return "Could not create a file in " + folder.getFullPathName();
    }

    if (! writerThread.isThreadRunning())
        writerThread.startThread();

    const juce::SpinLock::ScopedLockType lock (writerLock);
    processedWriter = std::move (processed);
    dryWriter = std::move (dry);
    current = take;
    takeRate = rate;
    samplesWritten.store (0);
    interrupted.store (false);
    recording.store (true);
    return {};
}

TakeRecorder::Take TakeRecorder::stop()
{
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> processed, dry;
    Take take;

    {
        const juce::SpinLock::ScopedLockType lock (writerLock);
        processed = std::move (processedWriter);
        dry = std::move (dryWriter);
        take = std::exchange (current, {});
        recording.store (false);
    }

    // Deleting the writers outside the lock lets the audio thread carry on
    // while the rest of the take is written and the files are closed.
    processed.reset();
    dry.reset();
    return take;
}

double TakeRecorder::getSecondsRecorded() const noexcept
{
    return takeRate > 0.0 ? (double) samplesWritten.load() / takeRate : 0.0;
}

void TakeRecorder::write (const float* dry, const float* left, const float* right, int numSamples) noexcept
{
    const juce::SpinLock::ScopedTryLockType lock (writerLock);

    if (! lock.isLocked() || processedWriter == nullptr)
        return;

    // A take cannot change sample rate halfway, so it stops being written.
    if (! juce::exactlyEqual (deviceRate.load(), takeRate))
    {
        interrupted.store (true);
        return;
    }

    const float* stereo[] = { left, right };
    processedWriter->write (stereo, numSamples);

    for (int offset = 0; offset < numSamples; offset += (int) silence.size())
    {
        const auto count = juce::jmin ((int) silence.size(), numSamples - offset);
        const float* mono[] = { dry != nullptr ? dry + offset : silence.data() };
        dryWriter->write (mono, count);
    }

    samplesWritten.fetch_add (numSamples);
}

//==============================================================================
void PracticeTools::prepare (double sampleRate, int maxBlockSize)
{
    metronome.prepare (sampleRate);
    looper.prepare (sampleRate);
    recorder.prepare (sampleRate, maxBlockSize);
    jamTrack.prepare (sampleRate);
}

void PracticeTools::process (const float* dry, float* left, float* right, int numSamples, bool muted) noexcept
{
    looper.process (left, right, left, right, numSamples);

    // The band comes after the looper, so loops hold only the guitar.
    jamTrack.process (left, right, numSamples, metronome.getBpm());

    // A clicking metronome lines up with the band when it starts.
    if (jamTrack.takeStartedFlag())
        metronome.restartBar();

    recorder.write (dry, left, right, numSamples);

    if (muted)
    {
        juce::FloatVectorOperations::clear (left, numSamples);

        if (right != left)
            juce::FloatVectorOperations::clear (right, numSamples);

        return;
    }

    metronome.process (left, right, numSamples);

    // The loop, the band and the click are added after the chain's limiter, so keep the
    // total inside full scale.
    juce::FloatVectorOperations::clip (left, left, -1.0f, 1.0f, numSamples);

    if (right != left)
        juce::FloatVectorOperations::clip (right, right, -1.0f, 1.0f, numSamples);
}
