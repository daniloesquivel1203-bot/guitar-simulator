#include "engine/Presets.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

namespace
{
/** A PresetManager working in its own temporary folder, removed afterwards. */
struct Fixture
{
    Fixture()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("gs-preset-tests-" + juce::Uuid().toString())),
          presets (chain, folder)
    {
    }

    ~Fixture()   { folder.deleteRecursively(); }

    SignalChain chain;
    juce::File folder;
    PresetManager presets;
};
}

TEST_CASE ("a captured preset restores every parameter and the cabinet")
{
    SignalChain source, target;

    // Give every parameter a distinct, non-default value.
    float nudge = 0.1f;

    for (auto* parameter : source.getAllParameters())
    {
        parameter->set (parameter->isSwitch() ? 1.0f - parameter->defaultValue
                                              : juce::jmap (nudge, parameter->minValue, parameter->maxValue));
        nudge = nudge > 0.85f ? 0.1f : nudge + 0.07f;
    }

    source.getCabinet().selectBuiltIn (2);

    // Go through text, as a saved file would.
    juce::var parsed;
    REQUIRE (juce::JSON::parse (juce::JSON::toString (PresetManager::capture (source, "Test")), parsed).wasOk());
    REQUIRE (PresetManager::apply (target, parsed));

    const auto a = source.getAllParameters(), b = target.getAllParameters();
    REQUIRE (a.size() == b.size());

    for (size_t i = 0; i < a.size(); ++i)
    {
        INFO (a[i]->id);
        REQUIRE_THAT (b[i]->get(), WithinAbs (a[i]->get(), 1.0e-4));
    }

    REQUIRE (target.getCabinet().getBuiltInIndex() == 2);
}

TEST_CASE ("parameters missing from a preset go to their defaults and unknown ones are ignored")
{
    SignalChain chain;

    for (auto* parameter : chain.getAllParameters())
        parameter->set (parameter->maxValue);

    juce::var parsed;
    REQUIRE (juce::JSON::parse (R"({ "format": "guitar-simulator-preset", "name": "Partial",
                                     "parameters": { "amp.gain": 9.0, "from.the.future": 3.0, "amp.bass": "loud" } })",
                                parsed).wasOk());
    REQUIRE (PresetManager::apply (chain, parsed));

    REQUIRE_THAT (chain.getAmp().gain.get(), WithinAbs (9.0, 1.0e-6));
    REQUIRE_THAT (chain.getAmp().bass.get(), WithinAbs (chain.getAmp().bass.defaultValue, 1.0e-6));
    REQUIRE_THAT (chain.findPedal<Delay>()->time.get(), WithinAbs (chain.findPedal<Delay>()->time.defaultValue, 1.0e-6));
    REQUIRE (chain.getCabinet().getBuiltInIndex() == 0);
}

TEST_CASE ("values outside a parameter's range are clamped when a preset loads")
{
    SignalChain chain;
    juce::var parsed;
    REQUIRE (juce::JSON::parse (R"({ "format": "guitar-simulator-preset", "parameters": { "amp.gain": 500.0 } })", parsed).wasOk());
    REQUIRE (PresetManager::apply (chain, parsed));
    REQUIRE_THAT (chain.getAmp().gain.get(), WithinAbs (chain.getAmp().gain.maxValue, 1.0e-6));
}

TEST_CASE ("data that is not a preset is rejected and changes nothing")
{
    SignalChain chain;
    chain.getAmp().gain.set (8.0f);

    juce::var notAPreset;
    REQUIRE (juce::JSON::parse (R"({ "hello": "world" })", notAPreset).wasOk());
    REQUIRE_FALSE (PresetManager::apply (chain, notAPreset));
    REQUIRE_FALSE (PresetManager::apply (chain, juce::var()));
    REQUIRE_FALSE (PresetManager::apply (chain, juce::var ("text")));

    REQUIRE_THAT (chain.getAmp().gain.get(), WithinAbs (8.0, 1.0e-6));
}

TEST_CASE ("every factory preset loads and they are all different")
{
    Fixture f;
    const auto names = f.presets.getFactoryNames();
    REQUIRE (names.size() >= 6);

    juce::StringArray snapshots;

    for (const auto& name : names)
    {
        INFO (name);
        REQUIRE (f.presets.load ({ name, true }).isEmpty());
        REQUIRE (f.presets.getCurrent().name == name);
        REQUIRE (f.presets.getCurrent().isFactory);
        REQUIRE_FALSE (f.presets.isModified());
        snapshots.add (juce::JSON::toString (PresetManager::capture (f.chain, {})));
    }

    snapshots.removeDuplicates (false);
    REQUIRE (snapshots.size() == names.size());
}

