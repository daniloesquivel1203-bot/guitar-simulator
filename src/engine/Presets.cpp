#include "Presets.h"

namespace
{
const juce::Identifier nameKey { "name" };
const juce::Identifier formatKey { "format" };
const juce::Identifier parametersKey { "parameters" };
const juce::Identifier cabinetKey { "cabinet" };

const char* const formatName = "guitar-simulator-preset";
const char* const fileExtension = ".json";

struct Setting
{
    const char* id;
    float value;
};

struct FactoryPreset
{
    const char* name;
    const char* cabinet;
    std::initializer_list<Setting> settings;   // everything not listed stays at its default
};

const std::initializer_list<FactoryPreset>& getFactoryPresets()
{
    static const std::initializer_list<FactoryPreset> presets =
    {
        { "Default", "builtin:0", {} },

        { "Clean Sparkle", "builtin:0",
          { { "amp.voicing", 0 }, { "amp.gain", 3.0f }, { "amp.treble", 6.0f }, { "amp.presence", 6.0f },
            { "comp.enabled", 1 }, { "comp.threshold", -22.0f }, { "comp.ratio", 3.0f }, { "comp.makeup", 3.0f },
            { "chorus.enabled", 1 }, { "chorus.mix", 30.0f },
            { "reverb.enabled", 1 }, { "reverb.mix", 20.0f } } },

        { "Blues Breakup", "builtin:0",
          { { "amp.voicing", 0 }, { "amp.gain", 8.0f }, { "amp.mid", 6.0f },
            { "od.enabled", 1 }, { "od.drive", 3.0f }, { "od.level", 6.0f },
            { "reverb.enabled", 1 }, { "reverb.mix", 18.0f } } },

        { "Classic Crunch", "builtin:1",
          { { "amp.voicing", 1 }, { "amp.gain", 5.0f }, { "amp.mid", 6.0f }, { "amp.treble", 6.0f },
            { "reverb.enabled", 1 }, { "reverb.mix", 12.0f } } },

        { "Rock Lead", "builtin:1",
          { { "amp.voicing", 1 }, { "amp.gain", 7.0f }, { "amp.mid", 6.0f },
            { "gate.enabled", 1 }, { "gate.threshold", -55.0f },
            { "od.enabled", 1 }, { "od.drive", 4.0f }, { "od.tone", 6.0f }, { "od.level", 6.0f },
            { "delay.enabled", 1 }, { "delay.time", 380.0f }, { "delay.feedback", 30.0f }, { "delay.mix", 22.0f },
            { "reverb.enabled", 1 }, { "reverb.mix", 15.0f } } },

        { "Modern High Gain", "builtin:2",
          { { "amp.voicing", 2 }, { "amp.gain", 7.0f }, { "amp.bass", 6.0f }, { "amp.mid", 4.0f },
            { "amp.treble", 6.0f }, { "amp.presence", 6.0f },
            { "gate.enabled", 1 }, { "gate.threshold", -48.0f }, { "gate.release", 80.0f },
            { "od.enabled", 1 }, { "od.drive", 1.0f }, { "od.tone", 6.0f }, { "od.level", 8.0f } } },

        { "Fuzzy Distortion", "builtin:2",
          { { "amp.voicing", 0 }, { "amp.gain", 4.0f },
            { "gate.enabled", 1 }, { "gate.threshold", -50.0f },
            { "dist.enabled", 1 }, { "dist.gain", 7.0f }, { "dist.tone", 5.0f },
            { "reverb.enabled", 1 }, { "reverb.mix", 10.0f } } },

        { "Ambient Wash", "builtin:0",
          { { "amp.voicing", 0 }, { "amp.gain", 3.0f },
            { "comp.enabled", 1 }, { "comp.threshold", -24.0f }, { "comp.makeup", 4.0f },
            { "chorus.enabled", 1 }, { "chorus.depth", 6.0f }, { "chorus.mix", 50.0f },
            { "delay.enabled", 1 }, { "delay.time", 480.0f }, { "delay.feedback", 55.0f }, { "delay.mix", 40.0f },
            { "delay.pingPong", 1 },
            { "reverb.enabled", 1 }, { "reverb.size", 9.0f }, { "reverb.mix", 45.0f } } },

        { "American Sparkle", "builtin:7",
          { { "amp.voicing", 3 }, { "amp.gain", 4.0f }, { "amp.treble", 6.0f },
            { "comp.enabled", 1 }, { "comp.threshold", -20.0f }, { "comp.ratio", 3.0f }, { "comp.makeup", 2.0f },
            { "reverb.enabled", 1 }, { "reverb.mix", 25.0f } } },

        { "Jazz Box", "builtin:5",
          { { "amp.voicing", 4 }, { "amp.gain", 4.0f }, { "amp.bass", 6.0f }, { "amp.treble", 4.0f },
            { "reverb.enabled", 1 }, { "reverb.mix", 15.0f } } },

        { "Chime Jangle", "builtin:8",
          { { "amp.voicing", 5 }, { "amp.gain", 4.0f }, { "amp.treble", 6.0f },
            { "reverb.enabled", 1 }, { "reverb.mix", 18.0f } } },

        { "Tweed Breakup", "builtin:3",
          { { "amp.voicing", 6 }, { "amp.gain", 6.0f },
            { "reverb.enabled", 1 }, { "reverb.mix", 12.0f } } },

        { "Vintage Stack", "builtin:10",
          { { "amp.voicing", 7 }, { "amp.gain", 7.0f }, { "amp.mid", 6.0f }, { "amp.presence", 6.0f },
            { "reverb.enabled", 1 }, { "reverb.mix", 10.0f } } },

        { "Hot-Rod Lead", "builtin:10",
          { { "amp.voicing", 8 }, { "amp.gain", 6.0f }, { "amp.mid", 6.0f },
            { "gate.enabled", 1 }, { "gate.threshold", -55.0f },
            { "delay.enabled", 1 }, { "delay.time", 420.0f }, { "delay.feedback", 30.0f }, { "delay.mix", 20.0f } } },

        { "Singing Lead", "builtin:4",
          { { "amp.voicing", 9 }, { "amp.gain", 6.0f },
            { "delay.enabled", 1 }, { "delay.time", 450.0f }, { "delay.feedback", 35.0f }, { "delay.mix", 22.0f },
            { "reverb.enabled", 1 }, { "reverb.mix", 15.0f } } },

        { "Scooped Metal", "builtin:11",
          { { "amp.voicing", 10 }, { "amp.gain", 7.0f }, { "amp.bass", 6.0f },
            { "gate.enabled", 1 }, { "gate.threshold", -48.0f }, { "gate.release", 80.0f } } },

        { "Tight Djent", "builtin:11",
          { { "amp.voicing", 11 }, { "amp.gain", 7.0f },
            { "gate.enabled", 1 }, { "gate.threshold", -45.0f }, { "gate.release", 60.0f },
            { "od.enabled", 1 }, { "od.drive", 1.0f }, { "od.tone", 6.0f }, { "od.level", 8.0f } } },
    };

    return presets;
}

juce::var makeFactoryVar (const FactoryPreset& preset)
{
    auto parameters = std::make_unique<juce::DynamicObject>();

    for (const auto& setting : preset.settings)
        parameters->setProperty (setting.id, setting.value);

    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty (formatKey, formatName);
    root->setProperty (nameKey, preset.name);
    root->setProperty (parametersKey, juce::var (parameters.release()));
    root->setProperty (cabinetKey, preset.cabinet);
    return juce::var (root.release());
}
}

