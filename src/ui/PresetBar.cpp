#include "PresetBar.h"

#include "Dialogs.h"

PresetBar::PresetBar (PresetManager& presetsToUse)
    : presets (presetsToUse)
{
    picker.setTextWhenNothingSelected ("Choose a preset");
    picker.onChange = [this] { choiceChanged(); };
    addAndMakeVisible (picker);

    saveButton.setTooltip ("Save the current sound. Factory presets are saved as a new preset of your own.");
    saveButton.onClick = [this] { save(); };
    addAndMakeVisible (saveButton);

    moreButton.setTooltip ("Save as, rename, delete, or open the presets folder.");
    moreButton.onClick = [this] { showMoreMenu(); };
    addAndMakeVisible (moreButton);

    refreshList();
    startTimerHz (4);
}

void PresetBar::resized()
{
    auto area = getLocalBounds();
    moreButton.setBounds (area.removeFromRight (56));
    area.removeFromRight (6);
    saveButton.setBounds (area.removeFromRight (56));
    area.removeFromRight (6);
    picker.setBounds (area);
}

void PresetBar::timerCallback()
{
    // Shows a star next to the name once a knob has been moved.
    refreshTitle();
}

void PresetBar::refreshList()
{
    entries.clear();
    picker.clear (juce::dontSendNotification);

    picker.addSectionHeading ("Factory presets");

    for (const auto& name : presets.getFactoryNames())
    {
        entries.push_back ({ name, true });
        picker.addItem (name, (int) entries.size());
    }

    const auto userNames = presets.getUserNames();

    if (! userNames.isEmpty())
    {
        picker.addSeparator();
        picker.addSectionHeading ("Your presets");

        for (const auto& name : userNames)
        {
            entries.push_back ({ name, false });
            picker.addItem (name, (int) entries.size());
        }
    }

    shownTitle.clear();
    refreshTitle();
}

void PresetBar::refreshTitle()
{
    const auto current = presets.getCurrent();
    const auto title = current.name.isEmpty() ? juce::String()
                                              : current.name + (presets.isModified() ? " *" : "");

    if (title == shownTitle)
        return;

    shownTitle = title;

    if (title.isEmpty())
        picker.setSelectedId (0, juce::dontSendNotification);
    else
        picker.setText (title, juce::dontSendNotification);
}

void PresetBar::choiceChanged()
{
    const auto index = picker.getSelectedId() - 1;

    if (juce::isPositiveAndBelow (index, (int) entries.size()))
        load (entries[(size_t) index]);
}

void PresetBar::load (const PresetManager::Entry& entry)
{
    const auto error = presets.load (entry);

    if (error.isNotEmpty())
        dialogs::showMessage ("Preset not loaded", error);
    else if (onPresetLoaded != nullptr)
        onPresetLoaded();

    refreshList();
}

void PresetBar::save()
{
    const auto current = presets.getCurrent();

    // Factory presets cannot be overwritten, so saving one makes a copy.
    if (current.name.isEmpty() || current.isFactory)
        saveAs();
    else
        saveWithName (current.name);
}

void PresetBar::saveAs()
{
    const auto current = presets.getCurrent();
    const auto suggestion = current.name.isEmpty() ? juce::String ("My Preset")
                                                   : current.isFactory ? "My " + current.name : current.name;

    juce::Component::SafePointer<PresetBar> safeThis (this);

    dialogs::askForText ("Save preset", "Name for this preset:", suggestion, [safeThis] (juce::String name)
    {
        if (safeThis == nullptr)
            return;

        const auto cleaned = PresetManager::cleanName (name);

        if (cleaned.isNotEmpty() && safeThis->presets.userPresetExists (cleaned))
        {
            dialogs::confirm ("Replace preset?", "A preset called \"" + cleaned + "\" already exists. Replace it?",
                              "Replace", [safeThis, cleaned]
                              {
                                  if (safeThis != nullptr)
                                      safeThis->saveWithName (cleaned);
                              });
            return;
        }

        safeThis->saveWithName (name);
    });
}

void PresetBar::saveWithName (const juce::String& name)
{
    const auto error = presets.saveUser (name);

    if (error.isNotEmpty())
        dialogs::showMessage ("Preset not saved", error);

    refreshList();
}

void PresetBar::rename()
{
    const auto current = presets.getCurrent();
    juce::Component::SafePointer<PresetBar> safeThis (this);

    dialogs::askForText ("Rename preset", "New name for \"" + current.name + "\":", current.name,
                         [safeThis, oldName = current.name] (juce::String newName)
                         {
                             if (safeThis == nullptr)
                                 return;

                             const auto error = safeThis->presets.renameUser (oldName, newName);

                             if (error.isNotEmpty())
                                 dialogs::showMessage ("Preset not renamed", error);

                             safeThis->refreshList();
                         });
}

void PresetBar::remove()
{
    const auto current = presets.getCurrent();
    juce::Component::SafePointer<PresetBar> safeThis (this);

    dialogs::confirm ("Delete preset?", "Delete your preset \"" + current.name + "\"? This cannot be undone.",
                      "Delete", [safeThis, name = current.name]
                      {
                          if (safeThis == nullptr)
                              return;

                          const auto error = safeThis->presets.deleteUser (name);

                          if (error.isNotEmpty())
                              dialogs::showMessage ("Preset not deleted", error);

                          safeThis->refreshList();
                      });
}

void PresetBar::showMoreMenu()
{
    const auto current = presets.getCurrent();
    const bool isUserPreset = current.name.isNotEmpty() && ! current.isFactory;

    juce::PopupMenu menu;
    menu.addItem (saveAsId, "Save as...");
    menu.addItem (renameId, "Rename...", isUserPreset);
    menu.addItem (deleteId, "Delete", isUserPreset);
    menu.addSeparator();
    menu.addItem (openFolderId, "Open presets folder");

    juce::Component::SafePointer<PresetBar> safeThis (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (moreButton), [safeThis] (int result)
    {
        if (safeThis == nullptr)
            return;

        switch (result)
        {
            case saveAsId:      safeThis->saveAs(); break;
            case renameId:      safeThis->rename(); break;
            case deleteId:      safeThis->remove(); break;
            case openFolderId:  safeThis->presets.getUserFolder().createDirectory();
                                safeThis->presets.getUserFolder().revealToUser(); break;
            default:            break;
        }
    });
}
