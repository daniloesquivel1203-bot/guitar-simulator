#include "CustomPedalPanel.h"

#include "Theme.h"

namespace
{
using custom_blocks::BlockType;

constexpr int tileWidth = 72;
constexpr int addTileWidth = 30;
constexpr int tileGap = 10;
}

//==============================================================================
CustomPedalPanel::CustomPedalPanel (CustomPedal& pedalToUse, std::function<void()> onRemovePedal)
    : TonePanel ("CUSTOM PEDAL", &pedalToUse.enabled),
      pedal (pedalToUse),
      power (pedal.enabled),
      level (pedal.level)
{
    addAndMakeVisible (power);

    if (onRemovePedal != nullptr)
    {
        removePedalButton.setTooltip ("Take this pedal off the board.");
        removePedalButton.onClick = std::move (onRemovePedal);
        addAndMakeVisible (removePedalButton);
    }

    addAndMakeVisible (blockRow);
    addAndMakeVisible (level);

    hint.setText ("Click + to add your first block. The guitar passes through the blocks from left to right.",
                  juce::dontSendNotification);
    hint.setJustificationType (juce::Justification::centred);
    hint.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.55f));
    addChildComponent (hint);

    description.setJustificationType (juce::Justification::topLeft);
    description.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
    description.setFont (juce::FontOptions (13.0f));
    addChildComponent (description);

    bandReadout.setJustificationType (juce::Justification::centredLeft);
    bandReadout.setColour (juce::Label::textColourId, Theme::accent);
    bandReadout.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    bandReadout.setBorderSize ({});
    bandReadout.setTooltip ("How much of the tone this block acts on. Raise Narrowness to focus it on a thinner slice.");
    addChildComponent (bandReadout);

    removeBlockButton.setTooltip ("Take the selected block out of this pedal.");
    removeBlockButton.onClick = [this] { removeBlock (selectedSlot); };
    addChildComponent (removeBlockButton);

    refreshBlocks();
}

void CustomPedalPanel::refreshBlocks()
{
    blocks = pedal.getBlocksInOrder();

    if (std::find (blocks.begin(), blocks.end(), selectedSlot) == blocks.end())
        selectedSlot = blocks.empty() ? -1 : blocks.front();

    selectBlock (selectedSlot);
}

void CustomPedalPanel::selectBlock (int slot)
{
    selectedSlot = slot;
    knobs.clear();
    choices.clear();
    switches.clear();

    const bool hasBlock = slot >= 0;
    hint.setVisible (blocks.empty());
    description.setVisible (hasBlock);
    removeBlockButton.setVisible (hasBlock);

    if (hasBlock)
    {
        auto& s = pedal.getSlot (slot);
        description.setText (custom_blocks::getDescription (s.getType()), juce::dontSendNotification);

        for (const auto& control : CustomPedal::getControlsFor (s, s.getType()))
        {
            if (control.parameter->isChoice())
                addAndMakeVisible (choices.add (new ParameterChoice (*control.parameter, control.caption)));
            else if (control.parameter->isSwitch())
                addAndMakeVisible (switches.add (new ParameterToggle (*control.parameter)));
            else
                addAndMakeVisible (knobs.add (new ParameterKnob (*control.parameter, control.caption)));
        }
    }

    const auto type = hasBlock ? pedal.getSlot (slot).getType() : BlockType::empty;
    const bool showsBand = type == BlockType::bandPass || type == BlockType::notch || type == BlockType::peak;
    bandReadout.setVisible (showsBand);

    if (showsBand)
    {
        timerCallback();
        startTimerHz (15);
    }
    else
    {
        stopTimer();
    }

    blockRow.repaint();
    resized();
}

void CustomPedalPanel::timerCallback()
{
    if (selectedSlot < 0)
        return;

    auto& s = pedal.getSlot (selectedSlot);
    bandReadout.setText (custom_blocks::describeBand (s.frequency.get(), s.resonance.get()), juce::dontSendNotification);
}

