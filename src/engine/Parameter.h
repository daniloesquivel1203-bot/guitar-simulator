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
               float defaultToUse, float stepToUse = 0.1f, const char* unitToUse = "")
        : id (idToUse), name (nameToUse), unit (unitToUse),
          minValue (minToUse), maxValue (maxToUse), defaultValue (defaultToUse), step (stepToUse),
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

private:
    std::atomic<float> value;

    JUCE_DECLARE_NON_COPYABLE (Parameter)
};
