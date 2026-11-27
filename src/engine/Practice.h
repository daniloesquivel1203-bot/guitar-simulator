#pragma once

#include "JamTrack.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <vector>

/** A click track. The first beat of each bar can be accented with a higher click.

    process() runs on the audio thread and never allocates or locks. The
    setters may be called from any thread.
*/
class Metronome
{
public:
    static constexpr double minBpm = 30.0;
    static constexpr double maxBpm = 300.0;
    static constexpr int maxBeatsPerBar = 12;
    static constexpr float minVolumeDb = -40.0f;
    static constexpr float maxVolumeDb = 0.0f;

    void prepare (double sampleRate);

    void setRunning (bool shouldRun) noexcept       { running.store (shouldRun); }
    void setBpm (double bpm) noexcept               { tempo.store (juce::jlimit (minBpm, maxBpm, bpm)); }
    void setBeatsPerBar (int beats) noexcept        { beatsPerBar.store (juce::jlimit (1, maxBeatsPerBar, beats)); }
    void setVolumeDb (float db) noexcept            { volumeDb.store (juce::jlimit (minVolumeDb, maxVolumeDb, db)); }
    void setAccentFirstBeat (bool shouldAccent) noexcept   { accent.store (shouldAccent); }

    bool isRunning() const noexcept         { return running.load(); }
    double getBpm() const noexcept          { return tempo.load(); }
    int getBeatsPerBar() const noexcept     { return beatsPerBar.load(); }
    float getVolumeDb() const noexcept      { return volumeDb.load(); }
    bool getAccentFirstBeat() const noexcept   { return accent.load(); }

    /** If running, starts again from the first beat of the bar at the next block. */
    void restartBar() noexcept              { restartRequested.store (true); }

    /** The beat being played, from 0, or -1 when stopped. */
    int getCurrentBeat() const noexcept     { return currentBeat.load(); }

    /** Adds the clicks to `left` and `right`, which may be the same buffer. */
    void process (float* left, float* right, int numSamples) noexcept;

private:
    void startClick (bool accented) noexcept;

    std::atomic<bool> running { false };
    std::atomic<double> tempo { 100.0 };
    std::atomic<int> beatsPerBar { 4 };
    std::atomic<float> volumeDb { -12.0f };
    std::atomic<bool> accent { true };
    std::atomic<int> currentBeat { -1 };
    std::atomic<bool> restartRequested { false };

    double sampleRate = 48000.0;
    bool wasRunning = false;
    double samplesToNextBeat = 0.0;
    int beat = 0;

    // The click is a short sine burst that dies away quickly.
    double clickPhase = 0.0;
    double clickPhaseStep = 0.0;
    float clickLevel = 0.0f;
    float clickDecay = 0.0f;
    int clickSamplesLeft = 0;
};

/** Works out a tempo from taps on a button. Runs on the message thread. */
class TapTempo
{
public:
    /** Records a tap at `seconds`, from any steady clock. Returns the tempo once
        there have been at least two taps close enough together. */
    std::optional<double> tap (double seconds);

private:
    static constexpr double maxGapSeconds = 2.0;
    static constexpr int maxIntervals = 4;

    std::optional<double> lastTap;
    std::vector<double> intervals;
};

/** A looper pedal: record a phrase, then it repeats while you play over it,
    and each overdub layers on top. Records the processed sound.

    The main button records and then plays. Overdub is a button of its own, so
    a second press of Play can never start recording by accident. Commands are
    queued from the message thread and run at the start of the next audio
    block, so process() never locks. Memory for the longest loop is set aside
    in prepare().
*/
class Looper
{
public:
    enum class State { empty, recording, playing, overdubbing, stopped };

    static constexpr double maxLoopSeconds = 90.0;
    static constexpr float minLevelDb = -40.0f;
    static constexpr float maxLevelDb = 6.0f;

    /** Allocates the loop memory. A loop survives a new buffer size, but is
        cleared when the sample rate changes. Not called while process() runs. */
    void prepare (double sampleRate);

    // Message thread.
    /** Record, close the loop, end an overdub, or play again after a stop.
        Does nothing while the loop is already playing. */
    void pressMain()   { post (Command::main); }

