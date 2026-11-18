#pragma once

#include "engine/AudioEngine.h"
#include "engine/ToneHistory.h"
#include "ui/ChainStrip.h"
#include "ui/LevelMeter.h"
#include "ui/PresetBar.h"
#include "ui/TonePanels.h"

#include <juce_gui_extra/juce_gui_extra.h>

/** Top-level content of the app window: status bar, input and output strips,
    and the space where the pedalboard and amp will go. */
class MainComponent final : public juce::Component,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    explicit MainComponent (AudioEngine& engineToUse);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void refreshStatus();
    void showAudioSettings();
    static void styleGainSlider (juce::Slider&, double minDb, double maxDb, double valueDb);

    AudioEngine& engine;

    juce::Label title { {}, "Guitar Simulator" };
    juce::Label statusLine;
    juce::Label detailLine;
    PresetBar presetBar;
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" };
    /** A button that also offers a menu on right-click, instead of clicking. */
    struct SlotButton final : public juce::TextButton
    {
        using juce::TextButton::TextButton;
        std::function<void()> onRightClick;

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) { if (onRightClick != nullptr) onRightClick(); return; }
            juce::TextButton::mouseDown (e);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu())
                juce::TextButton::mouseUp (e);
        }
    };

    SlotButton slotAButton { "A" }, slotBButton { "B" };
    void showSlotMenu (juce::Component& target);
    juce::TextButton tunerButton { "Tuner" };
    juce::TextButton settingsButton { "Audio settings" };

    juce::Label inputHeading { {}, "INPUT" };
    LevelMeter inputMeter { true };
    juce::Slider inputGain;

    juce::Label outputHeading { {}, "OUTPUT" };
    LevelMeter outputMeter { false };
    juce::Slider outputGain;
    juce::ToggleButton muteButton { "Mute" };

    /** Redraws the strip from the board. The selection stays on the same block
        if it still exists, otherwise on the tile at `fallbackIndex`, otherwise the amp. */
    void rebuildChain (int fallbackIndex = -1);
    void selectBlock (int index);
    void selectAmp();
    void showTuner();
    void showAddMenu (Pedalboard::Side, juce::Rectangle<int> screenArea);
    void showPedalMenu (int tile);
    void removePedal (Pedalboard::Side, int index);

    /** One per tile in the chain strip, in signal order. Empty for + tiles. */
    std::vector<std::function<std::unique_ptr<TonePanel>()>> panelMakers;

    /** Keeps the pedal behind the open panel alive, even after a preset has replaced it. */
    std::vector<std::shared_ptr<Pedal>> tilePedals;
    std::shared_ptr<Pedal> panelPedal;
    ChainStrip chainStrip;

    /** Identify blocks across rebuilds by their on/off switch: the one that
        should be selected, and the one whose panel is showing. */
    Parameter* selectedBlock = nullptr;
    Parameter* panelBlock = nullptr;
    bool showingTuner = false;
    std::unique_ptr<TonePanel> detailPanel;

    int ticksSinceStatusRefresh = 0;

    //==============================================================================
    /** Undo, redo and A/B compare all work by applying a saved state. */
    void applyState (const juce::String& state);
    void undo();
    void redo();
    void switchSlot (int slot);
    void refreshHistoryButtons();

    ToneHistory history;
    int ticksSinceHistoryCheck = 0;

    /** The two sounds being compared. The active one is the live sound; the
        other is kept here until you switch back to it. */
    std::array<juce::String, 2> slots;
    int activeSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
