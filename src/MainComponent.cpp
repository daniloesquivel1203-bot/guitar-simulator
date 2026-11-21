#include "MainComponent.h"

#include "ui/AudioSettingsPanel.h"
#include "ui/PracticePanel.h"
#include "ui/Theme.h"
#include "ui/TunerPanel.h"

namespace
{
constexpr int meterRefreshHz = 30;
constexpr int stripWidth = 120;
constexpr int headerHeight = 100;

const auto background = Theme::background;
const auto panel = Theme::panel;
const auto good = Theme::good;
const auto warning = Theme::warning;
}

MainComponent::MainComponent (AudioEngine& engineToUse)
    : engine (engineToUse),
      presetBar (engine.getPresets())
{
    // A loaded preset replaces the pedals, so rebuild the strip and the panel.
    presetBar.onPresetLoaded = [this]
    {
        // Every knob may have changed, so the panel is rebuilt too.
        panelBlock = nullptr;
        rebuildChain (chainStrip.getSelected());
    };
    addAndMakeVisible (presetBar);

    undoButton.setTooltip ("Undo the last change to the sound (Ctrl+Z).");
    undoButton.onClick = [this] { undo(); };
    addAndMakeVisible (undoButton);

    redoButton.setTooltip ("Redo (Ctrl+Y).");
    redoButton.onClick = [this] { redo(); };
    addAndMakeVisible (redoButton);

    // A and B work like two memories: the sound you leave is kept, and the
    // first switch to B starts it as a copy of A.
    for (auto* button : { &slotAButton, &slotBButton })
    {
        button->setClickingTogglesState (false);
        button->setTooltip ("Compare two versions of your sound. Tweak one, switch, and tweak the other. "
                            "B starts as a copy of A. Right-click to copy one onto the other.");
        button->setColour (juce::TextButton::buttonOnColourId, Theme::accent);
        addAndMakeVisible (*button);
    }

    slotAButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    slotBButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
    slotAButton.setToggleState (true, juce::dontSendNotification);
    slotAButton.onClick = [this] { switchSlot (0); };
    slotBButton.onClick = [this] { switchSlot (1); };

    slotAButton.onRightClick = [this] { showSlotMenu (slotAButton); };
    slotBButton.onRightClick = [this] { showSlotMenu (slotBButton); };

    setWantsKeyboardFocus (true);
    history.reset (engine.getPresets().snapshot());
    refreshHistoryButtons();

    tunerButton.setTooltip ("Show the tuner.");
    tunerButton.onClick = [this] { showTuner(); };
    addAndMakeVisible (tunerButton);

    practiceButton.setTooltip ("Metronome, looper and recording.");
    practiceButton.onClick = [this] { showPractice(); };
    addAndMakeVisible (practiceButton);

    chainStrip.onSelect = [this] (int index) { selectBlock (index); };
    chainStrip.onToggle = [this] (int index)
    {
        if (index == chainStrip.getSelected() && detailPanel != nullptr)
            detailPanel->refreshPower();
    };
    chainStrip.onAdd = [this] (Pedalboard::Side side, juce::Rectangle<int> area) { showAddMenu (side, area); };
    chainStrip.onPedalMenu = [this] (int tile) { showPedalMenu (tile); };
    chainStrip.onMove = [this] (Pedalboard::Side fromSide, int fromIndex, Pedalboard::Side toSide, int toIndex)
    {
        if (engine.getChain().getBoard().move (fromSide, fromIndex, toSide, toIndex))
            rebuildChain();
    };
    addAndMakeVisible (chainStrip);

    // Start on the amp, which is where most of the tone is set.
    selectedBlock = &engine.getChain().getAmp().enabled;
    rebuildChain();

    title.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    addAndMakeVisible (title);

    statusLine.setFont (juce::FontOptions (14.0f));
    statusLine.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLine);

    detailLine.setFont (juce::FontOptions (13.0f));
    detailLine.setJustificationType (juce::Justification::centredRight);
    detailLine.setMinimumHorizontalScale (0.8f);
    detailLine.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
    addAndMakeVisible (detailLine);

    settingsButton.onClick = [this] { showAudioSettings(); };
    addAndMakeVisible (settingsButton);

    for (auto* heading : { &inputHeading, &outputHeading })
    {
        heading->setFont (juce::FontOptions (12.0f, juce::Font::bold));
        heading->setJustificationType (juce::Justification::centred);
        heading->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
        addAndMakeVisible (*heading);
    }

    auto& chain = engine.getChain();

    styleGainSlider (inputGain, SignalChain::minInputGainDb, SignalChain::maxInputGainDb, chain.getInputGainDb());
    inputGain.onValueChange = [this] { engine.setInputGainDb ((float) inputGain.getValue()); };
    inputGain.setTooltip ("Input trim. Double-click to reset.");
    addAndMakeVisible (inputGain);

    styleGainSlider (outputGain, SignalChain::minOutputGainDb, SignalChain::maxOutputGainDb, chain.getOutputGainDb());
    outputGain.onValueChange = [this] { engine.setOutputGainDb ((float) outputGain.getValue()); };
    outputGain.setTooltip ("Output level.");
    addAndMakeVisible (outputGain);

    muteButton.onClick = [this] { engine.getChain().setMuted (muteButton.getToggleState()); };
    addAndMakeVisible (muteButton);

    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);

    engine.addChangeListener (this);
    refreshStatus();
    startTimerHz (meterRefreshHz);

    setSize (1040, 560);
}

