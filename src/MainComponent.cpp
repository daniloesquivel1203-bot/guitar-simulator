#include "MainComponent.h"

#include "ui/AudioSettingsPanel.h"
#include "ui/Dialogs.h"
#include "ui/JamPanel.h"
#include "ui/SongPanel.h"
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
constexpr int myPedalsFirstId = 10000;   // add-menu ids of saved Custom pedals

const auto background = Theme::background;
const auto panel = Theme::panel;
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

    jamButton.setTooltip ("Jam tracks: a backing band that plays chords in any key and style.");
    jamButton.onClick = [this] { showJam(); };
    addAndMakeVisible (jamButton);
    JamPanel::applySettings (engine);

    songsButton.setTooltip ("Songs: open a Guitar Pro or MusicXML file and play along with its scrolling tab.");
    songsButton.onClick = [this] { showSongs(); };
    addAndMakeVisible (songsButton);

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
    chainStack.onPedalMenu = [this] (int tile) { showPedalMenu (tile, chainStack.getBrickScreenBounds (tile)); };
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

    // The blocks either side of the open pedal: click one to open it instead.
    for (auto* neighbour : { &leftNeighbour, &rightNeighbour })
    {
        addChildComponent (*neighbour);
        neighbour->onClick = [this, neighbour]
        {
            selectBlock (neighbour == &leftNeighbour ? leftNeighbourTile : rightNeighbourTile);
        };
        neighbour->onMenu = [this, neighbour]
        {
            const auto tile = neighbour == &leftNeighbour ? leftNeighbourTile : rightNeighbourTile;
            showPedalMenu (tile, neighbour->localAreaToGlobal (neighbour->getPedalBounds().toNearestInt()));
        };
    }

    // Start on the first pedal, if there is one. The amp is always in view.
    const auto& firstPedals = engine.getChain().getBoard().getPedals (Pedalboard::beforeAmp);
    selectedBlock = firstPedals.empty() ? &engine.getChain().getAmp().enabled : &firstPedals.front()->getEffect().enabled;
    rebuildChain();

    title.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    addAndMakeVisible (title);

    // Shown only when the audio setup needs attention.
    statusLine.setFont (juce::FontOptions (14.0f));
    statusLine.setJustificationType (juce::Justification::centredLeft);
    statusLine.setColour (juce::Label::textColourId, warning);
    addChildComponent (statusLine);

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
    area.removeFromTop (getHeaderHeight());

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
    if (! showingTuner && ! showingPractice && ! showingJam && ! showingSongs)
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

        // Patch cables from the neighbours into the open pedal's jacks.
        if (auto* stompbox = dynamic_cast<EffectPanel*> (detailPanel.get()))
        {
            const auto panelOrigin = stompbox->getPosition().toFloat();

            if (leftNeighbour.isVisible())
                mini_pedal::paintCable (g, leftNeighbour.getPosition().toFloat() + leftNeighbour.getJack(), panelOrigin + stompbox->getJack (true));

            if (rightNeighbour.isVisible())
                mini_pedal::paintCable (g, panelOrigin + stompbox->getJack (false), rightNeighbour.getPosition().toFloat() + rightNeighbour.getJack());
        }
    }
}

void MainComponent::showChainOverview()
{
    auto overview = std::make_unique<ChainOverview>();
    overview->setTiles (chainStack.getTiles());
    overview->onOpen = [this] (int tile) { selectBlock (tile); };
    overview->onMenu = [this] (int tile, juce::Rectangle<int> screenArea) { showPedalMenu (tile, screenArea); };
    detailPanel = std::move (overview);
    addAndMakeVisible (*detailPanel);
}