    /** Starts or ends an overdub. While the first pass is recording, this closes
        the loop and goes straight into an overdub; after a stop, it plays from
        the start and overdubs. Does nothing until there is a loop. */
    void pressOverdub()  { post (Command::overdub); }

    void pressStop()   { post (Command::stop); }
    void pressUndo()   { post (Command::undo); }
    void pressClear()  { post (Command::clear); }

    /** Removes one layer, by its id from getLayerId(). The others keep playing.
        The last remaining layer stays; Clear is what removes that. */
    void removeLayer (int layerId)  { post (Command::removeLayer, layerId); }

    void setLevelDb (float db) noexcept   { levelDb.store (juce::jlimit (minLevelDb, maxLevelDb, db)); }
    float getLevelDb() const noexcept     { return levelDb.load(); }

    State getState() const noexcept        { return state.load(); }
    bool canUndo() const noexcept          { return undoAvailable.load(); }
    double getSampleRate() const noexcept  { return sampleRate; }

    /** In samples. While recording the first pass, the length so far. */
    int getLength() const noexcept         { return loopLength.load(); }
    int getPosition() const noexcept       { return position.load(); }

    /** The longest possible loop, in samples. */
    int getCapacity() const noexcept       { return capacityInSamples.load(); }

    // Each layer is kept separately, so any of them can be removed. Lane 0 is the
    // oldest. Once every lane is in use, the oldest overdub is folded into lane 0
    // to make room, and from then on the two are removed together.
    static constexpr int maxLayerLanes = 8;
    static constexpr int layerBins = 240;

    int getNumLayerLanes() const noexcept  { return numLanes.load (std::memory_order_acquire); }

    /** How many earlier overdubs have been folded into lane 0. */
    int getNumFoldedLayers() const noexcept  { return foldedLayers.load(); }

    /** All the layers in the loop, including any folded into lane 0. */
    int getNumLayers() const noexcept      { return getNumLayerLanes() + getNumFoldedLayers(); }

    /** How many bins of each lane hold data: all of them, except while the
        first pass is being recorded. */
    int getNumLayerBinsFilled() const noexcept  { return binsFilled.load (std::memory_order_acquire); }

    /** The loudest absolute sample in one slice of a lane. Safe from any thread. */
    float getLayerPeak (int lane, int bin) const noexcept
    {
        const auto slot = (size_t) laneSlots[(size_t) lane].load (std::memory_order_relaxed);
        return layerPeaks[slot][(size_t) bin].load (std::memory_order_relaxed);
    }

    /** Identifies the layer in a lane, and stays the same if the lanes move. */
    int getLayerId (int lane) const noexcept
    {
        return slotIds[(size_t) laneSlots[(size_t) lane].load (std::memory_order_relaxed)].load (std::memory_order_relaxed);
    }

    /** Records `inLeft`/`inRight` as the loop requires and adds the loop's
        playback to `outLeft`/`outRight`. All four may alias each other. */
    void process (const float* inLeft, const float* inRight, float* outLeft, float* outRight, int numSamples) noexcept;

private:
    enum class Command { main, overdub, stop, undo, clear, removeLayer };
    enum class SlotUse { free, layer, fading, folding, clearing };

    struct Message
    {
        Command command = Command::main;
        int layerId = 0;
    };

    /** The audio of one layer. */
    struct Slot
    {
        juce::HeapBlock<float> memory;
        float* left = nullptr;
        float* right = nullptr;
        SlotUse use = SlotUse::free;
        float gain = 1.0f;
        int dirty = 0;     // how many samples have been written
        int cleared = 0;   // how many of those have been zeroed again
    };

    // One more than the lanes, so a layer can fold away while a new one records.
    static constexpr int numSlots = maxLayerLanes + 1;

    void post (Command, int layerId = 0);
    void runCommands() noexcept;
    void doBackgroundWork() noexcept;
    void handle (const Message&) noexcept;
    bool needsFreeSlot (const Message&) const noexcept;
    int findFreeSlot() const noexcept;
    int claimSlot() noexcept;
    void retire (Slot&) noexcept;
    void appendLane (int slotIndex) noexcept;
    void dropLaneEntry (int lane) noexcept;
    void removeLaneAt (int lane) noexcept;
    void removeLayerById (int layerId) noexcept;
    void beginOverdub() noexcept;
    void finishFirstRecording() noexcept;
    void finishOverdub() noexcept;
    void resetLayerDisplay() noexcept;
    void noteRecordedPeak (float peak) noexcept;
    void noteOverdubPeak (float peak) noexcept;
    void stretchRecordedLayer() noexcept;