MainComponent::~MainComponent()
{
    engine.removeChangeListener (this);
}

void MainComponent::styleGainSlider (juce::Slider& slider, double minDb, double maxDb, double valueDb)
{
    slider.setSliderStyle (juce::Slider::LinearVertical);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 20);
    slider.setRange (minDb, maxDb, 0.1);
    slider.setTextValueSuffix (" dB");
    slider.setDoubleClickReturnValue (true, 0.0);
    slider.setValue (valueDb, juce::dontSendNotification);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (background);

    auto area = getLocalBounds();
    area.removeFromTop (headerHeight);

    g.setColour (panel);
    g.fillRect (area.removeFromLeft (stripWidth));
    g.fillRect (area.removeFromRight (stripWidth));
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (headerHeight).reduced (16, 12);

    // Top row: title, presets, tuner and settings. Bottom row: device status.
    auto topRow = header.removeFromTop (32);
    settingsButton.setBounds (topRow.removeFromRight (130));
    topRow.removeFromRight (8);
    tunerButton.setBounds (topRow.removeFromRight (80));
    topRow.removeFromRight (8);
    practiceButton.setBounds (topRow.removeFromRight (90));
    topRow.removeFromRight (16);
    slotBButton.setBounds (topRow.removeFromRight (30));
    slotAButton.setBounds (topRow.removeFromRight (30));
    topRow.removeFromRight (8);
    redoButton.setBounds (topRow.removeFromRight (50));
    topRow.removeFromRight (4);
    undoButton.setBounds (topRow.removeFromRight (50));
    topRow.removeFromRight (16);
    title.setBounds (topRow.removeFromLeft (180));
    presetBar.setBounds (topRow.removeFromRight (juce::jmin (400, topRow.getWidth())));

    header.removeFromTop (12);
    statusLine.setBounds (header.removeFromLeft (header.getWidth() * 2 / 5));
    detailLine.setBounds (header);

    auto layoutStrip = [] (juce::Rectangle<int> strip, juce::Label& heading, LevelMeter& meter,
                           juce::Slider& slider, juce::Component* footer)
    {
        strip = strip.reduced (12);
        heading.setBounds (strip.removeFromTop (20));
        strip.removeFromTop (8);

        // Reserve the footer row on both strips so the meters and sliders line up.
        auto footerRow = strip.removeFromBottom (24);
        strip.removeFromBottom (4);

        if (footer != nullptr)
            footer->setBounds (footerRow);

        // Stop the meter above the slider's text box.
        meter.setBounds (strip.removeFromLeft (20).withTrimmedBottom (24));
        strip.removeFromLeft (4);
        slider.setBounds (strip);
    };

    layoutStrip (area.removeFromLeft (stripWidth), inputHeading, inputMeter, inputGain, nullptr);
    layoutStrip (area.removeFromRight (stripWidth), outputHeading, outputMeter, outputGain, &muteButton);

    area = area.reduced (16);
    chainStrip.setBounds (area.removeFromTop (64));
    area.removeFromTop (16);

    if (detailPanel != nullptr)
        detailPanel->setBounds (area);
}