void CustomPedalPanel::resized()
{
    auto header = getHeaderArea();
    power.setBounds (header.removeFromRight (60));
    header.removeFromRight (12);
    removePedalButton.setBounds (header.removeFromRight (80));

    auto area = getContentArea();

    // The pedal's overall level sits on the right, apart from the blocks.
    auto levelArea = area.removeFromRight (ParameterKnob::preferredWidth);
    level.setBounds (levelArea.withSizeKeepingCentre (ParameterKnob::preferredWidth,
                                                      juce::jmin (ParameterKnob::preferredHeight, levelArea.getHeight())));
    area.removeFromRight (16);

    blockRow.setBounds (area.removeFromTop (48));
    area.removeFromTop (8);
    hint.setBounds (area.withHeight (juce::jmin (60, area.getHeight())));

    // Below the row: a description and a Remove button on the left, then the block's controls.
    auto side = area.removeFromLeft (200);
    removeBlockButton.setBounds (side.removeFromBottom (28).removeFromLeft (120));
    side.removeFromBottom (4);

    if (bandReadout.isVisible())
        bandReadout.setBounds (side.removeFromBottom (34));

    description.setBounds (side.withTrimmedBottom (2));
    area.removeFromLeft (16);

    if (! switches.isEmpty() || ! choices.isEmpty())
    {
        auto column = area.removeFromRight (150);
        column = column.withSizeKeepingCentre (column.getWidth(),
                                               choices.size() * ParameterChoice::preferredHeight + switches.size() * 32);

        for (auto* choice : choices)
            choice->setBounds (column.removeFromTop (ParameterChoice::preferredHeight).withTrimmedBottom (4));

        for (auto* toggle : switches)
            toggle->setBounds (column.removeFromTop (32).reduced (0, 3));
    }

    std::vector<juce::Component*> row;

    for (auto* knob : knobs)
        row.push_back (knob);

    layoutKnobRow (area, row);
}

void CustomPedalPanel::showAddMenu (juce::Rectangle<int> screenArea)
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Add a block");

    for (int t = 1; t < (int) BlockType::numTypes; ++t)
    {
        const auto type = (BlockType) t;

        if (type == BlockType::gain)
            menu.addSeparator();

        menu.addItem (t, custom_blocks::getTypeName (type));
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (screenArea),
                        [safe = juce::Component::SafePointer<CustomPedalPanel> (this)] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;

                            const auto slot = safe->pedal.addBlock ((BlockType) result);

                            if (slot >= 0)
                                safe->selectedSlot = slot;

                            safe->refreshBlocks();
                        });
}

void CustomPedalPanel::showBlockMenu (int slot)
{
    juce::PopupMenu menu;
    menu.addItem (1, "Remove " + juce::String (custom_blocks::getTypeName (pedal.getSlot (slot).getType())));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (blockRow),
                        [safe = juce::Component::SafePointer<CustomPedalPanel> (this), slot] (int result)
                        {
                            if (safe != nullptr && result == 1)
                                safe->removeBlock (slot);
                        });
}

void CustomPedalPanel::removeBlock (int slot)
{
    if (slot < 0)
        return;

    // Select the block that takes its place, or the one before it.
    const auto position = (int) (std::find (blocks.begin(), blocks.end(), slot) - blocks.begin());
    pedal.removeBlock (slot);
    const auto remaining = pedal.getBlocksInOrder();
    selectedSlot = remaining.empty() ? -1 : remaining[(size_t) juce::jmin (position, (int) remaining.size() - 1)];
    refreshBlocks();
}

//==============================================================================
CustomPedalPanel::BlockRow::BlockRow (CustomPedalPanel& ownerToUse)
    : owner (ownerToUse)
{
    setTooltip ("Click a block to show its controls. Drag it to change the order. Right-click it to remove it.");
}

int CustomPedalPanel::BlockRow::numTiles() const
{
    const auto count = (int) owner.blocks.size();
    return count < CustomPedal::maxBlocks ? count + 1 : count;
}

bool CustomPedalPanel::BlockRow::isAddTile (int tile) const
{
    return tile >= (int) owner.blocks.size();
}

juce::Rectangle<int> CustomPedalPanel::BlockRow::getTileBounds (int tile) const
{
    const auto count = (int) owner.blocks.size();
    const auto width = juce::jmin (tileWidth, (getWidth() - addTileWidth - tileGap * count) / juce::jmax (1, count));
    const auto x = tile * (width + tileGap);
    return { x, 0, isAddTile (tile) ? addTileWidth : width, getHeight() };
}

int CustomPedalPanel::BlockRow::findTileAt (juce::Point<int> position) const
{
    for (int i = 0; i < numTiles(); ++i)
        if (getTileBounds (i).contains (position))
            return i;

    return -1;
}

