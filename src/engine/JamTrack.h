#pragma once

#include "JamSynths.h"

#include <atomic>
#include <memory>

/** Plays a backing band along with the guitar: drums, bass, rhythm guitar,
    piano and organ, following a chord progression in a key and a style.

    The notes are worked out on the message thread (jam::arrange) and handed
    over with setArrangement(). process() runs on the audio thread, never
    allocates or locks, and adds the band to the output. The tempo is the
    metronome's, so the click, the band and synced delays always agree. */
class JamTrack
{
public:
    static constexpr float minLevelDb = -40.0f;
    static constexpr float maxLevelDb = 6.0f;
    static constexpr int countInBeats = 4;

    JamTrack();
    ~JamTrack();

    void prepare (double sampleRate);

    // Message thread.
    /** Swaps in a new loop. If the band is playing, it carries on at the same
        place, so changing the key or a chord takes effect straight away. */
    void setArrangement (jam::Arrangement);

    /** Starts from the top of the loop, after a bar of count-in clicks if asked. */
    void start (bool countIn)   { requestedCountIn.store (countIn); request.store (Request::start); }
    void stop()                 { request.store (Request::stop); }

    bool isPlaying() const noexcept   { return playing.load(); }

    /** False when the last arrangement handed over is a song that plays once. */
    bool isLooping() const noexcept   { return looping.load(); }

    /** Beats from the start of the loop. Negative during the count-in. After a
        song that does not loop has ended, it stays at the song's length. */
    double getPositionBeats() const noexcept   { return positionBeats.load(); }
    double getLengthBeats() const noexcept     { return lengthBeats.load(); }

    void setLevelDb (jam::Instrument, float db) noexcept;
    float getLevelDb (jam::Instrument i) const noexcept   { return levelsDb[(size_t) i].load(); }
    void setOn (jam::Instrument i, bool on) noexcept      { enabled[(size_t) i].store (on); }
    bool isOn (jam::Instrument i) const noexcept          { return enabled[(size_t) i].load(); }

    void setMasterDb (float db) noexcept   { masterDb.store (juce::jlimit (minLevelDb, maxLevelDb, db)); }
    float getMasterDb() const noexcept     { return masterDb.load(); }

    /** Frees loops the audio thread has finished with. Call now and then. */
    void collectGarbage();

    // Audio thread.
    /** Adds the band to `left` and `right` (which may be the same buffer).
        `bpm` is used unless the arrangement has its own tempo map. */
    void process (float* left, float* right, int numSamples, double bpm) noexcept;

    /** True once after the band has started, so the metronome can line up with it. */
    bool takeStartedFlag() noexcept   { return justStarted.exchange (false); }

private:
    enum class Request { none, start, stop };
    static constexpr int chunk = 32;

    void applyRequests() noexcept;
    void swapArrangement() noexcept;
    void renderChunk (float* left, float* right, int numSamples, double samplesPerBeat) noexcept;
    void fireEvents (double from, double to, double samplesPerBeat) noexcept;
    void fire (const jam::NoteEvent&, double samplesPerBeat) noexcept;
    void stopAllVoices() noexcept;
    void seekTo (double beat) noexcept;
    double tempoAt (double beat, double fallback) const noexcept;

    std::atomic<jam::Arrangement*> pending { nullptr };
    std::atomic<jam::Arrangement*> retired { nullptr };
    std::unique_ptr<jam::Arrangement> current;

    std::atomic<Request> request { Request::none };
    std::atomic<bool> requestedCountIn { true };
    std::atomic<bool> playing { false };
    std::atomic<bool> justStarted { false };
    std::atomic<double> positionBeats { 0.0 };
    std::atomic<double> lengthBeats { 0.0 };
    std::atomic<bool> looping { true };

    std::array<std::atomic<float>, jam::numInstruments> levelsDb;
    std::array<std::atomic<bool>, jam::numInstruments> enabled;
    std::atomic<float> masterDb { -6.0f };
    std::array<float, jam::numInstruments> gains {};

    double sampleRate = 48000.0;
    double position = 0.0;
    size_t cursor = 0;
    bool running = false;
    bool stopping = false;
    float fade = 1.0f;

    jam::synth::DrumKit drums;
    jam::synth::Bass bass;
    jam::synth::Guitar guitar;
    jam::synth::Keys keys;
    jam::synth::Organ organ;
    juce::Reverb room;

    std::array<std::array<float, chunk>, jam::numInstruments> parts {};
    std::array<float, chunk> drumRight {}, sendLeft {}, sendRight {};
};
