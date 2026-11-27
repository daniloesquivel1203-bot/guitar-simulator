#pragma once

#include "engine/AudioEngine.h"
#include "engine/CustomPedalLibrary.h"
#include "engine/ToneHistory.h"
#include "ui/AmpRig.h"
#include "ui/ChainStack.h"
#include "ui/CustomPedalPanel.h"
#include "ui/PatchPanel.h"
#include "ui/LevelMeter.h"
#include "ui/NeighbourPedal.h"
#include "ui/PedalTray.h"
#include "ui/PresetBar.h"
#include "ui/TonePanels.h"

#include <juce_gui_extra/juce_gui_extra.h>

/** Top-level content of the app window: the header, the pedal stack on the
    left with the input and output levels, the selected pedal in the middle
    above the pedal tray, and the amp and cabinet on the right. */
class MainComponent final : public juce::Component,
                            public juce::DragAndDropContainer,
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
    void paintCap (juce::Graphics&, juce::Rectangle<int>) const;

    AudioEngine& engine;
    CustomPedalLibrary customPedals { engine.getPresets().getUserFolder().getSiblingFile ("Custom Pedals") };

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
    juce::TextButton practiceButton { "Practice" };
    juce::TextButton settingsButton { "Audio settings" };

    juce::Label inputHeading { {}, "IN" };
    LevelMeter inputMeter { true };
    juce::Slider inputGain;

    juce::Label outputHeading { {}, "OUT" };
    LevelMeter outputMeter { false };
    juce::Slider outputGain;
    juce::ToggleButton muteButton { "Mute" };

    /** Redraws the stack from the board. The selection stays on the same block
        if it still exists, otherwise on the tile at `fallbackIndex`, otherwise the amp. */
    void rebuildChain (int fallbackIndex = -1);
    void selectBlock (int index);
    void selectAmp();
    void showTuner();
    void showPractice();
    void showAddMenu (juce::Rectangle<int> screenArea);
    void addFromTray (const juce::String& description, Pedalboard::Side, int index);
    void addFromTray (const juce::String& description);
    bool isWidePanelShowing() const;
    void showPedalMenu (int tile, juce::Rectangle<int> screenArea);

    /** Shows the blocks before and after the open pedal beside it, when it is a stompbox. */
    void layoutNeighbours (juce::Rectangle<int>& area);
    void paintCable (juce::Graphics&, juce::Point<float> from, juce::Point<float> to) const;
    void removePedal (Pedalboard::Side, int index);

    /** One per brick in the chain stack, in signal order. Empty for the amp and add bricks. */
    std::vector<std::function<std::unique_ptr<TonePanel>()>> panelMakers;

    /** Keeps the pedal behind the open panel alive, even after a preset has replaced it. */
    std::vector<std::shared_ptr<Pedal>> tilePedals;
    std::shared_ptr<Pedal> panelPedal;
    ChainStack chainStack;
    PedalTray pedalTray;
    AmpRig ampRig;
    NeighbourPedal leftNeighbour { NeighbourPedal::Placement::left };
    NeighbourPedal rightNeighbour { NeighbourPedal::Placement::right };
    int leftNeighbourTile = -1, rightNeighbourTile = -1;

    /** Identify blocks across rebuilds by their on/off switch: the one that
        should be selected, and the one whose panel is showing. */
    Parameter* selectedBlock = nullptr;
    Parameter* panelBlock = nullptr;
    bool showingTuner = false;
    bool showingPractice = false;
    bool widePanel = false;   // a Custom or Patch pedal's editor, which needs the amp's space too
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