int CustomPedalPanel::BlockRow::findDropPosition (int x) const
{
    int best = 0, bestDistance = std::numeric_limits<int>::max();

    for (int i = 0; i <= (int) owner.blocks.size(); ++i)
    {
        const auto edge = i < (int) owner.blocks.size() ? getTileBounds (i).getX() - tileGap / 2
                                                        : getTileBounds (i - 1).getRight() + tileGap / 2;

        if (std::abs (edge - x) < bestDistance)
        {
            bestDistance = std::abs (edge - x);
            best = i;
        }
    }

    return best;
}

void CustomPedalPanel::BlockRow::paintTile (juce::Graphics& g, int tile, juce::Rectangle<float> area) const
{
    if (isAddTile (tile))
    {
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawRoundedRectangle (area.reduced (0.75f), 6.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.setFont (juce::FontOptions (20.0f));
        g.drawText ("+", area, juce::Justification::centred);
        return;
    }

    const auto slot = owner.blocks[(size_t) tile];
    const bool selected = slot == owner.selectedSlot;
    const auto type = owner.pedal.getSlot (slot).getType();

    g.setColour (selected ? Theme::raised : juce::Colour (0xff313a42));
    g.fillRoundedRectangle (area, 6.0f);

    if (selected)
    {
        g.setColour (Theme::accent);
        g.drawRoundedRectangle (area.reduced (0.75f), 6.0f, 1.5f);
    }

    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawFittedText (custom_blocks::getShortName (type), area.toNearestInt().reduced (4, 0),
                      juce::Justification::centred, 1, 0.7f);
}

void CustomPedalPanel::BlockRow::paint (juce::Graphics& g)
{
    for (int i = 0; i < numTiles(); ++i)
    {
        const auto bounds = getTileBounds (i).toFloat();

        if (drag.has_value() && drag->moved && drag->position == i)
        {
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.drawRoundedRectangle (bounds.reduced (0.75f), 6.0f, 1.0f);
        }
        else
        {
            paintTile (g, i, bounds);
        }

        // Arrows show the direction the signal flows.
        if (i + 1 < (int) owner.blocks.size())
        {
            const auto x = bounds.getRight() + (float) tileGap * 0.5f;
            const auto y = bounds.getCentreY();
            juce::Path arrow;
            arrow.addTriangle (x - 2.5f, y - 4.0f, x - 2.5f, y + 4.0f, x + 2.5f, y);
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.fillPath (arrow);
        }
    }

    if (drag.has_value() && drag->moved)
    {
        auto floating = getTileBounds (drag->position).toFloat();
        floating.setX ((float) (drag->mouseX - drag->grabOffset));
        g.setOpacity (0.6f);
        paintTile (g, drag->position, floating);
        g.setOpacity (1.0f);

        const auto drop = findDropPosition (drag->mouseX);
        const auto x = drop < (int) owner.blocks.size() ? getTileBounds (drop).getX() - tileGap / 2
                                                        : getTileBounds (drop - 1).getRight() + tileGap / 2;
        g.setColour (Theme::accent);
        g.fillRoundedRectangle ((float) x - 1.5f, 0.0f, 3.0f, (float) getHeight(), 1.5f);
    }
}

void CustomPedalPanel::BlockRow::mouseDown (const juce::MouseEvent& event)
{
    drag.reset();
    const auto tile = findTileAt (event.getPosition());

    if (tile < 0)
        return;

    if (isAddTile (tile))
    {
        owner.showAddMenu (localAreaToGlobal (getTileBounds (tile)));
        return;
    }

    const auto slot = owner.blocks[(size_t) tile];

    if (event.mods.isPopupMenu())
    {
        owner.showBlockMenu (slot);
        return;
    }

    if (slot != owner.selectedSlot)
        owner.selectBlock (slot);

    drag = Drag { tile, event.x - getTileBounds (tile).getX(), event.x, false };
    repaint();
}

void CustomPedalPanel::BlockRow::mouseDrag (const juce::MouseEvent& event)
{
    if (! drag.has_value())
        return;

    drag->mouseX = event.x;
    drag->moved = drag->moved || event.getDistanceFromDragStart() > 5;
    repaint();
}

void CustomPedalPanel::BlockRow::mouseUp (const juce::MouseEvent&)
{
    if (! drag.has_value())
        return;

    const auto finished = *drag;
    drag.reset();
    repaint();

    if (! finished.moved)
        return;

    auto to = findDropPosition (finished.mouseX);

    // The position is counted after the block has left its old place.
    if (to > finished.position)
        --to;

    if (to != finished.position)
    {
        owner.pedal.moveBlock (finished.position, to);
        owner.refreshBlocks();
    }
}
