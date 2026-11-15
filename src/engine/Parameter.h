#pragma once

#include <juce_core/juce_core.h>

#include <atomic>

/** One user-adjustable value: a knob, a switch or a choice.

    The value can be read on the audio thread and written from the UI thread
    without locking. The id is stable and is what settings and presets store.
*/
class Parameter
{
public:
    Parameter (const char* idToUse, const char* nameToUse, float minToUse, float maxToUse,
               float defaultToUse, float stepToUse = 0.1f, const char* unitToUse = "",
               bool logarithmicToUse = false)
        : id (idToUse), name (nameToUse), unit (unitToUse),
          minValue (minToUse), maxValue (maxToUse), defaultValue (defaultToUse), step (stepToUse),
          logarithmic (logarithmicToUse),
          value (defaultToUse)
    {
    }

    float get() const noexcept              { return value.load (std::memory_order_relaxed); }
    bool isOn() const noexcept              { return get() >= 0.5f; }
    int getIndex() const noexcept           { return juce::roundToInt (get()); }
    void set (float newValue) noexcept      { value.store (juce::jlimit (minValue, maxValue, newValue)); }

    const char* const id;
    const char* const name;
    const char* const unit;
    const float minValue, maxValue, defaultValue, step;

    /** True for values such as frequencies and times that a knob should spread
        evenly by ratio instead of by difference. */
    const bool logarithmic;

    /** A switch is a parameter with only the values 0 and 1. */
    bool isSwitch() const noexcept   { return juce::exactlyEqual (minValue, 0.0f) && juce::exactlyEqual (maxValue, 1.0f)
                                              && juce::exactlyEqual (step, 1.0f); }

private:
    std::atomic<float> value;

    JUCE_DECLARE_NON_COPYABLE (Parameter)
};