void MainComponent::layoutNeighbours (juce::Rectangle<int>& area)
{
    leftNeighbourTile = rightNeighbourTile = -1;
    const auto selected = chainStack.getSelected();
    const auto& tiles = chainStack.getTiles();

    auto* stompbox = dynamic_cast<EffectPanel*> (detailPanel.get());

    // Room either side once the open pedal has what it needs. Too little, and the neighbours stay hidden.
    const auto columnWidth = stompbox != nullptr ? juce::jmin (160, (area.getWidth() - stompbox->getComfortableWidth()) / 2) : 0;

    if (stompbox != nullptr && columnWidth >= 80 && juce::isPositiveAndBelow (selected, (int) tiles.size()))
    {
        if (selected > 0 && tiles[(size_t) selected - 1].kind != ChainStack::Kind::add)
            leftNeighbourTile = selected - 1;

        if (selected + 1 < (int) tiles.size() && tiles[(size_t) selected + 1].kind != ChainStack::Kind::add)
            rightNeighbourTile = selected + 1;
    }

    const bool any = leftNeighbourTile >= 0 || rightNeighbourTile >= 0;

    // Both columns are kept, even with one neighbour, so the open pedal stays centred.
    auto leftColumn = any ? area.removeFromLeft (columnWidth) : juce::Rectangle<int>();
    auto rightColumn = any ? area.removeFromRight (columnWidth) : juce::Rectangle<int>();

    const auto place = [&tiles] (NeighbourPedal& neighbour, int tile, juce::Rectangle<int> column)
    {
        neighbour.setVisible (tile >= 0);

        if (tile >= 0)
        {
            neighbour.setTile (tiles[(size_t) tile]);
            neighbour.setBounds (column.reduced (0, 20));
        }
    };

    place (leftNeighbour, leftNeighbourTile, leftColumn);
    place (rightNeighbour, rightNeighbourTile, rightColumn);
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
    return showingTuner || showingPractice || showingJam || showingSongs || widePanel;
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (getHeaderHeight()).reduced (16, 12);

    // Top row: title, presets, tuner and settings. Bottom row: device status.
    auto topRow = header.removeFromTop (32);
    settingsButton.setBounds (topRow.removeFromRight (130));
    topRow.removeFromRight (8);
    tunerButton.setBounds (topRow.removeFromRight (80));
    topRow.removeFromRight (8);
    practiceButton.setBounds (topRow.removeFromRight (90));
    topRow.removeFromRight (8);
    jamButton.setBounds (topRow.removeFromRight (64));
    topRow.removeFromRight (8);
    songsButton.setBounds (topRow.removeFromRight (74));
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

    header.removeFromTop (8);
    statusLine.setBounds (header);

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
    pedalTray.setVisible (! showingTuner && ! showingPractice && ! showingJam && ! showingSongs);

    if (! wide)
        ampRig.setBounds (area.removeFromRight (rigWidth));

    if (pedalTray.isVisible())
        pedalTray.setBounds (area.removeFromBottom (trayHeight));

    if (! showingTuner && ! showingPractice && ! showingJam && ! showingSongs && ! wide)
        layoutNeighbours (area);
    else
    {
        leftNeighbour.setVisible (false);
        rightNeighbour.setVisible (false);
        leftNeighbourTile = rightNeighbourTile = -1;
    }

    if (detailPanel != nullptr)
        detailPanel->setBounds (wide ? area.reduced (16) : area);

    repaint();   // the cables follow the layout
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
                               pedal_look::pedalColour (pedal->type), &pedal->getEffect().enabled,
                               mini_pedal::countKnobs (pedal->getEffect()) });
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

    if (auto* overview = dynamic_cast<ChainOverview*> (detailPanel.get()))
        overview->setTiles (tiles);

    if (showingTuner || showingPractice || showingJam || showingSongs)
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
            {
                chainStack.setSelected (i);
                resized();   // the neighbours may have changed
            }
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

    // The amp's controls are always showing, so selecting it shows the whole chain in the middle.
    if (chainStack.getTiles()[(size_t) index].kind == ChainStack::Kind::amp)
    {
        chainStack.setSelected (index);
        selectedBlock = panelBlock = chainStack.getTiles()[(size_t) index].enabled;
        showingTuner = showingPractice = showingJam = showingSongs = widePanel = false;
        tunerButton.setToggleState (false, juce::dontSendNotification);
        practiceButton.setToggleState (false, juce::dontSendNotification);
        jamButton.setToggleState (false, juce::dontSendNotification);
        songsButton.setToggleState (false, juce::dontSendNotification);
        detailPanel.reset();
        panelPedal.reset();
        showChainOverview();
        resized();
        repaint();
        return;
    }

    if (panelMakers[(size_t) index] == nullptr)
        return;

    chainStack.setSelected (index);
    selectedBlock = panelBlock = chainStack.getTiles()[(size_t) index].enabled;
    showingTuner = showingPractice = showingJam = showingSongs = false;
    tunerButton.setToggleState (false, juce::dontSendNotification);
    practiceButton.setToggleState (false, juce::dontSendNotification);
    jamButton.setToggleState (false, juce::dontSendNotification);
    songsButton.setToggleState (false, juce::dontSendNotification);
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
    showingTuner = showingPractice = showingJam = showingSongs = false;
    tunerButton.setToggleState (false, juce::dontSendNotification);
    practiceButton.setToggleState (false, juce::dontSendNotification);
    jamButton.setToggleState (false, juce::dontSendNotification);
    songsButton.setToggleState (false, juce::dontSendNotification);
    rebuildChain();
}

