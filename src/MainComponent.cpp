#include "MainComponent.h"

#include "ui/AudioSettingsPanel.h"
#include "ui/Dialogs.h"
#include "ui/PracticePanel.h"
#include "ui/Theme.h"
#include "ui/TunerPanel.h"

namespace
{
constexpr int meterRefreshHz = 30;
constexpr int stackWidth = 270;
constexpr int rigWidth = 450;
constexpr int trayHeight = 150;
constexpr int capHeight = 62;
constexpr int headerHeight = 100;
constexpr int myPedalsFirstId = 10000;   // add-menu ids of saved Custom pedals

const auto background = Theme::background;
const auto panel = Theme::panel;
const auto good = Theme::good;
const auto warning = Theme::warning;
}

MainComponent::MainComponent (AudioEngine& engineToUse)
    : engine (engineToUse),
      presetBar (engine.getPresets()),
      ampRig (engine.getChain().getAmp(), engine.getChain().getCapture(), engine.getChain().getCabinet(), engine.getSettings())
{
    // A loaded preset replaces the pedals, so rebuild the strip and the panel.
    presetBar.onPresetLoaded = [this]
    {
        // Every knob may have changed, so the panel is rebuilt too.
        panelBlock = nullptr;
        rebuildChain (chainStack.getSelected());
        ampRig.refreshAll();
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

    chainStack.onSelect = [this] (int index) { selectBlock (index); };
    chainStack.onToggle = [this] (int index)
    {
        if (index == chainStack.getSelected() && detailPanel != nullptr)
            detailPanel->refreshPower();
    };
    chainStack.onAdd = [this] (juce::Rectangle<int> brick) { showAddMenu (chainStack.localAreaToGlobal (brick)); };
    chainStack.onDropNew = [this] (const juce::var& description, Pedalboard::Side side, int index)
    {
        addFromTray (description.toString(), side, index);
    };
    chainStack.onPedalMenu = [this] (int tile) { showPedalMenu (tile); };
    chainStack.onMove = [this] (Pedalboard::Side fromSide, int fromIndex, Pedalboard::Side toSide, int toIndex)
    {
        if (engine.getChain().getBoard().move (fromSide, fromIndex, toSide, toIndex))
            rebuildChain();
    };
    addAndMakeVisible (chainStack);

    pedalTray.onPick = [this] (const juce::String& description) { addFromTray (description); };
    pedalTray.getMyPedals = [this] { return customPedals.getNames(); };
    addAndMakeVisible (pedalTray);
    addAndMakeVisible (ampRig);

    // Start on the first pedal, if there is one. The amp is always in view.
    const auto& firstPedals = engine.getChain().getBoard().getPedals (Pedalboard::beforeAmp);
    selectedBlock = firstPedals.empty() ? &engine.getChain().getAmp().enabled : &firstPedals.front()->getEffect().enabled;
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
        heading->setFont (juce::FontOptions (11.0f, juce::Font::bold));
        heading->setJustificationType (juce::Justification::centredLeft);
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

    setSize (1280, 800);
}

MainComponent::~MainComponent()
{
    engine.removeChangeListener (this);
}

void MainComponent::styleGainSlider (juce::Slider& slider, double minDb, double maxDb, double valueDb)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
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

    // The stack's column, darker, with the input and output boxes.
    auto stack = area.removeFromLeft (stackWidth);
    g.setColour (juce::Colour (0xff15161a));
    g.fillRect (stack);
    g.setColour (juce::Colours::black);
    g.fillRect (stack.removeFromRight (1));

    stack = stack.reduced (14, 12);
    paintCap (g, stack.removeFromTop (capHeight));
    paintCap (g, stack.removeFromBottom (capHeight));

    // The floor the pedal stands on.
    if (! showingTuner && ! showingPractice)
    {
        auto floor = area.withTrimmedBottom (trayHeight);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2d33), (float) floor.getCentreX(), (float) floor.getY() + (float) floor.getHeight() * 0.35f,
                                                 juce::Colour (0xff17181c), (float) floor.getX(), (float) floor.getBottom(), true));
        g.fillRect (floor);

        if (detailPanel == nullptr)
        {
            g.setColour (juce::Colours::white.withAlpha (0.4f));
            g.setFont (juce::FontOptions (14.0f));
            g.drawFittedText ("Pick a pedal in the stack to show it here,\nor add one from the tray below.",
                              floor.withTrimmedRight (rigWidth), juce::Justification::centred, 2);
        }
    }
}