//==============================================================================
PresetManager::PresetManager (SignalChain& chainToUse, juce::File userFolderToUse)
    : chain (chainToUse), userFolder (std::move (userFolderToUse))
{
}

juce::var PresetManager::capture (SignalChain& chain, const juce::String& name)
{
    auto parameters = std::make_unique<juce::DynamicObject>();

    for (auto* parameter : chain.getAllParameters())
        parameters->setProperty (parameter->id, parameter->get());

    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty (formatKey, formatName);
    root->setProperty (nameKey, name);
    root->setProperty (parametersKey, juce::var (parameters.release()));
    root->setProperty (cabinetKey, chain.getCabinet().getSourceDescription());
    return juce::var (root.release());
}

bool PresetManager::apply (SignalChain& chain, const juce::var& preset)
{
    if (preset[formatKey].toString() != formatName || ! preset[parametersKey].isObject())
        return false;

    const auto& values = preset[parametersKey];

    for (auto* parameter : chain.getAllParameters())
    {
        const auto value = values[juce::Identifier (parameter->id)];
        const bool usable = (value.isDouble() || value.isInt() || value.isInt64() || value.isBool())
                            && std::isfinite ((double) value);

        parameter->set (usable ? (float) (double) value : parameter->defaultValue);
    }

    chain.getCabinet().restoreFromDescription (preset[cabinetKey].toString());
    return true;
}

