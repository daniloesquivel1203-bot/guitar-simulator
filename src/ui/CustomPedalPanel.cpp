#include "CustomPedalPanel.h"

#include "Dialogs.h"
#include "Theme.h"

namespace
{
using custom_blocks::BlockType;

constexpr int tileWidth = 72;
constexpr int addTileWidth = 30;
constexpr int tileGap = 10;
constexpr int nodeWidth = 46;      // the Split and Path B markers
constexpr int laneHeight = 44;
constexpr int laneGap = 10;
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
CustomPedalPanel::ResponseGraph::ResponseGraph()
{
    setTooltip ("How the filter, gain and all-pass blocks, and any split paths, change the level across the frequency range, "
                "from 20 Hz on the left to 20 kHz on the right. The middle line is no change; "
                "the dotted lines are 12 dB up and down.");
}

const std::vector<float>& CustomPedalPanel::ResponseGraph::getFrequencies()
{
    static const auto frequencies = []
    {
        std::vector<float> f;

        for (int i = 0; i < 160; ++i)
            f.push_back (20.0f * std::pow (1000.0f, (float) i / 159.0f));

        return f;
    }();

    return frequencies;
}

void CustomPedalPanel::ResponseGraph::setCurve (std::vector<float> newDb, bool show)
{
    db = std::move (newDb);
    showing = show;
    repaint();
}

void CustomPedalPanel::ResponseGraph::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (Theme::sunken);
    g.fillRoundedRectangle (area, 4.0f);

    constexpr float range = 24.0f;   // dB above and below the middle
    const auto yFor = [&] (float value) { return area.getCentreY() - juce::jlimit (-range, range, value) / range * area.getHeight() * 0.5f; };
    const auto xFor = [&] (float hz) { return area.getX() + std::log (hz / 20.0f) / std::log (1000.0f) * area.getWidth(); };

    g.setColour (juce::Colours::white.withAlpha (0.08f));

    for (auto hz : { 100.0f, 1000.0f, 10000.0f })
        g.drawVerticalLine (juce::roundToInt (xFor (hz)), area.getY(), area.getBottom());

    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawHorizontalLine (juce::roundToInt (yFor (0.0f)), area.getX(), area.getRight());

    const float dashes[] = { 2.0f, 3.0f };

    for (auto level : { 12.0f, -12.0f })
        g.drawDashedLine (juce::Line<float> (area.getX(), yFor (level), area.getRight(), yFor (level)), dashes, 2, 1.0f);

    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("100", juce::Rectangle<float> (xFor (100.0f) + 2.0f, area.getBottom() - 12.0f, 30.0f, 12.0f), juce::Justification::left);
    g.drawText ("1k", juce::Rectangle<float> (xFor (1000.0f) + 2.0f, area.getBottom() - 12.0f, 30.0f, 12.0f), juce::Justification::left);
    g.drawText ("10k", juce::Rectangle<float> (xFor (10000.0f) + 2.0f, area.getBottom() - 12.0f, 30.0f, 12.0f), juce::Justification::left);

    if (! showing || db.empty())
    {
        g.drawText ("Add a filter to see its curve", area, juce::Justification::centred);
        return;
    }

    const auto& frequencies = getFrequencies();
    juce::Path curve;

    for (size_t i = 0; i < db.size() && i < frequencies.size(); ++i)
    {
        const juce::Point<float> point (xFor (frequencies[i]), yFor (db[i]));

        if (i == 0)
            curve.startNewSubPath (point);
        else
            curve.lineTo (point);
    }

    g.saveState();
    g.reduceClipRegion (getLocalBounds());
    g.setColour (Theme::accent);
    g.strokePath (curve, juce::PathStrokeType (1.75f));
    g.restoreState();
}