void MainComponent::paintCap (juce::Graphics& g, juce::Rectangle<int> area) const
{
    g.setColour (juce::Colour (0xff23252a));
    g.fillRoundedRectangle (area.toFloat(), 8.0f);
    g.setColour (juce::Colour (0xff30333a));
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 8.0f, 1.0f);
}

bool MainComponent::isWidePanelShowing() const
{
    return showingTuner || showingPractice || widePanel;
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

    // Left: input level, the stack, output level.
    auto stack = area.removeFromLeft (stackWidth).reduced (14, 12);
    stack.removeFromRight (1);

    const auto layoutCap = [] (juce::Rectangle<int> cap, juce::Label& heading, LevelMeter& meter,
                               juce::Slider& slider, juce::Component* extra)
    {
        cap = cap.reduced (10, 7);
        auto top = cap.removeFromTop (24);
        heading.setBounds (top.removeFromLeft (34));

        if (extra != nullptr)
            extra->setBounds (top.removeFromRight (70));

        slider.setBounds (top);
        cap.removeFromTop (6);
        meter.setBounds (cap.removeFromTop (10));
    };

    layoutCap (stack.removeFromTop (capHeight), inputHeading, inputMeter, inputGain, nullptr);
    layoutCap (stack.removeFromBottom (capHeight), outputHeading, outputMeter, outputGain, &muteButton);
    stack.reduce (0, 10);
    chainStack.setBounds (stack);

    // Right: the amp. The tuner, practice tools and the bigger pedal editors use its space too.
    const bool wide = isWidePanelShowing();
    ampRig.setVisible (! wide);
    pedalTray.setVisible (! showingTuner && ! showingPractice);

    if (! wide)
        ampRig.setBounds (area.removeFromRight (rigWidth));

    if (pedalTray.isVisible())
        pedalTray.setBounds (area.removeFromBottom (trayHeight));

    if (detailPanel != nullptr)
        detailPanel->setBounds (wide ? area.reduced (16) : area);
}

void MainComponent::rebuildChain (int fallbackIndex)
{
    auto& chain = engine.getChain();
    auto& board = chain.getBoard();

    std::vector<ChainStack::Tile> tiles;
    panelMakers.clear();
    tilePedals.clear();

    const auto addSide = [&] (Pedalboard::Side side)
    {
        const auto& pedals = board.getPedals (side);

        for (int i = 0; i < (int) pedals.size(); ++i)
        {
            auto pedal = pedals[(size_t) i];
            // A Custom pedal the player named shows that name.
            juce::String tileName (pedal->type.shortName);

            if (auto* custom = dynamic_cast<CustomPedal*> (&pedal->getEffect()); custom != nullptr && custom->label.isNotEmpty())
                tileName = custom->label;

            tiles.push_back ({ ChainStack::Kind::pedal, side, i, tileName, pedal->type.category,
                               pedal_look::pedalColour (pedal->type), &pedal->getEffect().enabled });
            tilePedals.push_back (pedal);

            // The panel's Remove button works on whichever position the pedal has by then.
            panelMakers.push_back ([this, pedal]() -> std::unique_ptr<TonePanel>
            {
                auto onRemove = [this, weak = std::weak_ptr<Pedal> (pedal)]
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
                };

                // The Custom pedal has its own panel, with its blocks and their controls.
                if (auto* custom = dynamic_cast<CustomPedal*> (&pedal->getEffect()))
                    return std::make_unique<CustomPedalPanel> (*custom, std::move (onRemove), customPedals, [this]
                    {
                        // The tile and the panel title show the new name.
                        panelBlock = nullptr;
                        rebuildChain (chainStack.getSelected());
                    });

                // The Patch pedal has its canvas of wired blocks.
                if (auto* patch = dynamic_cast<PatchPedal*> (&pedal->getEffect()))
                    return std::make_unique<PatchPanel> (*patch, std::move (onRemove));

                return std::make_unique<EffectPanel> (pedal->getEffect(), pedal_look::pedalColour (pedal->type),
                                                      pedal->type.category, std::move (onRemove),
                    SharedTempo { [this] { return engine.getPractice().getMetronome().getBpm(); },
                                  [this] (double bpm) { PracticePanel::setTempo (engine, bpm); } });
            });
        }
    };

    addSide (Pedalboard::beforeAmp);

    // The amp and cabinet are one brick. Its controls are always in view on the right.
    tiles.push_back ({ ChainStack::Kind::amp, Pedalboard::beforeAmp, -1, "Amp & Cab", {},
                       juce::Colours::black, &chain.getAmp().enabled });
    panelMakers.push_back ({});
    tilePedals.push_back ({});

    addSide (Pedalboard::afterAmp);

    if (board.getPedals (Pedalboard::beforeAmp).size() + board.getPedals (Pedalboard::afterAmp).size()
            < 2 * (size_t) Pedalboard::maxPedalsPerSide)
    {
        tiles.push_back ({ ChainStack::Kind::add, Pedalboard::afterAmp, -1, "+", {}, {}, nullptr });
        panelMakers.push_back ({});
        tilePedals.push_back ({});
    }

    chainStack.setTiles (tiles);

    if (showingTuner || showingPractice)
    {
        chainStack.setSelected (-1);
        return;
    }

    for (int i = 0; i < (int) tiles.size(); ++i)
    {
        if (selectedBlock != nullptr && tiles[(size_t) i].enabled == selectedBlock)
        {
            // The same block's panel can stay; only the highlight moves.
            if (panelBlock == selectedBlock && detailPanel != nullptr)
                chainStack.setSelected (i);
            else
                selectBlock (i);

            return;
        }
    }

    if (juce::isPositiveAndBelow (fallbackIndex, (int) tiles.size())
        && tiles[(size_t) fallbackIndex].kind != ChainStack::Kind::add)
        selectBlock (fallbackIndex);
    else
        selectAmp();
}

