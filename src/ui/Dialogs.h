#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

/** Small pop-up dialogs. They never block the audio or the rest of the window. */
namespace dialogs
{
/** Shows a message with a single OK button. */
inline void showMessage (const juce::String& title, const juce::String& message)
{
    auto* window = new juce::AlertWindow (title, message, juce::MessageBoxIconType::NoIcon);
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey), juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, nullptr, true);
}

/** Asks a question. `onConfirm` runs only if the confirm button is chosen. */
inline void confirm (const juce::String& title, const juce::String& message, const juce::String& confirmText,
                     std::function<void()> onConfirm)
{
    auto* window = new juce::AlertWindow (title, message, juce::MessageBoxIconType::NoIcon);
    window->addButton (confirmText, 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true,
                             juce::ModalCallbackFunction::create ([action = std::move (onConfirm)] (int result)
                             {
                                 if (result == 1 && action != nullptr)
                                     action();
                             }),
                             true);
}

/** Asks for a line of text. `onAccept` runs with the text if OK is chosen. */
inline void askForText (const juce::String& title, const juce::String& message, const juce::String& initialText,
                        std::function<void (juce::String)> onAccept)
{
    auto* window = new juce::AlertWindow (title, message, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("text", initialText);
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    // The window deletes itself when dismissed, so read the text through a safe pointer.
    juce::Component::SafePointer<juce::AlertWindow> safeWindow (window);

    window->enterModalState (true,
                             juce::ModalCallbackFunction::create ([safeWindow, action = std::move (onAccept)] (int result)
                             {
                                 if (result == 1 && safeWindow != nullptr && action != nullptr)
                                     action (safeWindow->getTextEditorContents ("text"));
                             }),
                             true);
}
}
