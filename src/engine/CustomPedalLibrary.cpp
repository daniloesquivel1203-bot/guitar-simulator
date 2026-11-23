#include "CustomPedalLibrary.h"

namespace
{
constexpr const char* formatKey = "format";
constexpr const char* formatName = "GuitarSimulatorCustomPedal";
constexpr const char* nameKey = "name";
constexpr const char* parametersKey = "parameters";
constexpr const char* extension = ".gspedal";
}

juce::var CustomPedalLibrary::captureSettings (CustomPedal& pedal)
{
    auto values = std::make_unique<juce::DynamicObject>();

    // The on/off switch is left out: a saved pedal is always added switched on.
    for (auto* parameter : pedal.getControls())
        values->setProperty (parameter->id, parameter->get());

    return juce::var (values.release());
}

void CustomPedalLibrary::applySettings (CustomPedal& pedal, const juce::var& settings)
{
    for (auto* parameter : pedal.getControls())
    {
        const auto& value = settings[parameter->id];
        parameter->set (value.isVoid() ? parameter->defaultValue : (float) value);
    }
}

juce::File CustomPedalLibrary::getFile (const juce::String& name) const
{
    return folder.getChildFile (juce::File::createLegalFileName (name.trim()) + extension);
}

juce::StringArray CustomPedalLibrary::getNames() const
{
    juce::StringArray names;

    for (const auto& file : folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + extension))
    {
        const auto parsed = juce::JSON::parse (file);

        if (parsed[formatKey].toString() == formatName)
            names.add (parsed[nameKey].toString().isNotEmpty() ? parsed[nameKey].toString() : file.getFileNameWithoutExtension());
    }

    names.sortNatural();
    return names;
}

juce::String CustomPedalLibrary::save (const juce::String& name, CustomPedal& pedal)
{
    const auto cleanName = name.trim().substring (0, 40).trim();

    if (juce::File::createLegalFileName (cleanName).isEmpty())
        return "Please give the pedal a name.";

    if (! folder.createDirectory())
        return "Could not create the folder " + folder.getFullPathName() + ".";

    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty (formatKey, formatName);
    root->setProperty (nameKey, cleanName);
    root->setProperty (parametersKey, captureSettings (pedal));

    if (! getFile (cleanName).replaceWithText (juce::JSON::toString (juce::var (root.release()))))
        return "Could not write " + getFile (cleanName).getFullPathName() + ".";

    pedal.label = cleanName;
    return {};
}

bool CustomPedalLibrary::load (const juce::String& name, CustomPedal& pedal) const
{
    const auto parsed = juce::JSON::parse (getFile (name));

    if (parsed[formatKey].toString() != formatName || ! parsed[parametersKey].isObject())
        return false;

    applySettings (pedal, parsed[parametersKey]);
    pedal.label = parsed[nameKey].toString().isNotEmpty() ? parsed[nameKey].toString() : name;
    return true;
}

juce::String CustomPedalLibrary::remove (const juce::String& name)
{
    const auto file = getFile (name);
    return ! file.existsAsFile() || file.deleteFile() ? juce::String() : "Could not delete " + file.getFullPathName() + ".";
}