void MainComponent::selectAmp()
{
    const auto& tiles = chainStack.getTiles();

    for (int i = 0; i < (int) tiles.size(); ++i)
        if (tiles[(size_t) i].kind == ChainStack::Kind::amp)
            return selectBlock (i);
}

void MainComponent::selectBlock (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) panelMakers.size()))
        return;

    // The amp's controls are always showing, so selecting it just clears the middle.
    if (chainStack.getTiles()[(size_t) index].kind == ChainStack::Kind::amp)
    {
        chainStack.setSelected (index);
        selectedBlock = panelBlock = chainStack.getTiles()[(size_t) index].enabled;
        showingTuner = showingPractice = widePanel = false;
        tunerButton.setToggleState (false, juce::dontSendNotification);
        practiceButton.setToggleState (false, juce::dontSendNotification);
        detailPanel.reset();
        panelPedal.reset();
        resized();
        repaint();
        return;
    }

    if (panelMakers[(size_t) index] == nullptr)
        return;

    chainStack.setSelected (index);
    selectedBlock = panelBlock = chainStack.getTiles()[(size_t) index].enabled;
    showingTuner = showingPractice = false;
    tunerButton.setToggleState (false, juce::dontSendNotification);
    practiceButton.setToggleState (false, juce::dontSendNotification);
    detailPanel.reset();   // closes the tuner first, if it was open
    panelPedal = tilePedals[(size_t) index];
    detailPanel = panelMakers[(size_t) index]();
    widePanel = dynamic_cast<EffectPanel*> (detailPanel.get()) == nullptr;
    addAndMakeVisible (*detailPanel);

    if (panelPedal != nullptr)
        pedalTray.showCategory (panelPedal->type.category);

    resized();
    repaint();
}

