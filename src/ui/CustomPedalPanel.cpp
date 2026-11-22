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
/** The Width knob of the band-pass, notch and peak blocks. Underneath it is
    the filter's Q, but it reads as the width of the affected band in Hz at
    the block's current frequency (about frequency / Q), and turning it right
    makes the band wider. */
class CustomPedalPanel::WidthKnob final : public juce::Component,
                                          private juce::Timer
{
public:
    WidthKnob (Parameter& qToUse, Parameter& frequencyToUse)
        : q (qToUse), frequency (frequencyToUse)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 84, 20);
        slider.setTooltip ("Width: how wide a band of frequencies around the centre this block works on. "
                           "Turn right for wider. Drag, scroll, or type a width in Hz. Double-click to reset.");
        slider.setRange (0.0, 1.0);
        slider.textFromValueFunction = [this] (double position) { return formatHz (frequency.get() / qFromPosition (position)); };
        slider.valueFromTextFunction = [this] (const juce::String& text)
        {
            auto hz = text.retainCharacters ("0123456789.").getDoubleValue();

            if (text.containsIgnoreCase ("k"))
                hz *= 1000.0;

            return hz > 0.0 ? positionFromQ (frequency.get() / hz) : slider.getValue();
        };
        slider.setDoubleClickReturnValue (true, positionFromQ (q.defaultValue));
        slider.setValue (positionFromQ (q.get()), juce::dontSendNotification);
        slider.onValueChange = [this] { q.set ((float) qFromPosition (slider.getValue())); };
        addAndMakeVisible (slider);

        caption.setText ("Width", juce::dontSendNotification);
        caption.setJustificationType (juce::Justification::centred);
        caption.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        caption.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
        addAndMakeVisible (caption);

        lastFrequency = frequency.get();
        startTimerHz (15);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        caption.setBounds (area.removeFromTop (18));
        slider.setBounds (area);
    }

private:
    /** Position 0 is the narrowest band (highest Q), 1 the widest (lowest Q), spread by ratio. */
    double qFromPosition (double position) const
    {
        const auto narrow = std::log ((double) q.maxValue), wide = std::log ((double) q.minValue);
        return std::exp (narrow + juce::jlimit (0.0, 1.0, position) * (wide - narrow));
    }

    double positionFromQ (double value) const
    {
        const auto narrow = std::log ((double) q.maxValue), wide = std::log ((double) q.minValue);
        return juce::jlimit (0.0, 1.0, (std::log (juce::jmax (1.0e-3, value)) - narrow) / (wide - narrow));
    }

    static juce::String formatHz (double hz)
    {
        if (hz >= 1000.0)
            return juce::String (hz / 1000.0, hz >= 10000.0 ? 0 : 1) + " kHz";

        return juce::String (juce::roundToInt (hz)) + " Hz";
    }

    /** The width in Hz depends on the Frequency knob too, so it follows it. */
    void timerCallback() override
    {
        if (! juce::exactlyEqual (frequency.get(), lastFrequency))
        {
            lastFrequency = frequency.get();
            slider.updateText();
        }
    }

    Parameter& q;
    Parameter& frequency;
    juce::Slider slider;
    juce::Label caption;
    float lastFrequency = 0.0f;
};

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

    bandRange.setJustificationType (juce::Justification::topLeft);
    bandRange.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
    bandRange.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    bandRange.setTooltip ("The range of frequencies this block works on. At the edges its effect has dropped to about half.");
    addChildComponent (bandRange);

    removeBlockButton.setTooltip ("Take the selected block out of this pedal.");
    removeBlockButton.onClick = [this] { removeBlock (selectedSlot); };
    addChildComponent (removeBlockButton);

    refreshBlocks();
}

CustomPedalPanel::~CustomPedalPanel() = default;

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
    widthKnobs.clear();
    choices.clear();
    switches.clear();

    const bool hasBlock = slot >= 0;
    hint.setVisible (blocks.empty());
    description.setVisible (hasBlock);
    removeBlockButton.setVisible (hasBlock);
    bandRange.setVisible (false);

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
            else if (control.parameter == &s.resonance && juce::String (control.caption) == "Width")
                addAndMakeVisible (widthKnobs.add (new WidthKnob (s.resonance, s.frequency)));
            else
                addAndMakeVisible (knobs.add (new ParameterKnob (*control.parameter, control.caption)));
        }
    }

    blockRow.repaint();
    timerCallback();

    if (widthKnobs.isEmpty())
        stopTimer();
    else
        startTimerHz (15);

    resized();
}

void CustomPedalPanel::timerCallback()
{
    if (selectedSlot < 0 || widthKnobs.isEmpty())
    {
        bandRange.setVisible (false);
        return;
    }

    // The edges of a band of width f / Q, spaced evenly by ratio around f
    // (the -3 dB points of a band-pass filter).
    auto& slot = pedal.getSlot (selectedSlot);
    const auto f = (double) slot.frequency.get();
    const auto q = (double) slot.resonance.get();
    const auto half = 1.0 / (2.0 * q);
    const auto root = std::sqrt (1.0 + half * half);
    const auto low = f * (root - half);
    const auto high = juce::jmin (20000.0, f * (root + half));

    const auto format = [] (double hz)
    {
        return hz >= 1000.0 ? juce::String (hz / 1000.0, hz >= 10000.0 ? 0 : 2) + " kHz"
                            : juce::String (juce::roundToInt (hz)) + " Hz";
    };

    bandRange.setText ("Band: " + format (low) + " to " + format (high), juce::dontSendNotification);
    bandRange.setVisible (true);
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
    side.removeFromBottom (6);
    bandRange.setBounds (side.removeFromBottom (22));
    description.setBounds (side);
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

    for (auto* knob : widthKnobs)
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
