#include "engine/Presets.h"
#include "engine/ToneHistory.h"

#include <catch2/catch_test_macros.hpp>

/** Undo and redo, seamless state changes, and preset folders. */

namespace
{
/** Feeds the same state to the history until it settles. */
void settle (ToneHistory& history, const juce::String& state)
{
    for (int i = 0; i <= ToneHistory::settleObservations; ++i)
        history.observe (state);
}

struct TempFolder
{
    juce::File folder = juce::File::createTempFile ("presets");
    ~TempFolder()   { folder.deleteRecursively(); }
};
}

//==============================================================================
TEST_CASE ("undo history records a change only once it has settled")
{
    ToneHistory history;
    history.reset ("a");
    REQUIRE_FALSE (history.canUndo());

    // A knob moving through many values is one step.
    for (const auto* state : { "b1", "b2", "b3" })
        REQUIRE_FALSE (history.observe (state));

    settle (history, "b3");
    REQUIRE (history.canUndo());

    settle (history, "c");

    REQUIRE (history.undo() == "b3");
    REQUIRE (history.undo() == "a");
    REQUIRE (history.undo().isEmpty());
    REQUIRE_FALSE (history.canUndo());

    REQUIRE (history.redo() == "b3");
    REQUIRE (history.redo() == "c");
    REQUIRE_FALSE (history.canRedo());
}

TEST_CASE ("undo while a knob is still moving goes back to where it started")
{
    ToneHistory history;
    history.reset ("a");
    history.observe ("b");

    REQUIRE (history.canUndo());
    REQUIRE (history.undo() == "a");
    REQUIRE (history.redo() == "b");
}

TEST_CASE ("a new change after undo clears redo")
{
    ToneHistory history;
    history.reset ("a");
    settle (history, "b");
    REQUIRE (history.undo() == "a");
    REQUIRE (history.canRedo());

    // Applying the undone state reports it back, which must not count as a change.
    settle (history, "a");
    REQUIRE (history.canRedo());

    settle (history, "c");
    REQUIRE_FALSE (history.canRedo());
    REQUIRE (history.undo() == "a");
}

TEST_CASE ("undo history keeps a limited number of steps")
{
    ToneHistory history;
    history.reset ("0");

    for (int i = 1; i <= ToneHistory::maxSteps + 20; ++i)
        settle (history, juce::String (i));

    int steps = 0;

    while (history.undo().isNotEmpty())
        ++steps;

    REQUIRE (steps == ToneHistory::maxSteps);
}

//==============================================================================
TEST_CASE ("applying a state with the same pedals turns the knobs without rebuilding the board")
{
    SignalChain chain;
    const auto before = chain.getBoard().getPedals (Pedalboard::beforeAmp);

    auto* od = chain.findPedal<Overdrive>();
    od->drive.set (2.0f);
    const auto state = PresetManager::capture (chain, {});

    od->drive.set (9.0f);
    chain.getAmp().gain.set (8.0f);
    REQUIRE (PresetManager::apply (chain, state));

    REQUIRE (chain.getBoard().getPedals (Pedalboard::beforeAmp) == before);
    REQUIRE (chain.findPedal<Overdrive>() == od);
    REQUIRE (juce::exactlyEqual (od->drive.get(), 2.0f));
    REQUIRE (juce::exactlyEqual (chain.getAmp().gain.get(), 4.0f));
}

TEST_CASE ("applying a state with different pedals rebuilds the board in the saved order")
{
    SignalChain chain;
    const auto state = PresetManager::capture (chain, {});

    chain.getBoard().move (Pedalboard::beforeAmp, 0, Pedalboard::beforeAmp, 3);
    REQUIRE (juce::String (chain.getBoard().getPedals (Pedalboard::beforeAmp)[0]->type.key) == "comp");

    REQUIRE (PresetManager::apply (chain, state));
    REQUIRE (juce::String (chain.getBoard().getPedals (Pedalboard::beforeAmp)[0]->type.key) == "gate");
}

//==============================================================================
TEST_CASE ("preset names can hold one folder level")
{
    REQUIRE (PresetManager::cleanName ("Blues/Warm") == "Blues/Warm");
    REQUIRE (PresetManager::cleanName ("  Blues \\ Warm ") == "Blues/Warm");
    REQUIRE (PresetManager::cleanName ("a/b/c") == "a b/c");
    REQUIRE (PresetManager::cleanName ("../escape") == "escape");
    REQUIRE (PresetManager::cleanName ("/Lead/") == "Lead");
    REQUIRE (PresetManager::cleanName ("Plain") == "Plain");
}

TEST_CASE ("presets saved in a folder are listed, loaded, renamed and deleted")
{
    TempFolder temp;
    SignalChain chain;
    PresetManager presets (chain, temp.folder);

    chain.getAmp().gain.set (7.0f);
    REQUIRE (presets.saveUser ("Blues/Warm").isEmpty());
    REQUIRE (temp.folder.getChildFile ("Blues").getChildFile ("Warm.json").existsAsFile());

    REQUIRE (presets.saveUser ("Top level").isEmpty());
    REQUIRE (presets.getUserNames() == juce::StringArray { "Blues/Warm", "Top level" });

    chain.getAmp().gain.set (1.0f);
    REQUIRE (presets.load ({ "Blues/Warm", false }).isEmpty());
    REQUIRE (juce::exactlyEqual (chain.getAmp().gain.get(), 7.0f));

    // Moving it to another folder takes the empty old folder away.
    REQUIRE (presets.renameUser ("Blues/Warm", "Lead/Warm").isEmpty());
    REQUIRE (presets.getCurrent().name == "Lead/Warm");
    REQUIRE_FALSE (temp.folder.getChildFile ("Blues").exists());

    REQUIRE (presets.deleteUser ("Lead/Warm").isEmpty());
    REQUIRE_FALSE (temp.folder.getChildFile ("Lead").exists());
    REQUIRE (presets.getUserNames() == juce::StringArray { "Top level" });
}