void MainComponent::showAddMenu (juce::Rectangle<int> screenArea)
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Add a pedal after the selected one");

    // One submenu per category, in the order the categories first appear.
    const auto& types = getPedalTypes();
    juce::StringArray categories, descriptions;

    for (const auto& type : types)
        categories.addIfNotAlreadyThere (type.category);

    for (const auto& category : categories)
    {
        juce::PopupMenu submenu;

        for (const auto& type : types)
        {
            if (category == type.category)
            {
                descriptions.add (juce::String ("pedal:") + type.key);
                submenu.addItem (descriptions.size(), type.create()->name);
            }
        }

        menu.addSubMenu (category, submenu);
    }

    // Custom pedals the player saved under their own names.
    juce::PopupMenu mine;

    for (const auto& name : customPedals.getNames())
    {
        descriptions.add ("mine:" + name);
        mine.addItem (descriptions.size(), name);
    }

    if (mine.getNumItems() == 0)
        mine.addItem (-1, "None yet: use Save as my pedal on a Custom pedal", false);

    menu.addSeparator();
    menu.addSubMenu ("My pedals", mine);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (screenArea),
                        [safe = juce::Component::SafePointer<MainComponent> (this), descriptions] (int result)
                        {
                            if (safe != nullptr && result > 0)
                                safe->addFromTray (descriptions[result - 1]);
                        });
}

void MainComponent::addFromTray (const juce::String& description)
{
    if (const auto point = chainStack.getInsertPointAfterSelection())
        addFromTray (description, point->first, point->second);
    else
        dialogs::showMessage ("The pedalboard is full", "Remove a pedal to make room for another.");
}

void MainComponent::addFromTray (const juce::String& description, Pedalboard::Side side, int index)
{
    auto& board = engine.getChain().getBoard();

    if ((int) board.getPedals (side).size() >= Pedalboard::maxPedalsPerSide)
        return;

    index = juce::jlimit (0, (int) board.getPedals (side).size(), index);
    std::shared_ptr<Pedal> added;

    if (description.startsWith ("mine:"))
    {
        // A saved Custom pedal: make a Custom pedal and load its settings before it is heard.
        const auto name = description.fromFirstOccurrenceOf ("mine:", false, false);
        auto pedal = std::make_shared<Pedal> (*findPedalType ("custom"));
        auto& custom = dynamic_cast<CustomPedal&> (pedal->getEffect());

        if (! customPedals.load (name, custom))
            return dialogs::showMessage ("Could not add " + name, "That saved pedal could not be read.");

        custom.enabled.set (1.0f);
        auto list = board.getPedals (side);
        list.insert (list.begin() + index, pedal);
        board.setPedals (side == Pedalboard::beforeAmp ? list : board.getPedals (Pedalboard::beforeAmp),
                         side == Pedalboard::afterAmp ? list : board.getPedals (Pedalboard::afterAmp));
        added = pedal;
    }
    else if (const auto* type = findPedalType (description.fromFirstOccurrenceOf ("pedal:", false, false)))
    {
        added = board.add (side, *type, index);
    }

    if (added == nullptr)
        return;

    // Open the new pedal, even if the tuner was showing.
    selectedBlock = &added->getEffect().enabled;
    showingTuner = showingPractice = false;
    tunerButton.setToggleState (false, juce::dontSendNotification);
    practiceButton.setToggleState (false, juce::dontSendNotification);
    rebuildChain();
}

void MainComponent::showPedalMenu (int tileIndex)
{
    const auto tile = chainStack.getTiles()[(size_t) tileIndex];
    const auto otherSide = tile.side == Pedalboard::beforeAmp ? Pedalboard::afterAmp : Pedalboard::beforeAmp;
    const bool otherSideFull = (int) engine.getChain().getBoard().getPedals (otherSide).size() >= Pedalboard::maxPedalsPerSide;

    juce::PopupMenu menu;
    menu.addItem (1, tile.side == Pedalboard::beforeAmp ? "Move after the amp" : "Move before the amp", ! otherSideFull);
    menu.addSeparator();
    menu.addItem (2, "Remove " + tile.name);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (chainStack),
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
    const auto tileIndex = chainStack.getSelected();
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

    chainStack.setSelected (-1);
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

    chainStack.setSelected (-1);
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

    // Keeps the brick lights, and the amp brick's voicing and cabinet, in step with the controls.
    {
        const auto ampName = chain.getCapture().isInUse() ? juce::String ("Capture")
                                                          : AmpModel::getVoicingNames()[chain.getAmp().voicing.getIndex()];
        const auto cabName = chain.getCabinet().enabled.isOn() ? chain.getCabinet().getCurrentName() : juce::String ("cab off");
        chainStack.setAmpDetail (ampName + " | " + cabName);
    }

    chainStack.repaint();
    ampRig.refreshDimming();

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
    rebuildChain (chainStack.getSelected());
    ampRig.refreshAll();
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