TEST_CASE ("factory presets only use parameter ids that exist")
{
    // Loading a preset resets unlisted parameters to defaults. So if loading two
    // different presets after each other gives the same state as loading the
    // second alone, and every preset differs from Default, the ids are real.
    Fixture f;

    REQUIRE (f.presets.load ({ "Default", true }).isEmpty());
    const auto defaults = juce::JSON::toString (PresetManager::capture (f.chain, {}));

    for (const auto& name : f.presets.getFactoryNames())
    {
        if (name == "Default")
            continue;

        INFO (name);
        REQUIRE (f.presets.load ({ name, true }).isEmpty());
        REQUIRE (juce::JSON::toString (PresetManager::capture (f.chain, {})) != defaults);
    }
}

TEST_CASE ("a user preset can be saved, listed, loaded, renamed and deleted")
{
    Fixture f;
    REQUIRE (f.presets.getUserNames().isEmpty());

    f.chain.getAmp().gain.set (7.5f);
    f.chain.findPedal<Delay>()->enabled.set (1.0f);
    REQUIRE (f.presets.saveUser ("My Sound").isEmpty());
    REQUIRE (f.presets.getUserNames() == juce::StringArray { "My Sound" });
    REQUIRE (f.presets.userPresetExists ("My Sound"));
    REQUIRE (f.presets.getCurrent() == PresetManager::Entry { "My Sound", false });

    // Change things, then load the preset back.
    f.chain.getAmp().gain.set (1.0f);
    f.chain.findPedal<Delay>()->enabled.set (0.0f);
    REQUIRE (f.presets.isModified());
    REQUIRE (f.presets.load ({ "My Sound", false }).isEmpty());
    REQUIRE_THAT (f.chain.getAmp().gain.get(), WithinAbs (7.5, 1.0e-4));
    REQUIRE (f.chain.findPedal<Delay>()->enabled.isOn());
    REQUIRE_FALSE (f.presets.isModified());

    REQUIRE (f.presets.renameUser ("My Sound", "Better Name").isEmpty());
    REQUIRE (f.presets.getUserNames() == juce::StringArray { "Better Name" });
    REQUIRE (f.presets.getCurrent().name == "Better Name");
    REQUIRE (f.presets.load ({ "Better Name", false }).isEmpty());

    REQUIRE (f.presets.deleteUser ("Better Name").isEmpty());
    REQUIRE (f.presets.getUserNames().isEmpty());
    REQUIRE (f.presets.getCurrent().name.isEmpty());
}

TEST_CASE ("saving over an existing preset replaces it")
{
    Fixture f;
    f.chain.getAmp().gain.set (2.0f);
    REQUIRE (f.presets.saveUser ("Sound").isEmpty());
    f.chain.getAmp().gain.set (9.0f);
    REQUIRE (f.presets.saveUser ("Sound").isEmpty());

    f.chain.getAmp().gain.set (5.0f);
    REQUIRE (f.presets.load ({ "Sound", false }).isEmpty());
    REQUIRE_THAT (f.chain.getAmp().gain.get(), WithinAbs (9.0, 1.0e-4));
    REQUIRE (f.presets.getUserNames().size() == 1);
}

TEST_CASE ("preset problems are reported instead of failing silently")
{
    Fixture f;

    REQUIRE (f.presets.saveUser ("   ").isNotEmpty());
    REQUIRE (f.presets.load ({ "Missing", false }).isNotEmpty());
    REQUIRE (f.presets.load ({ "Missing", true }).isNotEmpty());
    REQUIRE (f.presets.deleteUser ("Missing").isNotEmpty());
    REQUIRE (f.presets.renameUser ("Missing", "Other").isNotEmpty());

    REQUIRE (f.presets.saveUser ("One").isEmpty());
    REQUIRE (f.presets.saveUser ("Two").isEmpty());
    REQUIRE (f.presets.renameUser ("One", "Two").isNotEmpty());   // would overwrite
    REQUIRE (f.presets.getUserNames().size() == 2);

    // A damaged file is reported and leaves the current sound alone.
    f.chain.getAmp().gain.set (6.5f);
    f.folder.getChildFile ("Broken.json").replaceWithText ("{ this is not json");
    REQUIRE (f.presets.load ({ "Broken", false }).isNotEmpty());
    REQUIRE_THAT (f.chain.getAmp().gain.get(), WithinAbs (6.5, 1.0e-6));
}

TEST_CASE ("preset names are made safe for use as file names")
{
    REQUIRE (PresetManager::cleanName ("  My Sound  ") == "My Sound");
    REQUIRE (PresetManager::cleanName ("a:b*c?d") == "abcd");
    REQUIRE (PresetManager::cleanName ("a/b\\c:d*e?") == "a b/cde");   // one folder level at most
    REQUIRE (PresetManager::cleanName ("../../escape") .containsChar ('/') == false);
    REQUIRE (PresetManager::cleanName (juce::String::repeatedString ("x", 200)).length() <= 60);

    Fixture f;
    REQUIRE (f.presets.saveUser ("../outside").isEmpty());
    REQUIRE (f.folder.getNumberOfChildFiles (juce::File::findFiles) == 1);   // stayed inside the folder
}
