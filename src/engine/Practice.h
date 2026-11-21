#pragma once

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

    The single main button cycles record, play, overdub, play, like a looper
    pedal. Commands are queued from the message thread and run at the start of
    the next audio block, so process() never locks. Memory for the longest
    loop is set aside in prepare().
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
    void pressMain()   { post (Command::main); }
    void pressStop()   { post (Command::stop); }
    void pressUndo()   { post (Command::undo); }
    void pressClear()  { post (Command::clear); }

    void setLevelDb (float db) noexcept   { levelDb.store (juce::jlimit (minLevelDb, maxLevelDb, db)); }
    float getLevelDb() const noexcept     { return levelDb.load(); }

    State getState() const noexcept        { return state.load(); }
    bool canUndo() const noexcept          { return undoAvailable.load(); }
    double getSampleRate() const noexcept  { return sampleRate; }

    /** In samples. While recording the first pass, the length so far. */
    int getLength() const noexcept         { return loopLength.load(); }
    int getPosition() const noexcept       { return position.load(); }

    // A picture of each layer, for display. The audio is still one mixed loop.
    // Lane 0 is the first recording. Once every lane is in use, the oldest
    // overdub is folded into lane 0 to make room.
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
        return layerPeaks[(size_t) lane][(size_t) bin].load (std::memory_order_relaxed);
    }

    /** Records `inLeft`/`inRight` as the loop requires and adds the loop's
        playback to `outLeft`/`outRight`. All four may alias each other. */
    void process (const float* inLeft, const float* inRight, float* outLeft, float* outRight, int numSamples) noexcept;

private:
    enum class Command { main, stop, undo, clear };

    void post (Command);
    void runCommands() noexcept;
    void handle (Command) noexcept;
    void finishFirstRecording() noexcept;
    void finishOverdub() noexcept;
    void undoLastOverdub() noexcept;

    void resetLayers() noexcept;
    void clearLane (int lane) noexcept;
    void beginOverdubLayer() noexcept;
    void noteRecordedPeak (float peak) noexcept;
    void noteOverdubPeak (float peak) noexcept;
    void stretchRecordedLayer() noexcept;

    juce::AbstractFifo commandFifo { 64 };
    std::array<Command, 64> commands {};

    std::atomic<State> state { State::empty };
    std::atomic<float> levelDb { 0.0f };
    std::atomic<int> loopLength { 0 };
    std::atomic<int> position { 0 };
    std::atomic<bool> undoAvailable { false };

    double sampleRate = 0.0;
    std::array<std::vector<float>, 2> loop, backup;
    int capacity = 0;
    int length = 0;
    int pos = 0;

    // For undo: where the current overdub began and how many samples it has
    // changed. The old samples are saved to `backup` as they are overwritten.
    int overdubStart = 0;
    int overdubCount = 0;

    // The last finished overdub, which undo can take back.
    int lastOverdubStart = 0;
    int lastOverdubCount = 0;

    // An undo in progress: the samples still to copy back from `backup`.
    int restoreStart = 0;
    int restoreCount = 0;

    // Whether undo was available before the overdub in progress, so an overdub
    // that recorded nothing can leave things as they were.
    bool undoBeforeOverdub = false;

    std::array<std::array<std::atomic<float>, layerBins>, maxLayerLanes> layerPeaks {};
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

/** The practice tools: metronome, looper and take recorder, applied to the
    output of the signal chain. The looper and the recorder hear the processed
    guitar, and the recorder also hears the loop. The metronome is only heard,
    never recorded or looped. */
class PracticeTools
{
public:
    void prepare (double sampleRate, int maxBlockSize);

    Metronome& getMetronome() noexcept       { return metronome; }
    Looper& getLooper() noexcept             { return looper; }
    TakeRecorder& getRecorder() noexcept     { return recorder; }

    /** `dry` is the raw guitar input (may be null). `left` and `right` hold the
        chain's output on entry and the final output on return. With `muted`
        nothing is heard, but the looper and recorder keep running. */
    void process (const float* dry, float* left, float* right, int numSamples, bool muted) noexcept;

private:
    Metronome metronome;
    Looper looper;
    TakeRecorder recorder;
};