juce::String PresetManager::cleanName (const juce::String& name)
{
    return juce::File::createLegalFileName (name.trim()).trim().substring (0, 60).trim();
}

//==============================================================================
juce::StringArray PresetManager::getFactoryNames() const
{
    juce::StringArray names;

    for (const auto& preset : getFactoryPresets())
        names.add (preset.name);

    return names;
}

juce::StringArray PresetManager::getUserNames() const
{
    juce::StringArray names;

    for (const auto& file : userFolder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension))
        names.add (file.getFileNameWithoutExtension());

    names.sortNatural();
    return names;
}

juce::File PresetManager::getFile (const juce::String& name) const
{
    return userFolder.getChildFile (cleanName (name) + fileExtension);
}

juce::String PresetManager::snapshot() const
{
    return juce::JSON::toString (capture (chain, {}), true);
}

void PresetManager::setCurrent (const Entry& entry)
{
    current = entry;
    loadedSnapshot = snapshot();
}

bool PresetManager::isModified() const
{
    return current.name.isNotEmpty() && snapshot() != loadedSnapshot;
}

juce::String PresetManager::load (const Entry& entry)
{
    if (entry.isFactory)
    {
        for (const auto& preset : getFactoryPresets())
        {
            if (entry.name == preset.name)
            {
                apply (chain, makeFactoryVar (preset));
                setCurrent (entry);
                return {};
            }
        }

        return "There is no factory preset called \"" + entry.name + "\".";
    }

    const auto file = getFile (entry.name);

    if (! file.existsAsFile())
        return "The preset \"" + entry.name + "\" could not be found.";

    juce::var parsed;

    if (juce::JSON::parse (file.loadFileAsString(), parsed).failed() || ! apply (chain, parsed))
        return "The preset file \"" + file.getFileName() + "\" is damaged or is not a Guitar Simulator preset.";

    setCurrent (entry);
    return {};
}

juce::String PresetManager::saveUser (const juce::String& name)
{
    const auto cleaned = cleanName (name);

    if (cleaned.isEmpty())
        return "Please give the preset a name.";

    if (! userFolder.createDirectory())
        return "The presets folder could not be created at " + userFolder.getFullPathName() + ".";

    if (! getFile (cleaned).replaceWithText (juce::JSON::toString (capture (chain, cleaned))))
        return "The preset could not be written to " + getFile (cleaned).getFullPathName() + ".";

    setCurrent ({ cleaned, false });
    return {};
}

juce::String PresetManager::renameUser (const juce::String& oldName, const juce::String& newName)
{
    const auto cleaned = cleanName (newName);
    const auto source = getFile (oldName);

    if (cleaned.isEmpty())
        return "Please give the preset a name.";

    if (! source.existsAsFile())
        return "The preset \"" + oldName + "\" could not be found.";

    if (cleaned.equalsIgnoreCase (cleanName (oldName)) && cleaned != cleanName (oldName))
    {
        // Only the letter case changed, which a file system may treat as the same file.
    }
    else if (getFile (cleaned).existsAsFile())
    {
        return cleaned == cleanName (oldName) ? juce::String()
                                              : "A preset called \"" + cleaned + "\" already exists.";
    }

    // Rewrite the file so the name stored inside it matches.
    juce::var parsed;

    if (juce::JSON::parse (source.loadFileAsString(), parsed).failed() || ! parsed.isObject())
        return "The preset file \"" + source.getFileName() + "\" is damaged.";

    parsed.getDynamicObject()->setProperty (nameKey, cleaned);

    if (! source.moveFileTo (getFile (cleaned)) || ! getFile (cleaned).replaceWithText (juce::JSON::toString (parsed)))
        return "The preset could not be renamed.";

    if (! current.isFactory && current.name == cleanName (oldName))
        current.name = cleaned;

    return {};
}

juce::String PresetManager::deleteUser (const juce::String& name)
{
    const auto file = getFile (name);

    if (! file.existsAsFile())
        return "The preset \"" + name + "\" could not be found.";

    if (! file.deleteFile())
        return "The preset file could not be deleted.";

    if (! current.isFactory && current.name == cleanName (name))
        current = {};

    return {};
}