void MainComponent::rebuildChain (int fallbackIndex)
{
    auto& chain = engine.getChain();
    auto& board = chain.getBoard();

    std::vector<ChainStrip::Tile> tiles;
    panelMakers.clear();
    tilePedals.clear();

    const auto addSide = [&] (Pedalboard::Side side)
    {
        const auto& pedals = board.getPedals (side);

        for (int i = 0; i < (int) pedals.size(); ++i)
        {
            auto pedal = pedals[(size_t) i];
            tiles.push_back ({ ChainStrip::Kind::pedal, side, i, pedal->type.shortName, &pedal->getEffect().enabled });
            tilePedals.push_back (pedal);

            // The panel's Remove button works on whichever position the pedal has by then.
            panelMakers.push_back ([this, pedal]
            {
                return std::make_unique<EffectPanel> (pedal->getEffect(), [this, weak = std::weak_ptr<Pedal> (pedal)]
                {
                    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), weak]
                    {
                        auto locked = weak.lock();

                        if (safe == nullptr || locked == nullptr)
                            return;

                        for (auto s : { Pedalboard::beforeAmp, Pedalboard::afterAmp })
                        {
                            const auto& list = safe->engine.getChain().getBoard().getPedals (s);

                            for (int index = 0; index < (int) list.size(); ++index)
                                if (list[(size_t) index] == locked)
                                    return safe->removePedal (s, index);
                        }
                    });
                });
            });
        }

        if ((int) pedals.size() < Pedalboard::maxPedalsPerSide)
        {
            tiles.push_back ({ ChainStrip::Kind::add, side, -1, "+", nullptr });
            panelMakers.push_back ({});
            tilePedals.push_back ({});
        }
    };

    addSide (Pedalboard::beforeAmp);

    tiles.push_back ({ ChainStrip::Kind::amp, Pedalboard::beforeAmp, -1, "Amp", &chain.getAmp().enabled });
    panelMakers.push_back ([&chain] { return std::make_unique<AmpPanel> (chain.getAmp(), chain.getCapture(), chain.getCabinet()); });
    tilePedals.push_back ({});
    tiles.push_back ({ ChainStrip::Kind::cabinet, Pedalboard::afterAmp, -1, "Cab", &chain.getCabinet().enabled });
    panelMakers.push_back ([&chain] { return std::make_unique<CabinetPanel> (chain.getCabinet()); });
    tilePedals.push_back ({});

    addSide (Pedalboard::afterAmp);

    chainStrip.setTiles (tiles);

    if (showingTuner || showingPractice)
    {
        chainStrip.setSelected (-1);
        return;
    }

    for (int i = 0; i < (int) tiles.size(); ++i)
    {
        if (selectedBlock != nullptr && tiles[(size_t) i].enabled == selectedBlock)
        {
            // The same block's panel can stay; only the highlight moves.
            if (panelBlock == selectedBlock && detailPanel != nullptr)
                chainStrip.setSelected (i);
            else
                selectBlock (i);

            return;
        }
    }

    if (juce::isPositiveAndBelow (fallbackIndex, (int) tiles.size())
        && tiles[(size_t) fallbackIndex].kind != ChainStrip::Kind::add)
        selectBlock (fallbackIndex);
    else
        selectAmp();
}

void MainComponent::selectAmp()
{
    const auto& tiles = chainStrip.getTiles();

    for (int i = 0; i < (int) tiles.size(); ++i)
        if (tiles[(size_t) i].kind == ChainStrip::Kind::amp)
            return selectBlock (i);
}

void MainComponent::selectBlock (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) panelMakers.size()) || panelMakers[(size_t) index] == nullptr)
        return;

    chainStrip.setSelected (index);
    selectedBlock = panelBlock = chainStrip.getTiles()[(size_t) index].enabled;
    showingTuner = showingPractice = false;
    tunerButton.setToggleState (false, juce::dontSendNotification);
    practiceButton.setToggleState (false, juce::dontSendNotification);
    detailPanel.reset();   // closes the tuner first, if it was open
    panelPedal = tilePedals[(size_t) index];
    detailPanel = panelMakers[(size_t) index]();
    addAndMakeVisible (*detailPanel);
    resized();
}

void MainComponent::showAddMenu (Pedalboard::Side side, juce::Rectangle<int> screenArea)
{
    juce::PopupMenu menu;
    menu.addSectionHeader (side == Pedalboard::beforeAmp ? "Add a pedal before the amp" : "Add a pedal after the amp");

    // One submenu per category, in the order the categories first appear.
    const auto& types = getPedalTypes();
    juce::StringArray categories;

    for (const auto& type : types)
        categories.addIfNotAlreadyThere (type.category);

    for (const auto& category : categories)
    {
        juce::PopupMenu submenu;

        for (int i = 0; i < (int) types.size(); ++i)
            if (category == types[(size_t) i].category)
                submenu.addItem (i + 1, types[(size_t) i].create()->name);

        menu.addSubMenu (category, submenu);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (screenArea),
                        [safe = juce::Component::SafePointer<MainComponent> (this), side] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;

                            if (auto pedal = safe->engine.getChain().getBoard().add (side, getPedalTypes()[(size_t) result - 1]))
                            {
                                // Open the new pedal's panel, even if the tuner was showing.
                                safe->selectedBlock = &pedal->getEffect().enabled;
                                safe->showingTuner = safe->showingPractice = false;
                                safe->rebuildChain();
                            }
                        });
}