    juce::AbstractFifo commandFifo { 64 };
    std::array<Message, 64> commands {};
    std::optional<Message> heldCommand;

    std::atomic<State> state { State::empty };
    std::atomic<float> levelDb { 0.0f };
    std::atomic<int> loopLength { 0 };
    std::atomic<int> position { 0 };
    std::atomic<int> capacityInSamples { 0 };
    std::atomic<bool> undoAvailable { false };

    double sampleRate = 0.0;
    std::array<Slot, numSlots> slots;
    int capacity = 0;
    int length = 0;
    int pos = 0;
    float fadeStep = 1.0f;

    int liveSlot = -1;          // the layer being recorded
    bool overdubWrote = false;
    int nextLayerId = 1;

    // A layer being moved into another, a piece per block.
    int foldSource = -1;
    int foldTarget = -1;
    int foldDone = 0;

    std::array<std::array<std::atomic<float>, layerBins>, numSlots> layerPeaks {};
    std::array<std::atomic<int>, numSlots> slotIds {};
    std::array<std::atomic<int>, maxLayerLanes> laneSlots {};
    std::atomic<int> numLanes { 0 };
    std::atomic<int> foldedLayers { 0 };
    std::atomic<int> binsFilled { 0 };

    // The first pass has no known length yet, so each bin starts small and
    // doubles in width whenever the bins run out.
    int samplesPerRecordedBin = 1;

    juce::SmoothedValue<float> level { 1.0f };
};

/** Records takes to WAV files: the sound you hear, and the guitar exactly as it
    came in (the dry take), so it can be run through a different sound later.

    start() and stop() run on the message thread. The audio thread hands the
    samples to a background thread that writes the files.
*/
class TakeRecorder
{
public:
    TakeRecorder();
    ~TakeRecorder();

    void prepare (double sampleRate, int maxBlockSize);

    struct Take
    {
        juce::File processed, dry;
    };

    /** Starts writing two new files in `folder`. Returns an error message, or
        an empty string on success. */
    juce::String start (const juce::File& folder, const juce::String& baseName);

    /** Finishes the files and returns them. */
    Take stop();

    bool isRecording() const noexcept   { return recording.load(); }
    double getSecondsRecorded() const noexcept;

    /** True if the take stopped being written because the sample rate changed. */
    bool wasInterrupted() const noexcept   { return interrupted.load(); }

    /** Audio thread. `dry` may be null for silence; `left` and `right` may be the same. */
    void write (const float* dry, const float* left, const float* right, int numSamples) noexcept;

private:
    juce::TimeSliceThread writerThread { "Take writer" };
    juce::SpinLock writerLock;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> processedWriter, dryWriter;
    Take current;

    std::atomic<bool> recording { false };
    std::atomic<bool> interrupted { false };
    std::atomic<juce::int64> samplesWritten { 0 };
    std::atomic<double> deviceRate { 0.0 };
    double takeRate = 0.0;
    std::vector<float> silence = std::vector<float> (512, 0.0f);
};

/** The practice tools: metronome, looper, jam track and take recorder, applied
    to the output of the signal chain. The looper hears only the processed
    guitar. The recorder hears that, the loop and the jam track. The metronome
    is only heard, never recorded or looped. */
class PracticeTools
{
public:
    void prepare (double sampleRate, int maxBlockSize);

    Metronome& getMetronome() noexcept       { return metronome; }
    Looper& getLooper() noexcept             { return looper; }
    TakeRecorder& getRecorder() noexcept     { return recorder; }
    JamTrack& getJamTrack() noexcept         { return jamTrack; }

    /** `dry` is the raw guitar input (may be null). `left` and `right` hold the
        chain's output on entry and the final output on return. With `muted`
        nothing is heard, but the looper and recorder keep running. */
    void process (const float* dry, float* left, float* right, int numSamples, bool muted) noexcept;

private:
    Metronome metronome;
    Looper looper;
    TakeRecorder recorder;
    JamTrack jamTrack;
};