void MainComponent::showPedalMenu (int tileIndex, juce::Rectangle<int> screenArea)
{
    if (! juce::isPositiveAndBelow (tileIndex, (int) chainStack.getTiles().size()))
        return;

    const auto tile = chainStack.getTiles()[(size_t) tileIndex];

    // Moving is done by dragging, so the menu only removes.
    juce::PopupMenu menu;
    menu.addItem (1, "Remove " + tile.name);

    // The menu opens beside the pedal that was right-clicked.
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (screenArea)
                                                  .withPreferredPopupDirection (juce::PopupMenu::Options::PopupDirection::downwards),
                        [safe = juce::Component::SafePointer<MainComponent> (this), tile] (int result)
                        {
                            if (safe != nullptr && result == 1)
                                safe->removePedal (tile.side, tile.index);
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
    showingPractice = showingJam = showingSongs = false;
    tunerButton.setToggleState (true, juce::dontSendNotification);
    practiceButton.setToggleState (false, juce::dontSendNotification);
    jamButton.setToggleState (false, juce::dontSendNotification);
    songsButton.setToggleState (false, juce::dontSendNotification);
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
    showingTuner = showingJam = showingSongs = false;
    practiceButton.setToggleState (true, juce::dontSendNotification);
    tunerButton.setToggleState (false, juce::dontSendNotification);
    jamButton.setToggleState (false, juce::dontSendNotification);
    songsButton.setToggleState (false, juce::dontSendNotification);
    detailPanel.reset();
    panelPedal.reset();
    detailPanel = std::make_unique<PracticePanel> (engine);
    addAndMakeVisible (*detailPanel);
    resized();
}

void MainComponent::showJam()
{
    // Pressing the button again goes back to the amp. The band keeps playing.
    if (showingJam)
    {
        selectAmp();
        return;
    }

    chainStack.setSelected (-1);
    selectedBlock = panelBlock = nullptr;
    showingJam = true;
    showingTuner = showingPractice = showingSongs = false;
    jamButton.setToggleState (true, juce::dontSendNotification);
    songsButton.setToggleState (false, juce::dontSendNotification);
    tunerButton.setToggleState (false, juce::dontSendNotification);
    practiceButton.setToggleState (false, juce::dontSendNotification);
    detailPanel.reset();
    panelPedal.reset();
    detailPanel = std::make_unique<JamPanel> (engine);
    addAndMakeVisible (*detailPanel);
    resized();
}

void MainComponent::showSongs()
{
    // Pressing the button again goes back to the amp. A song keeps playing.
    if (showingSongs)
    {
        selectAmp();
        return;
    }

    chainStack.setSelected (-1);
    selectedBlock = panelBlock = nullptr;
    showingSongs = true;
    showingTuner = showingPractice = showingJam = false;
    songsButton.setToggleState (true, juce::dontSendNotification);
    tunerButton.setToggleState (false, juce::dontSendNotification);
    practiceButton.setToggleState (false, juce::dontSendNotification);
    jamButton.setToggleState (false, juce::dontSendNotification);
    detailPanel.reset();
    panelPedal.reset();
    detailPanel = std::make_unique<SongPanel> (engine, songSession);
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

    const auto jamText = JamPanel::isBusy (engine) && ! showingJam ? juce::String::fromUTF8 ("Jam \xe2\x80\xa2")
                                                                   : juce::String ("Jam");

    if (jamButton.getButtonText() != jamText)
        jamButton.setButtonText (jamText);

    const auto songsText = SongPanel::isPlaying (engine, songSession) && ! showingSongs
                               ? juce::String::fromUTF8 ("Songs \xe2\x80\xa2")
                               : juce::String ("Songs");

    if (songsButton.getButtonText() != songsText)
        songsButton.setButtonText (songsText);

    // Keeps the brick lights, and the amp brick's voicing and cabinet, in step with the controls.
    {
        const auto ampName = chain.getCapture().isInUse() ? juce::String ("Capture")
                                                          : AmpModel::getVoicingNames()[chain.getAmp().voicing.getIndex()];
        const auto cabName = chain.getCabinet().enabled.isOn() ? chain.getCabinet().getCurrentName() : juce::String ("cab off");
        chainStack.setAmpDetail (ampName + " | " + cabName);
    }

    chainStack.repaint();
    leftNeighbour.repaint();
    rightNeighbour.repaint();
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

    // The header only speaks up when something needs fixing. The device details are in Audio settings.
    juce::String problem;

    if (! status.running)
        problem = status.message.isNotEmpty() ? status.message : "No audio device. Open Audio settings to choose one.";
    else if (! engine.isInputChannelAvailable())
        problem = status.deviceName + ": no guitar input selected. Open Audio settings.";

    statusLine.setText (problem, juce::dontSendNotification);

    if (problem.isNotEmpty() != statusLine.isVisible())
    {
        statusLine.setVisible (problem.isNotEmpty());
        resized();
    }
}

int MainComponent::getHeaderHeight() const
{
    return statusLine.isVisible() ? 88 : 56;
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
