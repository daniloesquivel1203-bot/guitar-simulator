#pragma once

#include "../Parameter.h"

#include <vector>

/** Common parts of every pedal: a name, an on/off switch and a list of controls. */
class Effect
{
public:
    Effect (const char* nameToUse, const char* enabledId)
        : name (nameToUse), enabled (enabledId, "On", 0.0f, 1.0f, 0.0f, 1.0f)
    {
    }

    virtual ~Effect() = default;

    /** The knobs and switches to show, not including the on/off switch. */
    virtual std::vector<Parameter*> getControls() = 0;

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;

    const char* const name;
    Parameter enabled;

    /** Audio thread. Tells the pedal whether it hears the guitar's pickups
        directly, with nothing switched on in front of it. Some old fuzz and
        booster circuits load the pickups and sound warmer and fuller that way,
        and thinner and harsher behind another pedal, as real ones do. */
    void setFedByGuitar (bool isFedByGuitar) noexcept   { fedByGuitar = isFedByGuitar; }

    /** Audio thread. The shared tempo, in beats per minute, which is the
        metronome's. Delays can follow it; other pedals ignore it. */
    void setTempo (double bpm) noexcept   { tempoBpm = bpm; }

    /** For pedals with a time that tap tempo can set (the delays): the Time
        knob. Null for every other pedal. */
    virtual Parameter* getTapTimeParameter() noexcept   { return nullptr; }

    /** The Sync choice that goes with getTapTimeParameter(). See TempoSync.h. */
    virtual Parameter* getTempoSyncParameter() noexcept   { return nullptr; }

protected:
    bool isFedByGuitar() const noexcept   { return fedByGuitar; }
    double getTempo() const noexcept      { return tempoBpm; }

protected:
    /** Call at the top of process(). Returns false when the pedal is off, and
        clears stale state when it has just been switched on. */
    bool shouldProcess() noexcept
    {
        if (! enabled.isOn())
        {
            wasOn = false;
            return false;
        }

        if (! wasOn)
        {
            wasOn = true;
            reset();
        }

        return true;
    }

private:
    bool wasOn = false;
    bool fedByGuitar = false;
    double tempoBpm = 100.0;
};

/** A pedal placed before the amp or cabinet, where the signal is mono. */
class MonoEffect : public Effect
{
public:
    using Effect::Effect;

    /** In place. numSamples must not exceed the block size given to prepare(). */
    virtual void process (float* samples, int numSamples) noexcept = 0;
};

/** A pedal placed after the cabinet, where the signal is stereo. */
class StereoEffect : public Effect
{
public:
    using Effect::Effect;

    /** In place. `left` and `right` must be different buffers. */
    virtual void process (float* left, float* right, int numSamples) noexcept = 0;
};