void MainComponent::showPedalMenu (int tileIndex)
{
    const auto tile = chainStrip.getTiles()[(size_t) tileIndex];
    const auto otherSide = tile.side == Pedalboard::beforeAmp ? Pedalboard::afterAmp : Pedalboard::beforeAmp;
    const bool otherSideFull = (int) engine.getChain().getBoard().getPedals (otherSide).size() >= Pedalboard::maxPedalsPerSide;

    juce::PopupMenu menu;
    menu.addItem (1, tile.side == Pedalboard::beforeAmp ? "Move after the amp" : "Move before the amp", ! otherSideFull);
    menu.addSeparator();
    menu.addItem (2, "Remove " + tile.shortName);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (chainStrip),
                        [safe = juce::Component::SafePointer<MainComponent> (this), tile, otherSide] (int result)
                        {
                            if (safe == nullptr)
                                return;

                            auto& board = safe->engine.getChain().getBoard();

                            if (result == 1)
                            {
                                // Before the amp it goes last; after the amp it goes first, so it stays next to the amp.
                                const auto toIndex = otherSide == Pedalboard::beforeAmp ? (int) board.getPedals (otherSide).size() : 0;

                                if (board.move (tile.side, tile.index, otherSide, toIndex))
                                    safe->rebuildChain();
                            }
                            else if (result == 2)
                            {
                                safe->removePedal (tile.side, tile.index);
                            }
                        });
}

void MainComponent::removePedal (Pedalboard::Side side, int index)
{
    auto& board = engine.getChain().getBoard();
    const auto& pedals = board.getPedals (side);

    if (! juce::isPositiveAndBelow (index, (int) pedals.size()))
        return;

    const auto wasSelected = &pedals[(size_t) index]->getEffect().enabled == selectedBlock;
    const auto tileIndex = chainStrip.getSelected();
    board.remove (side, index);

    // The removed pedal's panel holds references to it, so close it first.
    if (wasSelected)
    {
        selectedBlock = panelBlock = nullptr;
        detailPanel.reset();
    }

    rebuildChain (wasSelected ? tileIndex : -1);
}

void MainComponent::showTuner()
{
    // Pressing the button again goes back to the amp.
    if (showingTuner)
    {
        selectAmp();
        return;
    }

    chainStrip.setSelected (-1);
    selectedBlock = panelBlock = nullptr;
    showingTuner = true;
    showingPractice = false;
    tunerButton.setToggleState (true, juce::dontSendNotification);
    practiceButton.setToggleState (false, juce::dontSendNotification);
    detailPanel.reset();
    panelPedal.reset();
    detailPanel = std::make_unique<TunerPanel> (engine);
    addAndMakeVisible (*detailPanel);
    resized();
}

void MainComponent::showPractice()
{
    // Pressing the button again goes back to the amp. The tools keep running.
    if (showingPractice)
    {
        selectAmp();
        return;
    }

    chainStrip.setSelected (-1);
    selectedBlock = panelBlock = nullptr;
    showingPractice = true;
    showingTuner = false;
    practiceButton.setToggleState (true, juce::dontSendNotification);
    tunerButton.setToggleState (false, juce::dontSendNotification);
    detailPanel.reset();
    panelPedal.reset();
    detailPanel = std::make_unique<PracticePanel> (engine);
    addAndMakeVisible (*detailPanel);
    resized();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshStatus();
}

void MainComponent::timerCallback()
{
    auto& chain = engine.getChain();
    inputMeter.update (chain.readInputPeak(), chain.readInputClipped());
    outputMeter.update (chain.readOutputPeak(), false);

    // Shows when the metronome, looper or recorder is running behind another panel.
    const auto practiceText = PracticePanel::isBusy (engine.getPractice()) && ! showingPractice
                                  ? juce::String::fromUTF8 ("Practice \xe2\x80\xa2")
                                  : juce::String ("Practice");

    if (practiceButton.getButtonText() != practiceText)
        practiceButton.setButtonText (practiceText);

    // Keeps the tile lights in step with switches changed in the panel below.
    chainStrip.repaint();

    if (detailPanel != nullptr)
        detailPanel->refreshDimming();

    // Record settled changes for undo, about four times a second.
    if (++ticksSinceHistoryCheck >= meterRefreshHz / 4)
    {
        ticksSinceHistoryCheck = 0;
        history.observe (engine.getPresets().snapshot());
        refreshHistoryButtons();
    }

    // The CPU and dropout figures change continuously, so refresh them a few times a second.
    if (++ticksSinceStatusRefresh >= meterRefreshHz / 3)
        refreshStatus();
}

