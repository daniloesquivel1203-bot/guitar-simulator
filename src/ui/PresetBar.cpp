#include "PresetBar.h"

#include "Dialogs.h"
#include "Theme.h"

#include <map>

namespace
{
/** A search box with the matching presets below it. Enter or a double-click
    loads the highlighted one. */
class PresetFinder final : public juce::Component,
                           private juce::ListBoxModel,
                           private juce::TextEditor::Listener,
                           private juce::KeyListener
{
public:
    PresetFinder (std::vector<PresetManager::Entry> allToUse, std::function<void (const PresetManager::Entry&)> onChosenToUse)
        : all (std::move (allToUse)), onChosen (std::move (onChosenToUse))
    {
        search.setTextToShowWhenEmpty ("Type to search presets", juce::Colours::white.withAlpha (0.4f));
        search.addListener (this);
        search.addKeyListener (this);
        addAndMakeVisible (search);

        list.setModel (this);
        list.setRowHeight (24);
        list.setColour (juce::ListBox::backgroundColourId, Theme::panel);
        addAndMakeVisible (list);

        filter();
        setSize (300, 320);
    }

    using juce::Component::keyPressed;

    void visibilityChanged() override
    {
        if (isShowing())
            search.grabKeyboardFocus();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        search.setBounds (area.removeFromTop (28));
        area.removeFromTop (6);
        list.setBounds (area);
    }

private:
    int getNumRows() override   { return (int) shown.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, (int) shown.size()))
            return;

        if (selected)
            g.fillAll (Theme::accent.withAlpha (0.5f));

        const auto& entry = shown[(size_t) row];
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (entry.name, 8, 0, width - 80, height, juce::Justification::centredLeft, true);
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (entry.isFactory ? "factory" : "yours", width - 70, 0, 62, height, juce::Justification::centredRight);
    }

    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override   { choose (row); }
    void returnKeyPressed (int row) override                                    { choose (row); }

    void textEditorTextChanged (juce::TextEditor&) override   { filter(); }
    void textEditorReturnKeyPressed (juce::TextEditor&) override   { choose (juce::jmax (0, list.getSelectedRow())); }

    bool keyPressed (const juce::KeyPress& key, juce::Component*) override
    {
        // Arrow keys move through the list while typing.
        const auto row = list.getSelectedRow();

        if (key == juce::KeyPress::downKey)   { list.selectRow (juce::jmin (getNumRows() - 1, row + 1)); return true; }
        if (key == juce::KeyPress::upKey)     { list.selectRow (juce::jmax (0, row - 1)); return true; }

        return false;
    }

    void filter()
    {
        const auto words = juce::StringArray::fromTokens (search.getText(), " ", {});
        shown.clear();

        for (const auto& entry : all)
        {
            bool matches = true;

            for (const auto& word : words)
                matches = matches && entry.name.containsIgnoreCase (word);

            if (matches)
                shown.push_back (entry);
        }

        list.updateContent();
        list.repaint();

        if (! shown.empty())
            list.selectRow (0);
    }

    void choose (int row)
    {
        if (! juce::isPositiveAndBelow (row, (int) shown.size()))
            return;

        const auto entry = shown[(size_t) row];
        const auto callback = onChosen;

        // Closing the call-out box deletes this component, so do that last.
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();

        callback (entry);
    }

    std::vector<PresetManager::Entry> all, shown;
    std::function<void (const PresetManager::Entry&)> onChosen;
    juce::TextEditor search;
    juce::ListBox list;
};
}

PresetBar::PresetBar (PresetManager& presetsToUse)
    : presets (presetsToUse)
{
    picker.setTextWhenNothingSelected ("Choose a preset");
    picker.onChange = [this] { choiceChanged(); };
    addAndMakeVisible (picker);

    saveButton.setTooltip ("Save the current sound. Factory presets are saved as a new preset of your own.");
    saveButton.onClick = [this] { save(); };
    addAndMakeVisible (saveButton);

    moreButton.setTooltip ("Save as, rename, delete, find a preset (Ctrl+F), or open the presets folder.");
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

        // Presets in a folder go in a submenu named after it, after the loose ones.
        std::map<juce::String, juce::PopupMenu> folders;

        for (const auto& name : userNames)
        {
            entries.push_back ({ name, false });

            if (name.containsChar ('/'))
                folders[name.upToFirstOccurrenceOf ("/", false, false)]
                    .addItem ((int) entries.size(), name.fromFirstOccurrenceOf ("/", false, false));
            else
                picker.addItem (name, (int) entries.size());
        }

        for (auto& [folder, menu] : folders)
            picker.getRootMenu()->addSubMenu (folder, menu);
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

    dialogs::askForText ("Save preset", "Name for this preset. To put it in a folder, type the folder first, "
                                        "for example \"Blues/Warm drive\".", suggestion, [safeThis] (juce::String name)
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

void PresetBar::showFinder()
{
    if (! isShowing())
        return;

    std::vector<PresetManager::Entry> all;

    for (const auto& name : presets.getFactoryNames())   all.push_back ({ name, true });
    for (const auto& name : presets.getUserNames())      all.push_back ({ name, false });

    auto finder = std::make_unique<PresetFinder> (std::move (all),
                                                  [safeThis = juce::Component::SafePointer<PresetBar> (this)] (const PresetManager::Entry& entry)
                                                  {
                                                      if (safeThis != nullptr)
                                                          safeThis->load (entry);
                                                  });

    juce::CallOutBox::launchAsynchronously (std::move (finder), picker.getScreenBounds(), nullptr);
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
    menu.addItem (findId, "Find a preset...  (Ctrl+F)");
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
            case findId:        safeThis->showFinder(); break;
            case openFolderId:  safeThis->presets.getUserFolder().createDirectory();
                                safeThis->presets.getUserFolder().revealToUser(); break;
            default:            break;
        }
    });
}
