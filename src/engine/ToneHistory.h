#pragma once

#include <juce_core/juce_core.h>

#include <deque>

/** Undo and redo for the sound, kept as a list of complete states (the same
    text a preset is saved as).

    The window feeds it the current state a few times a second through
    observe(). A new state is only recorded once it has stayed the same for a
    moment, so turning a knob from 2 to 8 is one undo step, not a hundred.
    Message thread only.
*/
class ToneHistory
{
public:
    static constexpr int maxSteps = 100;

    /** How many unchanged observations in a row make a change count. */
    static constexpr int settleObservations = 2;

    /** Starts afresh from `state`, with nothing to undo. */
    void reset (const juce::String& state)
    {
        past.clear();
        future.clear();
        current = lastSeen = state;
        unchangedCount = 0;
    }

    /** Call regularly with the current state. Returns true when a change was
        recorded as a new undo step. */
    bool observe (const juce::String& state)
    {
        if (state != lastSeen)
        {
            lastSeen = state;
            unchangedCount = 0;
            return false;
        }

        if (state == current || ++unchangedCount < settleObservations)
            return false;

        past.push_back (current);
        current = state;
        future.clear();

        while ((int) past.size() > maxSteps)
            past.pop_front();

        return true;
    }

    bool canUndo() const noexcept   { return ! past.empty() || lastSeen != current; }
    bool canRedo() const noexcept   { return ! future.empty() && lastSeen == current; }

    /** Returns the state to go back to, or an empty string if there is none.
        A change that has not settled yet is undone first. */
    juce::String undo()
    {
        if (lastSeen != current)
        {
            // The knob was still moving: go back to where it started.
            future.clear();
            future.push_back (lastSeen);
            lastSeen = current;
            unchangedCount = 0;
            return current;
        }

        if (past.empty())
            return {};

        future.push_back (current);
        current = lastSeen = past.back();
        past.pop_back();
        unchangedCount = 0;
        return current;
    }

    /** Returns the state to go forward to, or an empty string if there is none. */
    juce::String redo()
    {
        if (! canRedo())
            return {};

        past.push_back (current);
        current = lastSeen = future.back();
        future.pop_back();
        unchangedCount = 0;
        return current;
    }

private:
    std::deque<juce::String> past, future;
    juce::String current, lastSeen;
    int unchangedCount = 0;
};
