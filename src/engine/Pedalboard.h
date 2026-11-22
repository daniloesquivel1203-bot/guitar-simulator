#pragma once

#include "effects/CustomPedal.h"
#include "effects/Drives.h"
#include "effects/Dynamics.h"
#include "effects/Echoes.h"
#include "effects/Equaliser.h"
#include "effects/Filters.h"
#include "effects/Modulation.h"
#include "effects/MoreDrives.h"
#include "effects/MoreDynamics.h"
#include "effects/Pitch.h"
#include "effects/Reverbs.h"
#include "effects/TimeEffects.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

/** One kind of pedal the board can hold. */
struct PedalType
{
    /** Stored in presets. It is also the start of the pedal's parameter ids. */
    const char* key;

    /** The label on the pedal's tile. */
    const char* shortName;

    /** The group the add menu lists it under, such as "Drive" or "Reverb". */
    const char* category;

    std::function<std::unique_ptr<Effect>()> create;
};

/** Every kind of pedal, grouped by category in the order the add menu lists
    them. Presets store the key, so the order can change freely. */
const std::vector<PedalType>& getPedalTypes();

/** Returns nullptr when there is no pedal of that kind, for example in a
    preset made by a newer version of the app. */
const PedalType* findPedalType (const juce::String& key);

//==============================================================================
/** One pedal on the board.

    A mono pedal placed after a stereo one has to process both ears, and each
    ear needs its own filter and envelope state. So every mono pedal has a
    hidden twin that follows its knobs and handles the right ear.
*/
class Pedal
{
public:
    explicit Pedal (const PedalType& typeToUse);

    const PedalType& type;

    Effect& getEffect() noexcept         { return *effect; }

    /** Audio thread. See Effect::setFedByGuitar(). */
    void setFedByGuitar (bool isFedByGuitar) noexcept;

    /** Audio thread. See Effect::setTempo(). */
    void setTempo (double bpm) noexcept;
    bool isStereo() const noexcept       { return stereo != nullptr; }

    /** Every parameter, starting with the on/off switch. */
    std::vector<Parameter*> getParameters();

    void prepare (double sampleRate, int maxBlockSize);
    bool isPreparedFor (double sampleRate, int maxBlockSize) const noexcept;
    void reset();

    /** Audio thread. Processes a mono signal in place. */
    void processMono (float* samples, int numSamples) noexcept;

    /** Audio thread. Processes a stereo signal in place. */
    void processStereo (float* left, float* right, int numSamples) noexcept;

private:
    std::unique_ptr<Effect> effect, twin;
    MonoEffect* mono = nullptr;
    MonoEffect* monoTwin = nullptr;
    StereoEffect* stereo = nullptr;

    std::vector<std::pair<Parameter*, Parameter*>> twinLinks;
    bool twinInUse = false;

    std::vector<float> scratch;
    double preparedRate = 0.0;
    int preparedBlockSize = 0;

    JUCE_DECLARE_NON_COPYABLE (Pedal)
};

//==============================================================================
/** The pedals on either side of the amp and cabinet, in the order the signal
    passes through them.

    The editing calls (add, remove, move, setPedals) are for the message
    thread. Each edit builds a new layout and hands it to the audio thread,
    which fades the sound out for a few milliseconds, switches to the new
    layout and fades back in, so rearranging never clicks. The audio thread
    never allocates or frees anything: replaced layouts are passed back and
    freed by the next edit.
*/
class Pedalboard
{
public:
    enum Side { beforeAmp = 0, afterAmp = 1 };

    static constexpr int maxPedalsPerSide = 8;

    using PedalList = std::vector<std::shared_ptr<Pedal>>;

    Pedalboard();
    ~Pedalboard();

    //==============================================================================
    const PedalList& getPedals (Side side) const noexcept   { return model[(size_t) side]; }

    /** Adds a new pedal, switched on, at `index` (or at the end when -1).
        Returns nullptr when that side is full. */
    std::shared_ptr<Pedal> add (Side, const PedalType&, int index = -1);

    void remove (Side, int index);

    /** `toIndex` is the position in the destination list after the pedal has
        been taken out of its old place. Returns false if nothing moved. */
    bool move (Side fromSide, int fromIndex, Side toSide, int toIndex);

    /** Replaces every pedal. Lists longer than maxPedalsPerSide are cut short. */
    void setPedals (PedalList before, PedalList after);

    /** The fixed V1 order: gate, compressor, overdrive, distortion before the
        amp; EQ, chorus, delay, reverb after it. All are switched off. */
    static void makeClassicLayout (PedalList& before, PedalList& after);

    /** Frees layouts the audio thread has finished with. */
    void collectGarbage();

    //==============================================================================
    /** Not called while process calls are running. Applies any pending edit
        straight away. */
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Audio thread, once per chunk before the processing calls. */
    void beginChunk() noexcept;

    /** Audio thread. The shared tempo the delays can follow, in beats per
        minute. Passed to every pedal before it runs. */
    void setTempo (double bpm) noexcept   { tempo = bpm; }

    /** Audio thread. Mono in, mono out. */
    void processBeforeAmp (float* mono, int numSamples) noexcept;

    /** Audio thread. `left` holds the mono signal coming from the cabinet; on
        return both `left` and `right` hold the output. */
    void processAfterAmp (float* left, float* right, int numSamples) noexcept;

    /** Audio thread. The fade used while switching layouts, one step per sample. */
    float getNextFadeGain() noexcept   { return fade.getNextValue(); }
    bool isFading() const noexcept     { return fade.isSmoothing() || fade.getTargetValue() < 1.0f; }

private:
    struct Layout
    {
        std::array<PedalList, 2> pedals;
    };

    void publish();
    void prepareNewPedals (Layout&);

    std::array<PedalList, 2> model;

    Layout* active = nullptr;
    std::atomic<Layout*> pending { nullptr };

    static constexpr int retiredCapacity = 32;
    std::array<Layout*, retiredCapacity> retired {};
    juce::AbstractFifo retiredFifo { retiredCapacity };

    std::mutex prepareMutex;
    double sampleRate = 48000.0;
    int blockSize = 512;
    double tempo = 100.0;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> fade { 1.0f };

    JUCE_DECLARE_NON_COPYABLE (Pedalboard)
};
