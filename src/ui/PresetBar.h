#pragma once

#include "../engine/Presets.h"

#include <juce_gui_basics/juce_gui_basics.h>

/** The preset picker and its Save and More buttons, shown in the header. */
class PresetBar final : public juce::Component,
                        private juce::Timer
{
public:
    explicit PresetBar (PresetManager& presetsToUse);

    /** Called after a preset has been loaded, so the knobs on screen can be refreshed. */
    std::function<void()> onPresetLoaded;

    void resized() override;

private:
    enum MenuIds { saveAsId = 1, renameId, deleteId, openFolderId };

    void timerCallback() override;

    void refreshList();
    void refreshTitle();
    void choiceChanged();
    void load (const PresetManager::Entry&);
    void save();
    void saveAs();
    void saveWithName (const juce::String& name);
    void rename();
    void remove();
    void showMoreMenu();

    PresetManager& presets;
    juce::ComboBox picker;
    juce::TextButton saveButton { "Save" }, moreButton { "More" };
    std::vector<PresetManager::Entry> entries;   // picker item id minus one indexes this
    juce::String shownTitle;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};