void MainComponent::refreshStatus()
{
    ticksSinceStatusRefresh = 0;
    const auto status = engine.getStatus();

    if (! status.running)
    {
        statusLine.setColour (juce::Label::textColourId, warning);
        statusLine.setText (status.message.isNotEmpty() ? status.message
                                                        : "No audio device. Open Audio settings to choose one.",
                            juce::dontSendNotification);
        detailLine.setText ({}, juce::dontSendNotification);
        return;
    }

    const auto inputs = engine.getInputChannelNames();
    const auto channel = engine.getInputChannel();
    const bool inputOk = engine.isInputChannelAvailable();

    statusLine.setColour (juce::Label::textColourId, inputOk ? good : warning);
    statusLine.setText (status.deviceName + " (" + status.deviceType + ")  |  "
                            + (inputOk ? "guitar on input " + juce::String (channel + 1) + ": " + inputs[channel]
                                       : juce::String ("no guitar input selected. Open Audio settings.")),
                        juce::dontSendNotification);

    detailLine.setText (juce::String (status.sampleRate / 1000.0, 1) + " kHz  |  buffer "
                            + juce::String (status.bufferSize) + "  |  latency "
                            + juce::String (status.roundTripMs, 1) + " ms round trip (driver reported)  |  CPU "
                            + juce::String (juce::roundToInt (status.cpuUsage * 100.0)) + "%  |  dropouts "
                            + juce::String (juce::jmax (0, status.xruns)),
                        juce::dontSendNotification);
}

//==============================================================================
bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto modifiers = key.getModifiers();
    const auto code = key.getKeyCode();

    if (modifiers.isCommandDown() && (code == 'Z' || code == 'z'))
    {
        if (modifiers.isShiftDown())
            redo();
        else
            undo();

        return true;
    }

    if (modifiers.isCommandDown() && (code == 'Y' || code == 'y'))
    {
        redo();
        return true;
    }

    if (modifiers.isCommandDown() && (code == 'F' || code == 'f'))
    {
        presetBar.showFinder();
        return true;
    }

    return false;
}

void MainComponent::applyState (const juce::String& state)
{
    juce::var parsed;

    if (state.isEmpty() || juce::JSON::parse (state, parsed).failed())
        return;

    PresetManager::apply (engine.getChain(), parsed);

    // Every knob may have changed, so the open panel is rebuilt too.
    panelBlock = nullptr;
    rebuildChain (chainStrip.getSelected());
}

void MainComponent::undo()
{
    applyState (history.undo());
    refreshHistoryButtons();
}

void MainComponent::redo()
{
    applyState (history.redo());
    refreshHistoryButtons();
}

void MainComponent::switchSlot (int slot)
{
    if (slot == activeSlot)
        return;

    const auto current = engine.getPresets().snapshot();
    slots[(size_t) activeSlot] = current;

    // The first time B is used it starts as a copy of A, so nothing changes yet.
    if (slots[(size_t) slot].isEmpty())
        slots[(size_t) slot] = current;
    else
        applyState (slots[(size_t) slot]);

    activeSlot = slot;
    slotAButton.setToggleState (activeSlot == 0, juce::dontSendNotification);
    slotBButton.setToggleState (activeSlot == 1, juce::dontSendNotification);
}

void MainComponent::showSlotMenu (juce::Component& target)
{
    const auto other = 1 - activeSlot;
    const juce::String active (activeSlot == 0 ? "A" : "B"), inactive (other == 0 ? "A" : "B");

    juce::PopupMenu menu;
    menu.addItem (1, "Copy " + active + " onto " + inactive);
    menu.addItem (2, "Copy " + inactive + " onto " + active, slots[(size_t) other].isNotEmpty());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                        [safe = juce::Component::SafePointer<MainComponent> (this), other] (int result)
                        {
                            if (safe == nullptr)
                                return;

                            if (result == 1)
                                safe->slots[(size_t) other] = safe->engine.getPresets().snapshot();
                            else if (result == 2)
                                safe->applyState (safe->slots[(size_t) other]);
                        });
}

void MainComponent::refreshHistoryButtons()
{
    undoButton.setEnabled (history.canUndo());
    redoButton.setEnabled (history.canRedo());
}

void MainComponent::showAudioSettings()
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new AudioSettingsPanel (engine));
    options.dialogTitle = "Audio settings";
    options.dialogBackgroundColour = background;
    options.componentToCentreAround = this;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}