//==============================================================================
CustomPedalPanel::CustomPedalPanel (CustomPedal& pedalToUse, std::function<void()> onRemovePedal,
                                    CustomPedalLibrary& libraryToUse, std::function<void()> onRenamedToUse)
    : TonePanel (pedalToUse.label.isNotEmpty() ? pedalToUse.label.toUpperCase() + "  (CUSTOM PEDAL)" : juce::String ("CUSTOM PEDAL"),
                 &pedalToUse.enabled),
      pedal (pedalToUse),
      power (pedal.enabled),
      library (libraryToUse),
      onRenamed (std::move (onRenamedToUse)),
      level (pedal.level)
{
    addAndMakeVisible (power);

    if (onRemovePedal != nullptr)
    {
        removePedalButton.setTooltip ("Take this pedal off the board.");
        removePedalButton.onClick = std::move (onRemovePedal);
        addAndMakeVisible (removePedalButton);
    }

    saveButton.setTooltip ("Save this pedal under a name of your own. It then appears in the + menu under My pedals.");
    saveButton.onClick = [this] { saveAsMyPedal(); };
    addAndMakeVisible (saveButton);

    addAndMakeVisible (blockRow);
    addAndMakeVisible (graph);
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

    targetCaption.setText ("Moves", juce::dontSendNotification);
    targetCaption.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    targetCaption.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    addChildComponent (targetCaption);

    targetBox.setTooltip ("The block whose main knob this modulator moves.");
    targetBox.onChange = [this]
    {
        if (selectedSlot >= 0 && targetBox.getSelectedId() > 0)
            pedal.setTarget (selectedSlot, targetBox.getSelectedId() - 2);

        blockRow.repaint();
    };
    addChildComponent (targetBox);

    removeBlockButton.setTooltip ("Take the selected block out of this pedal.");
    removeBlockButton.onClick = [this] { removeBlock (selectedSlot); };
    addChildComponent (removeBlockButton);

    refreshBlocks();
}

CustomPedalPanel::~CustomPedalPanel() = default;

