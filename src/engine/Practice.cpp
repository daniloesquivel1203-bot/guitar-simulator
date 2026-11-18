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

// Undoing an overdub copies the old samples back a piece at a time, so a long
// loop never makes one audio block late.
constexpr int undoSamplesPerBlock = 65536;

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

    for (auto* buffers : { &loop, &backup })
        for (auto& channel : *buffers)
            channel.assign ((size_t) capacity, 0.0f);

    length = pos = 0;
    lastOverdubCount = restoreCount = 0;
    state.store (State::empty);
    loopLength.store (0);
    position.store (0);
    undoAvailable.store (false);
}

void Looper::post (Command command)
{
    const auto scope = commandFifo.write (1);

    if (scope.blockSize1 > 0)
        commands[(size_t) scope.startIndex1] = command;
    else if (scope.blockSize2 > 0)
        commands[(size_t) scope.startIndex2] = command;
}

void Looper::runCommands() noexcept
{
    // Finish restoring an undone overdub before taking new commands.
    if (restoreCount > 0)
    {
        const auto todo = juce::jmin (restoreCount, undoSamplesPerBlock);

        for (int k = 0; k < todo; ++k)
        {
            const auto p = (size_t) ((restoreStart + k) % length);
            loop[0][p] = backup[0][p];
            loop[1][p] = backup[1][p];
        }

        restoreStart = (restoreStart + todo) % length;
        restoreCount -= todo;
        return;
    }

    while (commandFifo.getNumReady() > 0)
    {
        Command command {};
        const auto scope = commandFifo.read (1);

        if (scope.blockSize1 > 0)
            command = commands[(size_t) scope.startIndex1];
        else if (scope.blockSize2 > 0)
            command = commands[(size_t) scope.startIndex2];
        else
            break;

        handle (command);

        if (restoreCount > 0)
            return;
    }
}

void Looper::handle (Command command) noexcept
{
    if (capacity == 0)
        return;

    auto current = state.load();

    switch (command)
    {
        case Command::main:
            if (current == State::empty)
            {
                pos = length = 0;
                undoAvailable.store (false);
                state.store (State::recording);
            }
            else if (current == State::recording)
            {
                finishFirstRecording();
            }
            else if (current == State::playing)
            {
                overdubStart = pos;
                overdubCount = 0;
                undoAvailable.store (true);
                state.store (State::overdubbing);
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

            if (undoAvailable.exchange (false))
                undoLastOverdub();
            break;

        case Command::clear:
            pos = length = 0;
            lastOverdubCount = restoreCount = 0;
            undoAvailable.store (false);
            state.store (State::empty);
            break;
    }
}

void Looper::finishFirstRecording() noexcept
{
    length = pos;
    pos = 0;

    if (length < (int) (minLoopSeconds * sampleRate))
    {
        // Too short to be a loop, most likely a double press.
        length = 0;
        state.store (State::empty);
        return;
    }

    // Fade the ends a little so the seam does not click.
    const auto fade = juce::jmin (length / 4, (int) (loopEdgeFadeSeconds * sampleRate));

    for (int k = 0; k < fade; ++k)
    {
        const auto gain = (float) k / (float) fade;

        for (auto& channel : loop)
        {
            channel[(size_t) k] *= gain;
            channel[(size_t) (length - 1 - k)] *= gain;
        }
    }

    state.store (State::playing);
}

void Looper::finishOverdub() noexcept
{
    // The samples that this overdub changed, which undo puts back.
    lastOverdubStart = overdubStart;
    lastOverdubCount = overdubCount;
}

void Looper::undoLastOverdub() noexcept
{
    restoreStart = lastOverdubStart;
    restoreCount = lastOverdubCount;
    lastOverdubCount = 0;
}

void Looper::process (const float* inLeft, const float* inRight, float* outLeft, float* outRight, int numSamples) noexcept
{
    runCommands();

    level.setTargetValue (juce::Decibels::decibelsToGain (levelDb.load()));

    const auto current = state.load();
    const bool monoOutput = outLeft == outRight;
    bool reachedEnd = false;

    for (int i = 0; i < numSamples; ++i)
    {
        // Read the input first: it may be the same buffer as the output.
        const auto inL = inLeft[i];
        const auto inR = inRight[i];
        float playL = 0.0f, playR = 0.0f;

        if (current == State::recording && ! reachedEnd)
        {
            loop[0][(size_t) pos] = inL;
            loop[1][(size_t) pos] = inR;

            if (++pos >= capacity)
                reachedEnd = true;
        }
        else if (current == State::playing)
        {
            playL = loop[0][(size_t) pos];
            playR = loop[1][(size_t) pos];

            if (++pos >= length)
                pos = 0;
        }
        else if (current == State::overdubbing)
        {
            auto& sampleL = loop[0][(size_t) pos];
            auto& sampleR = loop[1][(size_t) pos];
            playL = sampleL;
            playR = sampleR;

            // Save each sample before its first change, for undo.
            if (overdubCount < length)
            {
                backup[0][(size_t) pos] = sampleL;
                backup[1][(size_t) pos] = sampleR;
                ++overdubCount;
            }

            sampleL += inL;
            sampleR += inR;

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
}

void PracticeTools::process (const float* dry, float* left, float* right, int numSamples, bool muted) noexcept
{
    looper.process (left, right, left, right, numSamples);
    recorder.write (dry, left, right, numSamples);

    if (muted)
    {
        juce::FloatVectorOperations::clear (left, numSamples);

        if (right != left)
            juce::FloatVectorOperations::clear (right, numSamples);

        return;
    }

    metronome.process (left, right, numSamples);

    // The loop and the click are added after the chain's limiter, so keep the
    // total inside full scale.
    juce::FloatVectorOperations::clip (left, left, -1.0f, 1.0f, numSamples);

    if (right != left)
        juce::FloatVectorOperations::clip (right, right, -1.0f, 1.0f, numSamples);
}