void CustomPedalPanel::saveAsMyPedal()
{
    dialogs::askForText ("Save as my pedal", "Name this pedal. It will appear in the + menu under My pedals.", pedal.label,
                         [safe = juce::Component::SafePointer<CustomPedalPanel> (this)] (juce::String name)
                         {
                             if (safe == nullptr)
                                 return;

                             const auto doSave = [safe, name]
                             {
                                 if (safe == nullptr)
                                     return;

                                 if (const auto error = safe->library.save (name, safe->pedal); error.isNotEmpty())
                                     return dialogs::showMessage ("Could not save the pedal", error);

                                 if (safe->onRenamed != nullptr)
                                     safe->onRenamed();
                             };

                             if (safe->library.getNames().contains (name.trim(), true) && name.trim() != safe->pedal.label)
                                 dialogs::confirm ("Replace " + name.trim() + "?", "You already have a pedal with that name.", "Replace", doSave);
                             else
                                 doSave();
                         });
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
    widthKnobs.clear();
    choices.clear();
    switches.clear();

    const bool hasBlock = slot >= 0;
    hint.setVisible (blocks.empty());
    description.setVisible (hasBlock);
    removeBlockButton.setVisible (hasBlock);
    bandRange.setVisible (false);
    targetBox.setVisible (false);
    targetCaption.setVisible (false);

    if (hasBlock)
    {
        auto& s = pedal.getSlot (slot);
        description.setText (custom_blocks::getDescription (s.getType()), juce::dontSendNotification);

        for (const auto& control : CustomPedal::getControlsFor (s, s.getType()))
        {
            if (control.parameter == &s.target)
            {
                refreshTargetChoices();
                targetBox.setVisible (true);
                targetCaption.setVisible (true);
            }
            else if (control.parameter->isChoice())
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

    startTimerHz (15);

    resized();
}

void CustomPedalPanel::refreshTargetChoices()
{
    targetBox.clear (juce::dontSendNotification);
    targetBox.addItem ("Nothing", 1);

    for (int position = 0; position < (int) blocks.size(); ++position)
    {
        const auto slot = blocks[(size_t) position];
        const auto type = pedal.getSlot (slot).getType();
        const juce::String knob (CustomPedal::getModulatedKnobName (type));

        if (slot != selectedSlot && knob.isNotEmpty())
            targetBox.addItem (juce::String (position + 1) + ": " + custom_blocks::getShortName (type) + " " + knob, slot + 2);
    }

    const auto target = pedal.getSlot (selectedSlot).getTargetSlot();
    targetBox.setSelectedId (target >= 0 ? target + 2 : 1, juce::dontSendNotification);
}

void CustomPedalPanel::refreshGraph()
{
    // Only recalculated when a knob that shapes the curve has moved.
    juce::String key;

    for (auto slot : blocks)
    {
        auto& s = pedal.getSlot (slot);
        key << (int) s.getType() << ':' << s.frequency.get() << ':' << s.resonance.get() << ':'
            << s.gainDb.get() << ':' << s.slope.getIndex() << ':' << (int) s.invert.isOn() << ':'
            << s.mix.get() << ':' << s.stages.getIndex() << ';';
    }

    if (key == lastResponseKey)
        return;

    lastResponseKey = key;
    bool hasFilters = false;
    auto curve = pedal.getResponseDb (ResponseGraph::getFrequencies(), hasFilters);
    graph.setCurve (std::move (curve), hasFilters);
}

void CustomPedalPanel::timerCallback()
{
    refreshGraph();

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
    header.removeFromRight (8);
    saveButton.setBounds (header.removeFromRight (140));

    auto area = getContentArea();

    // The pedal's overall level sits on the right, apart from the blocks.
    auto levelArea = area.removeFromRight (ParameterKnob::preferredWidth);
    level.setBounds (levelArea.withSizeKeepingCentre (ParameterKnob::preferredWidth,
                                                      juce::jmin (ParameterKnob::preferredHeight, levelArea.getHeight())));
    area.removeFromRight (16);

    blockRow.setBounds (area.removeFromTop (blockRow.getPreferredHeight()));
    area.removeFromTop (8);
    hint.setBounds (area.withHeight (juce::jmin (60, area.getHeight())));

    // Below the row: a description and a Remove button on the left, then the block's controls.
    auto side = area.removeFromLeft (200);
    removeBlockButton.setBounds (side.removeFromBottom (28).removeFromLeft (120));
    side.removeFromBottom (6);
    bandRange.setBounds (side.removeFromBottom (22));
    side.removeFromBottom (2);
    graph.setBounds (side.removeFromBottom (juce::jlimit (60, 110, side.getHeight() - 56)));
    side.removeFromBottom (6);
    description.setBounds (side);
    area.removeFromLeft (16);

    const auto numTargetRows = targetBox.isVisible() ? 1 : 0;

    if (! switches.isEmpty() || ! choices.isEmpty() || numTargetRows > 0)
    {
        auto column = area.removeFromRight (150);
        column = column.withSizeKeepingCentre (column.getWidth(),
                                               (choices.size() + numTargetRows) * ParameterChoice::preferredHeight
                                                 + switches.size() * 32);

        if (numTargetRows > 0)
        {
            auto row = column.removeFromTop (ParameterChoice::preferredHeight).withTrimmedBottom (4);
            targetCaption.setBounds (row.removeFromTop (18));
            targetBox.setBounds (row.removeFromTop (26));
        }

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

        if (type == BlockType::gain || type == BlockType::lfo)
            menu.addSeparator();

        // A Split brings its lower path and its + along, so those are not offered alone.
        if (type == BlockType::pathB || type == BlockType::mix)
            continue;

        if (type == BlockType::split)
            menu.addItem (t, custom_blocks::getTypeName (type), (int) blocks.size() <= CustomPedal::maxBlocks - 3);
        else
            menu.addItem (t, custom_blocks::getTypeName (type));
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (screenArea),
                        [safe = juce::Component::SafePointer<CustomPedalPanel> (this)] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;

                            const auto slot = (BlockType) result == BlockType::split ? safe->pedal.addSplit()
                                                                                     : safe->pedal.addBlock ((BlockType) result);

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

bool CustomPedalPanel::BlockRow::hasSplit() const
{
    return std::any_of (owner.blocks.begin(), owner.blocks.end(),
                        [this] (int slot) { return owner.pedal.getSlot (slot).getType() == BlockType::split; });
}

int CustomPedalPanel::BlockRow::getPreferredHeight() const
{
    return hasSplit() ? 2 * laneHeight + laneGap : 48;
}

CustomPedalPanel::BlockRow::Layout CustomPedalPanel::BlockRow::makeLayout() const
{
    // Shrink the blocks when the row would run past the edge.
    const auto widest = makeLayout (tileWidth);
    const auto right = widest.tiles.empty() ? 0 : widest.tiles.back().getRight();

    if (right <= getWidth())
        return widest;

    const auto narrowest = makeLayout (0).tiles.back().getRight();
    const auto perPixel = (float) (right - narrowest) / (float) tileWidth;
    const auto width = perPixel > 0.0f ? (int) ((float) (getWidth() - narrowest) / perPixel) : tileWidth;
    return makeLayout (juce::jlimit (28, tileWidth, width));
}

CustomPedalPanel::BlockRow::Layout CustomPedalPanel::BlockRow::makeLayout (int blockWidth) const
{
    // The main line runs along the top lane. Between a Split and its Mix, path A
    // stays on top and path B runs below, joining again at the Mix's + circle.
    // This follows how CustomPedal::process routes the sound.
    Layout layout;
    const bool twoLanes = hasSplit();
    const auto topLane = juce::Rectangle<int> (0, 0, getWidth(), twoLanes ? laneHeight : getHeight());
    const auto bottomLane = topLane.withY (laneHeight + laneGap);
    const auto bottomY = (float) bottomLane.getCentreY();

    int x = 0, xB = 0;
    bool inSplit = false, inPathB = false;
    std::optional<juce::Point<float>> lastTop, lastBottom;   // where the wire leaves the last tile in each lane

    const auto connect = [&layout] (juce::Point<float> from, juce::Point<float> to)
    {
        if (to.x - from.x < 1.0f && std::abs (to.y - from.y) < 1.0f)
            return;

        layout.wires.startNewSubPath (from);

        if (std::abs (to.y - from.y) > 0.5f)
        {
            // Across the lower lane, then up or down into the tile.
            layout.wires.lineTo (to.x, from.y);
            layout.wires.lineTo (to);
            layout.arrows.push_back ({ from, { to.x, from.y } });
        }
        else
        {
            layout.wires.lineTo (to);
            layout.arrows.push_back ({ from, to });
        }
    };

    const auto place = [&] (juce::Rectangle<int> bounds, Look look, bool lower)
    {
        layout.tiles.push_back (bounds);
        layout.looks.push_back (look);
        const auto b = bounds.toFloat();
        auto& last = lower ? lastBottom : lastTop;

        if (last.has_value())
            connect (*last, { b.getX(), b.getCentreY() });

        last = juce::Point<float> (b.getRight(), b.getCentreY());
    };

    for (auto slot : owner.blocks)
    {
        const auto type = owner.pedal.getSlot (slot).getType();
        const bool lower = inSplit && inPathB;
        const auto lane = lower ? bottomLane : topLane;
        auto& laneX = lower ? xB : x;

        if (type == BlockType::split && ! inSplit)
        {
            place ({ x, lane.getY(), nodeWidth, lane.getHeight() }, Look::node, false);
            x += nodeWidth + tileGap;

            // Path B leaves the Split downwards.
            const auto branch = juce::Point<float> ((float) x - (float) tileGap - nodeWidth * 0.5f, (float) lane.getBottom());
            layout.wires.startNewSubPath (branch);
            layout.wires.lineTo (branch.x, bottomY);
            lastBottom = juce::Point<float> (branch.x, bottomY);
            xB = x;
            inSplit = true;
            inPathB = false;
        }
        else if (type == BlockType::pathB && inSplit && ! inPathB)
        {
            // The marker for where the lower path starts is not drawn: the
            // lane itself shows it, and blocks are dragged onto it.
            inPathB = true;
            layout.tiles.push_back ({ xB, bottomLane.getY(), 0, bottomLane.getHeight() });
            layout.looks.push_back (Look::hidden);
        }
        else if (type == BlockType::mix && inSplit)
        {
            const auto size = juce::jmin (laneHeight, 40);
            x = juce::jmax (x, xB + 8);
            const juce::Rectangle<int> circle (x, topLane.getCentreY() - size / 2, size, size);
            place (circle, Look::mixer, false);

            // Path B comes up into the bottom of the +.
            if (lastBottom.has_value())
            {
                const auto into = juce::Point<float> ((float) circle.getCentreX(), (float) circle.getBottom());
                layout.wires.startNewSubPath (*lastBottom);
                layout.wires.lineTo (into.x, lastBottom->y);
                layout.wires.lineTo (into);
                layout.arrows.push_back ({ *lastBottom, { into.x, lastBottom->y } });
            }

            lastBottom.reset();
            x += size + tileGap;
            inSplit = inPathB = false;
        }
        else
        {
            // A block, or a marker that does nothing where it is.
            const bool marker = custom_blocks::isRouting (type);
            const auto width = marker ? nodeWidth : blockWidth;
            place ({ laneX, lane.getY(), width, lane.getHeight() }, marker ? Look::node : Look::block, lower);
            laneX += width + tileGap;
        }
    }

    if (inSplit)
        x = juce::jmax (x, xB);

    // With no Path B, the lower path is the clean signal and is drawn as a bare wire.
    if (inSplit && lastBottom.has_value())
    {
        layout.wires.startNewSubPath (*lastBottom);
        layout.wires.lineTo ((float) x - tileGap * 0.5f, lastBottom->y);
    }

    if (numTiles() > (int) owner.blocks.size())
    {
        layout.tiles.push_back ({ x, topLane.getY(), addTileWidth, topLane.getHeight() });
        layout.looks.push_back (Look::add);
    }

    return layout;
}

int CustomPedalPanel::BlockRow::findTileAt (const Layout& layout, juce::Point<int> position) const
{
    for (int i = 0; i < (int) layout.tiles.size(); ++i)
        if (layout.looks[(size_t) i] != Look::hidden && layout.tiles[(size_t) i].contains (position))
            return i;

    return -1;
}

juce::Rectangle<int> CustomPedalPanel::BlockRow::getDropMarker (const Layout& layout, int position) const
{
    const auto count = (int) owner.blocks.size();

    // Dropping just before a Path B marker or a Mix lands at the end of the
    // path before it, so the marker goes there rather than in the other lane.
    const auto endsAPath = [&] (int tile)
    {
        return layout.looks[(size_t) tile] == Look::mixer
            || (owner.pedal.getSlot (owner.blocks[(size_t) tile]).getType() == BlockType::pathB
                && layout.tiles[(size_t) tile].getY() > 0);
    };

    if (position > 0 && position < count && endsAPath (position))
    {
        const auto& last = layout.tiles[(size_t) (position - 1)];
        return { last.getRight() + tileGap / 2 - 1, last.getY(), 3, last.getHeight() };
    }

    if (position < count)
    {
        const auto& tile = layout.tiles[(size_t) position];
        return { tile.getX() - tileGap / 2 - 1, tile.getY(), 3, tile.getHeight() };
    }

    const auto& last = layout.tiles[(size_t) (count - 1)];
    return { last.getRight() + tileGap / 2 - 1, last.getY(), 3, last.getHeight() };
}

int CustomPedalPanel::BlockRow::findDropPosition (const Layout& layout, juce::Point<int> point) const
{
    int best = 0;
    auto bestDistance = std::numeric_limits<float>::max();

    for (int i = 0; i <= (int) owner.blocks.size(); ++i)
    {
        const auto marker = getDropMarker (layout, i).toFloat();
        const auto distance = std::abs (marker.getCentreX() - (float) point.x)
                              + 2.0f * juce::jmax (0.0f, std::abs (marker.getCentreY() - (float) point.y) - marker.getHeight() * 0.5f);

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }

    return best;
}

void CustomPedalPanel::BlockRow::paintTile (juce::Graphics& g, int tile, juce::Rectangle<float> area, Look look) const
{
    if (look == Look::add)
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

    const bool modulator = custom_blocks::isModulator (type);

    if (look == Look::mixer)
    {
        // The Mix joins the two paths: a + in a circle.
        g.setColour (selected ? Theme::raised : juce::Colour (0xff313a42));
        g.fillEllipse (area);
        g.setColour (selected ? Theme::accent : juce::Colours::white.withAlpha (0.45f));
        g.drawEllipse (area.reduced (0.75f), selected ? 1.5f : 1.0f);
        const auto c = area.getCentre();
        const auto arm = area.getWidth() * 0.22f;
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawLine (c.x - arm, c.y, c.x + arm, c.y, 2.0f);
        g.drawLine (c.x, c.y - arm, c.x, c.y + arm, 2.0f);
        return;
    }

    g.setColour (selected ? Theme::raised : juce::Colour (look == Look::node ? 0xff2e363d : 0xff313a42));
    g.fillRoundedRectangle (area, 6.0f);

    if (look == Look::node)
    {
        if (selected)
        {
            g.setColour (Theme::accent);
            g.drawRoundedRectangle (area.reduced (0.75f), 6.0f, 1.5f);
        }

        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawFittedText (custom_blocks::getShortName (type), area.toNearestInt().reduced (2, 0), juce::Justification::centred, 2, 0.8f);
        return;
    }

    // Modulators make no sound, so they are drawn with a dashed outline.
    if (modulator && ! selected)
    {
        juce::Path outline, dashed;
        outline.addRoundedRectangle (area.reduced (0.75f), 6.0f);
        const float dashes[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (Theme::warning.withAlpha (0.7f));
        g.fillPath (dashed);
    }

    if (selected)
    {
        g.setColour (Theme::accent);
        g.drawRoundedRectangle (area.reduced (0.75f), 6.0f, 1.5f);
    }

    auto text = area.toNearestInt().reduced (4, 0);
    const auto target = modulator ? owner.pedal.getSlot (slot).getTargetSlot() : -1;
    const auto targetPosition = (int) (std::find (owner.blocks.begin(), owner.blocks.end(), target) - owner.blocks.begin());
    const bool showTarget = target >= 0 && targetPosition < (int) owner.blocks.size();

    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawFittedText (custom_blocks::getShortName (type), showTarget ? text.removeFromTop (text.getHeight() / 2 + 2) : text,
                      showTarget ? juce::Justification::centredBottom : juce::Justification::centred, 1, 0.7f);

    if (showTarget)
    {
        // Which block it moves, by its position in the row.
        g.setColour (Theme::warning);
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92 ")) + juce::String (targetPosition + 1) + " "
                            + custom_blocks::getShortName (owner.pedal.getSlot (target).getType()),
                          text, juce::Justification::centredTop, 1, 0.7f);
    }
}

void CustomPedalPanel::BlockRow::paint (juce::Graphics& g)
{
    const auto layout = makeLayout();

    // Wires first, so the tiles sit on top of them.
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.strokePath (layout.wires, juce::PathStrokeType (1.5f));

    for (const auto& line : layout.arrows)
    {
        if (line.getLength() < 6.0f)
            continue;

        // Arrows show the direction the signal flows.
        const auto x = (line.getStartX() + line.getEndX()) * 0.5f;
        const auto y = line.getStartY();
        juce::Path arrow;
        arrow.addTriangle (x - 2.5f, y - 4.0f, x - 2.5f, y + 4.0f, x + 2.5f, y);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.fillPath (arrow);
    }

    for (int i = 0; i < (int) layout.tiles.size(); ++i)
    {
        const auto bounds = layout.tiles[(size_t) i].toFloat();

        if (layout.looks[(size_t) i] == Look::hidden)
            continue;

        if (drag.has_value() && drag->moved && drag->position == i)
        {
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.drawRoundedRectangle (bounds.reduced (0.75f), 6.0f, 1.0f);
        }
        else
        {
            paintTile (g, i, bounds, layout.looks[(size_t) i]);
        }
    }

    if (drag.has_value() && drag->moved)
    {
        const auto floating = layout.tiles[(size_t) drag->position].withPosition (drag->mouse - drag->grabOffset).toFloat();
        g.setOpacity (0.6f);
        paintTile (g, drag->position, floating, layout.looks[(size_t) drag->position]);
        g.setOpacity (1.0f);

        g.setColour (Theme::accent);
        g.fillRoundedRectangle (getDropMarker (layout, findDropPosition (layout, drag->mouse)).toFloat(), 1.5f);
    }
}

void CustomPedalPanel::BlockRow::mouseDown (const juce::MouseEvent& event)
{
    drag.reset();
    const auto layout = makeLayout();
    const auto tile = findTileAt (layout, event.getPosition());

    if (tile < 0)
        return;

    if (isAddTile (tile))
    {
        owner.showAddMenu (localAreaToGlobal (layout.tiles[(size_t) tile]));
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

    drag = Drag { tile, event.getPosition() - layout.tiles[(size_t) tile].getPosition(), event.getPosition(), false };
    repaint();
}

void CustomPedalPanel::BlockRow::mouseDrag (const juce::MouseEvent& event)
{
    if (! drag.has_value())
        return;

    drag->mouse = event.getPosition();
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

    auto to = findDropPosition (makeLayout(), finished.mouse);

    // The position is counted after the block has left its old place.
    if (to > finished.position)
        --to;

    if (to != finished.position)
    {
        owner.pedal.moveBlock (finished.position, to);
        owner.refreshBlocks();
    }
}
