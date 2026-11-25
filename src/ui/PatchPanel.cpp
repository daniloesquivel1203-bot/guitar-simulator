#include "PatchPanel.h"

#include "Theme.h"

namespace
{
using custom_blocks::BlockType;

constexpr float nodeWidth = 72.0f;
constexpr float nodeHeight = 34.0f;
constexpr float pillWidth = 40.0f;
constexpr float pillHeight = 28.0f;
constexpr float portRadius = 4.5f;
constexpr float grabRadius = 10.0f;

const juce::Colour wireColour = juce::Colours::white.withAlpha (0.45f);
const juce::Colour loopColour = Theme::accent.withAlpha (0.8f);
const juce::Colour modColour = Theme::warning.withAlpha (0.8f);
const juce::Colour cutColour = juce::Colour (0xffe0605a);

bool makesNoSound (BlockType type)   { return custom_blocks::isModulator (type); }
}

//==============================================================================
PatchPanel::Canvas::Canvas (PatchPanel& ownerToUse)
    : owner (ownerToUse)
{
    setWantsKeyboardFocus (true);
    setTooltip ("Drag a block to move it. Drag between a block's right-hand dot and another block's left-hand dot, either way, to wire them. "
                "Click a wire to cut it. Right-click a block to remove it, or empty space to add one.");
}

juce::Rectangle<float> PatchPanel::Canvas::getInputPill() const
{
    return { 6.0f, (float) getHeight() * 0.5f - pillHeight * 0.5f, pillWidth, pillHeight };
}

juce::Rectangle<float> PatchPanel::Canvas::getOutputPill() const
{
    return { (float) getWidth() - 6.0f - pillWidth, (float) getHeight() * 0.5f - pillHeight * 0.5f, pillWidth, pillHeight };
}

juce::Rectangle<float> PatchPanel::Canvas::getNodeArea() const
{
    // Block centres sit between the In and Out pills, kept inside the canvas.
    const auto left = 6.0f + pillWidth + 14.0f + nodeWidth * 0.5f;
    const auto right = (float) getWidth() - 6.0f - pillWidth - 14.0f - nodeWidth * 0.5f;
    const auto top = 6.0f + nodeHeight * 0.5f;
    const auto bottom = (float) getHeight() - 6.0f - nodeHeight * 0.5f;
    return { left, top, juce::jmax (1.0f, right - left), juce::jmax (1.0f, bottom - top) };
}

juce::Point<float> PatchPanel::Canvas::toNodePosition (juce::Point<float> p) const
{
    const auto area = getNodeArea();
    return { juce::jlimit (0.0f, 1.0f, (p.x - area.getX()) / area.getWidth()),
             juce::jlimit (0.0f, 1.0f, (p.y - area.getY()) / area.getHeight()) };
}

juce::Point<float> PatchPanel::Canvas::findFreeSpot() const
{
    // The grid point furthest from every block, preferring the middle on a tie.
    const auto area = getNodeArea();
    juce::Point<float> best (0.5f, 0.5f);
    float bestScore = -1.0f;

    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 7; ++column)
        {
            const juce::Point<float> spot (0.2f + 0.1f * (float) ((column + 3) % 7), 0.5f + (row == 0 ? 0.0f : (row == 1 ? -0.4f : 0.4f)));
            const juce::Point<float> centre (area.getX() + spot.x * area.getWidth(), area.getY() + spot.y * area.getHeight());
            float nearest = 1.0e6f;

            for (auto n : owner.nodes)
            {
                const auto other = getNodeBounds (n).getCentre();
                // Blocks are wide, so distance across counts for less.
                nearest = juce::jmin (nearest, juce::Point<float> ((other.x - centre.x) * 0.5f, other.y - centre.y).getDistanceFromOrigin());
            }

            if (nearest > bestScore + 1.0f)
            {
                bestScore = nearest;
                best = spot;
            }
        }
    }

    return best;
}

juce::Rectangle<float> PatchPanel::Canvas::getNodeBounds (int index) const
{
    auto& node = owner.pedal.getNode (index);
    const auto area = getNodeArea();
    const juce::Point<float> centre (area.getX() + node.x.get() * area.getWidth(),
                                     area.getY() + node.y.get() * area.getHeight());

    if (node.getType() == BlockType::mix)
        return juce::Rectangle<float> (nodeHeight, nodeHeight).withCentre (centre);

    return juce::Rectangle<float> (nodeWidth, nodeHeight).withCentre (centre);
}

int PatchPanel::Canvas::numInputs (int index) const
{
    const auto type = owner.pedal.getNode (index).getType();

    if (type == BlockType::mix)
        return 2;

    if (type == BlockType::lfo)
        return 0;

    return 1;
}

bool PatchPanel::Canvas::hasOutput (int index) const
{
    return ! makesNoSound (owner.pedal.getNode (index).getType());
}

juce::Point<float> PatchPanel::Canvas::getOutputPort (int source) const
{
    if (source == PatchPedal::inputSource)
        return { getInputPill().getRight(), getInputPill().getCentreY() };

    const auto box = getNodeBounds (source);
    return { box.getRight(), box.getCentreY() };
}

juce::Point<float> PatchPanel::Canvas::getModPort (int index) const
{
    const auto box = getNodeBounds (index);
    return { box.getRight(), box.getCentreY() };
}

juce::Point<float> PatchPanel::Canvas::getInputPort (int index, int input) const
{
    const auto box = getNodeBounds (index);

    if (numInputs (index) == 2)
        return { box.getX() + 3.0f, box.getY() + box.getHeight() * (input == 0 ? 0.22f : 0.78f) };

    return { box.getX(), box.getCentreY() };
}

juce::Point<float> PatchPanel::Canvas::getOutPillPort() const
{
    return { getOutputPill().getX(), getOutputPill().getCentreY() };
}

PatchPanel::Canvas::Hit PatchPanel::Canvas::findHit (juce::Point<float> p) const
{
    const auto near = [p] (juce::Point<float> port) { return port.getDistanceFrom (p) <= grabRadius; };

    // Ports first, from the top-most block down, so a dot is easy to grab.
    for (auto it = owner.nodes.rbegin(); it != owner.nodes.rend(); ++it)
    {
        const auto n = *it;

        for (int input = 0; input < numInputs (n); ++input)
            if (near (getInputPort (n, input)))
                return { Hit::nodeInput, n, input };

        if (near (getOutputPort (n)))
            return { hasOutput (n) ? Hit::nodeOutput : Hit::modOutput, n, 0 };
    }

    if (near (getOutputPort (PatchPedal::inputSource)) || getInputPill().contains (p))
        return { Hit::inputPill, -1, 0 };

    if (near (getOutPillPort()) || getOutputPill().contains (p))
        return { Hit::outputPill, -1, 0 };

    for (auto it = owner.nodes.rbegin(); it != owner.nodes.rend(); ++it)
        if (getNodeBounds (*it).expanded (2.0f).contains (p))
            return { Hit::body, *it, 0 };

    // Last, the wires, so clicking near one picks it.
    Hit closest;
    float closestDistance = 6.0f;

    for (const auto& wire : getWires())
    {
        juce::Point<float> onWire;
        makeWire (wire.from, wire.to).getNearestPoint (p, onWire);

        if (const auto distance = onWire.getDistanceFrom (p); distance < closestDistance)
        {
            closestDistance = distance;
            closest = wire.id;
        }
    }

    return closest;
}

std::vector<PatchPanel::Canvas::Wire> PatchPanel::Canvas::getWires() const
{
    std::vector<Wire> wires;
    auto& pedal = owner.pedal;
    const auto inUse = [this] (int n) { return std::find (owner.nodes.begin(), owner.nodes.end(), n) != owner.nodes.end(); };

    // Sound wires. A loop (into a block that runs first) is drawn dashed in blue.
    for (auto n : owner.nodes)
    {
        for (int input = 0; input < numInputs (n); ++input)
        {
            const auto source = pedal.getNode (n).getSource (input);

            if (source == PatchPedal::noSource || (source >= 0 && (! inUse (source) || ! hasOutput (source))))
                continue;

            const bool loop = pedal.isLoopWire (n, input);
            wires.push_back ({ { Hit::wire, n, input }, getOutputPort (source), getInputPort (n, input),
                               loop ? loopColour : wireColour, loop });
        }
    }

    if (const auto out = pedal.getOutputSource(); out != PatchPedal::noSource && (out < 0 || inUse (out)))
        wires.push_back ({ { Hit::wire, -1, 0 }, getOutputPort (out), getOutPillPort(), wireColour, false });

    // Modulator links: dashed orange, into the top of the block they move.
    for (auto n : owner.nodes)
    {
        const auto target = pedal.getNode (n).block.getTargetSlot();

        if (makesNoSound (pedal.getNode (n).getType()) && target >= 0 && inUse (target))
        {
            const auto to = getNodeBounds (target);
            wires.push_back ({ { Hit::wire, n, -1 }, getModPort (n), { to.getCentreX(), to.getY() }, modColour, true });
        }
    }

    return wires;
}

void PatchPanel::Canvas::cutWire (const Hit& wire)
{
    auto& pedal = owner.pedal;

    if (wire.node < 0)
        pedal.connectOutput (PatchPedal::noSource);
    else if (wire.input < 0)
        pedal.setTarget (wire.node, -1);
    else
        pedal.connect (PatchPedal::noSource, wire.node, wire.input);

    owner.refreshTargetChoices();
    hover = {};
    repaint();
}

juce::Path PatchPanel::Canvas::makeWire (juce::Point<float> from, juce::Point<float> to)
{
    // A soft S-curve, which loops round nicely when it goes back to the left.
    const auto pull = juce::jmax (30.0f, std::abs (to.x - from.x) * 0.5f);
    juce::Path wire;
    wire.startNewSubPath (from);
    wire.cubicTo (from.translated (pull, 0.0f), to.translated (-pull, 0.0f), to);
    return wire;
}

void PatchPanel::Canvas::drawWire (juce::Graphics& g, juce::Point<float> from, juce::Point<float> to, juce::Colour colour, bool dashed)
{
    const auto wire = makeWire (from, to);

    g.setColour (colour);

    if (dashed)
    {
        juce::Path dashedWire;
        const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.6f).createDashedStroke (dashedWire, wire, dashes, 2);
        g.fillPath (dashedWire);
    }
    else
    {
        g.strokePath (wire, juce::PathStrokeType (1.6f));
    }
}

void PatchPanel::Canvas::paintNode (juce::Graphics& g, int index) const
{
    auto& node = owner.pedal.getNode (index);
    const auto type = node.getType();
    const auto box = getNodeBounds (index);
    const bool selected = index == owner.selectedNode;

    if (type == BlockType::mix)
    {
        g.setColour (selected ? Theme::raised : juce::Colour (0xff313a42));
        g.fillEllipse (box);
        g.setColour (selected ? Theme::accent : juce::Colours::white.withAlpha (0.45f));
        g.drawEllipse (box.reduced (0.75f), selected ? 1.5f : 1.0f);
        const auto c = box.getCentre();
        const auto arm = box.getWidth() * 0.22f;
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawLine (c.x - arm, c.y, c.x + arm, c.y, 2.0f);
        g.drawLine (c.x, c.y - arm, c.x, c.y + arm, 2.0f);
    }
    else
    {
        g.setColour (selected ? Theme::raised : juce::Colour (0xff313a42));
        g.fillRoundedRectangle (box, 6.0f);

        if (makesNoSound (type) && ! selected)
        {
            // Modulators make no sound, so they are drawn with a dashed outline.
            juce::Path outline, dashed;
            outline.addRoundedRectangle (box.reduced (0.75f), 6.0f);
            const float dashes[] = { 4.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (modColour);
            g.fillPath (dashed);
        }

        if (selected)
        {
            g.setColour (Theme::accent);
            g.drawRoundedRectangle (box.reduced (0.75f), 6.0f, 1.5f);
        }

        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawFittedText (custom_blocks::getShortName (type), box.toNearestInt().reduced (8, 0), juce::Justification::centred, 1, 0.7f);

        // Its number, which the "Moves" list uses.
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.setFont (juce::FontOptions (9.0f));
        g.drawText (juce::String (index + 1), box.reduced (4.0f, 2.0f), juce::Justification::topRight);
    }

    // The dots: inputs on the left, the output (or a modulator's orange dot) on the right.
    const auto dot = [&g] (juce::Point<float> at, juce::Colour colour, bool highlight)
    {
        const auto r = highlight ? portRadius + 1.5f : portRadius;
        g.setColour (Theme::panel);
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f + 2.0f, r * 2.0f + 2.0f).withCentre (at));
        g.setColour (colour);
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (at));
    };

    for (int input = 0; input < numInputs (index); ++input)
        dot (getInputPort (index, input), juce::Colours::white.withAlpha (0.75f),
             hover.kind == Hit::nodeInput && hover.node == index && hover.input == input);

    dot (getOutputPort (index), hasOutput (index) ? juce::Colours::white.withAlpha (0.75f) : Theme::warning,
         (hover.kind == Hit::nodeOutput || hover.kind == Hit::modOutput) && hover.node == index);
}

void PatchPanel::Canvas::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (Theme::sunken);
    g.fillRoundedRectangle (area, 6.0f);

    // In and Out.
    for (const auto& [pill, text] : { std::pair { getInputPill(), "In" }, std::pair { getOutputPill(), "Out" } })
    {
        g.setColour (juce::Colour (0xff313a42));
        g.fillRoundedRectangle (pill, pillHeight * 0.5f);
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (text, pill, juce::Justification::centred);
    }

    // The wire under the mouse is drawn red and thicker: a click cuts it.
    for (const auto& wire : getWires())
    {
        if (hover.kind == Hit::wire && hover == wire.id && ! wiring.has_value())
        {
            g.setColour (cutColour);
            g.strokePath (makeWire (wire.from, wire.to), juce::PathStrokeType (3.0f));
        }
        else
            drawWire (g, wire.from, wire.to, wire.colour, wire.dashed);
    }

    for (auto n : owner.nodes)
        paintNode (g, n);

    // The In and Out dots.
    const auto pillDot = [&g] (juce::Point<float> at, bool highlight)
    {
        const auto r = highlight ? portRadius + 1.5f : portRadius;
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (at));
    };

    pillDot (getOutputPort (PatchPedal::inputSource), hover.kind == Hit::inputPill);
    pillDot (getOutPillPort(), hover.kind == Hit::outputPill);

    if (wiring.has_value())
    {
        if (wiring->into.has_value())
        {
            const auto to = wiring->into->kind == Hit::outputPill ? getOutPillPort() : getInputPort (wiring->into->node, wiring->into->input);
            drawWire (g, wiring->end, to, Theme::accent, false);
        }
        else
        {
            const auto from = wiring->fromModulator ? getModPort (wiring->source) : getOutputPort (wiring->source);
            drawWire (g, from, wiring->end, wiring->fromModulator ? modColour : Theme::accent, wiring->fromModulator);
        }
    }

    if (owner.nodes.empty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Right-click here, or press Add block, to put your first block on the canvas.",
                    area.reduced (pillWidth + 20.0f, 0.0f).withTrimmedTop (area.getHeight() * 0.6f), juce::Justification::centredTop);
    }
}

void PatchPanel::Canvas::mouseMove (const juce::MouseEvent& event)
{
    const auto now = findHit (event.position);

    if (! (now == hover))
    {
        hover = now;
        setMouseCursor (now.kind == Hit::wire ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void PatchPanel::Canvas::mouseExit (const juce::MouseEvent&)
{
    hover = {};
    repaint();
}

void PatchPanel::Canvas::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    wiring.reset();
    moving.reset();
    const auto hit = findHit (event.position);
    auto& pedal = owner.pedal;

    if (hit.kind == Hit::wire)
    {
        cutWire (hit);
        return;
    }

    if (event.mods.isPopupMenu())
    {
        if (hit.node >= 0)
            owner.showNodeMenu (hit.node);
        else if (hit.kind == Hit::nothing)
            owner.showAddMenu (event.position);


        return;
    }

    switch (hit.kind)
    {
        case Hit::nodeOutput:
            wiring = Wiring { false, hit.node, event.position, std::nullopt };
            break;

        case Hit::modOutput:
            wiring = Wiring { true, hit.node, event.position, std::nullopt };
            break;

        case Hit::inputPill:
            wiring = Wiring { false, PatchPedal::inputSource, event.position, std::nullopt };
            break;

        case Hit::nodeInput:
        {
            // Pick up the wire plugged in here, to move it elsewhere or drop it.
            const auto source = pedal.getNode (hit.node).getSource (hit.input);

            if (source != PatchPedal::noSource)
            {
                pedal.connect (PatchPedal::noSource, hit.node, hit.input);
                wiring = Wiring { false, source, event.position, std::nullopt };
            }
            else
            {
                // An empty input: draw a wire back from it to an output.
                if (hit.node != owner.selectedNode)
                    owner.selectNode (hit.node);

                wiring = Wiring { false, PatchPedal::noSource, event.position, hit };
            }

            break;
        }

        case Hit::outputPill:
        {
            const auto source = pedal.getOutputSource();

            if (source != PatchPedal::noSource)
            {
                pedal.connectOutput (PatchPedal::noSource);
                wiring = Wiring { false, source, event.position, std::nullopt };
            }
            else
            {
                wiring = Wiring { false, PatchPedal::noSource, event.position, hit };
            }

            break;
        }

        case Hit::body:
        {
            if (hit.node != owner.selectedNode)
                owner.selectNode (hit.node);

            moving = Moving { hit.node, event.position - getNodeBounds (hit.node).getCentre(), false };
            break;
        }

        case Hit::wire:
        case Hit::nothing:
            break;
    }

    repaint();
}

void PatchPanel::Canvas::mouseDrag (const juce::MouseEvent& event)
{
    if (wiring.has_value())
    {
        wiring->end = event.position;
        hover = findHit (event.position);
        repaint();
        return;
    }

    if (moving.has_value())
    {
        moving->moved = moving->moved || event.getDistanceFromDragStart() > 3;

        if (! moving->moved)
            return;

        const auto position = toNodePosition (event.position - moving->grabOffset);
        auto& node = owner.pedal.getNode (moving->node);
        node.x.set (position.x);
        node.y.set (position.y);
        repaint();
    }
}

void PatchPanel::Canvas::mouseUp (const juce::MouseEvent& event)
{
    moving.reset();

    if (! wiring.has_value())
        return;

    const auto finished = *wiring;
    wiring.reset();
    finishWire (finished, findHit (event.position));
    repaint();
}

void PatchPanel::Canvas::finishWire (const Wiring& w, const Hit& target)
{
    auto& pedal = owner.pedal;

    if (w.into.has_value())
    {
        // Drawn backwards: dropped on an output (a block, or In) gives the source.
        const auto plugInto = [&] (int source)
        {
            if (w.into->kind == Hit::outputPill)
                pedal.connectOutput (source);
            else if (source != w.into->node)
                pedal.connect (source, w.into->node, w.into->input);
        };

        if (target.kind == Hit::inputPill)
            plugInto (PatchPedal::inputSource);
        else if (target.kind == Hit::modOutput && w.into->kind == Hit::nodeInput)
        {
            // A modulator's orange dot: let it move this block instead.
            pedal.setTarget (target.node, w.into->node);
            owner.refreshTargetChoices();
        }
        else if ((target.kind == Hit::nodeOutput || target.kind == Hit::body || target.kind == Hit::nodeInput)
                 && target.node >= 0 && hasOutput (target.node))
            plugInto (target.node);

        return;
    }

    if (w.fromModulator)
    {
        if (target.node >= 0 && target.node != w.source)
            pedal.setTarget (w.source, target.node);

        owner.refreshTargetChoices();
        return;
    }

    switch (target.kind)
    {
        case Hit::nodeInput:
            pedal.connect (w.source, target.node, target.input);
            break;

        case Hit::body:
        case Hit::nodeOutput:
        case Hit::modOutput:
        {
            // Dropped on a block: its input, or the nearer of a Mix's two.
            auto input = 0;

            if (numInputs (target.node) == 2)
                input = w.end.y > getNodeBounds (target.node).getCentreY() ? 1 : 0;

            if (numInputs (target.node) > 0)
                pedal.connect (w.source, target.node, input);

            break;
        }

        case Hit::outputPill:
            pedal.connectOutput (w.source);
            break;

        case Hit::inputPill:
        case Hit::wire:
        case Hit::nothing:
            break;
    }
}

bool PatchPanel::Canvas::keyPressed (const juce::KeyPress& key)
{
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && owner.selectedNode >= 0)
    {
        owner.removeNode (owner.selectedNode);
        return true;
    }

    return false;
}

//==============================================================================
PatchPanel::PatchPanel (PatchPedal& pedalToUse, std::function<void()> onRemovePedal)
    : TonePanel ("PATCH PEDAL", &pedalToUse.enabled),
      pedal (pedalToUse),
      power (pedal.enabled),
      level (pedal.level)
{
    addAndMakeVisible (power);

    removePedal = std::move (onRemovePedal);

    removeBlockButton.setTooltip ("Remove the selected block. (To take the whole pedal off the board, right-click "
                                  "empty space on the canvas.)");
    removeBlockButton.onClick = [this] { removeNode (selectedNode); };
    addAndMakeVisible (removeBlockButton);

    addButton.setTooltip ("Put a new block in an empty spot on the canvas. It floats until you wire it.");
    addButton.onClick = [this] { showAddMenu (std::nullopt); };
    addAndMakeVisible (addButton);

    addAndMakeVisible (canvas);
    addAndMakeVisible (level);
    addAndMakeVisible (graph);

    description.setJustificationType (juce::Justification::centredLeft);
    description.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
    description.setFont (juce::FontOptions (12.5f));
    description.setMinimumHorizontalScale (0.8f);
    addAndMakeVisible (description);

    targetCaption.setText ("Moves", juce::dontSendNotification);
    targetCaption.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    targetCaption.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    addChildComponent (targetCaption);

    targetBox.setTooltip ("The block whose main knob this modulator moves. You can also drag its orange dot onto a block.");
    targetBox.onChange = [this]
    {
        if (selectedNode >= 0 && targetBox.getSelectedId() > 0)
            pedal.setTarget (selectedNode, targetBox.getSelectedId() - 2);

        canvas.repaint();
    };
    addChildComponent (targetBox);

    refreshNodes();
    startTimerHz (15);
}

PatchPanel::~PatchPanel() = default;

void PatchPanel::refreshNodes()
{
    nodes = pedal.getNodesInUse();

    if (std::find (nodes.begin(), nodes.end(), selectedNode) == nodes.end())
        selectedNode = nodes.empty() ? -1 : nodes.back();

    selectNode (selectedNode);
}

void PatchPanel::selectNode (int index)
{
    selectedNode = index;
    knobs.clear();
    widthKnobs.clear();
    choices.clear();
    switches.clear();
    targetBox.setVisible (false);
    targetCaption.setVisible (false);

    if (index >= 0)
    {
        auto& block = pedal.getNode (index).block;
        const auto type = block.getType();
        const juce::String what = type == BlockType::mix
            ? "Joins two wires. Blend sets how much of the second input (the lower dot) you hear."
            : juce::String (custom_blocks::getDescription (type));
        description.setText (juce::String (custom_blocks::getTypeName (type)) + ": " + what, juce::dontSendNotification);

        for (const auto& control : custom_blocks::getControlsFor (block, type))
        {
            if (control.parameter == &block.target)
            {
                refreshTargetChoices();
                targetBox.setVisible (true);
                targetCaption.setVisible (true);
            }
            else if (control.parameter->isChoice())
                addAndMakeVisible (choices.add (new ParameterChoice (*control.parameter, control.caption)));
            else if (control.parameter->isSwitch())
                addAndMakeVisible (switches.add (new ParameterToggle (*control.parameter)));
            else if (control.parameter == &block.resonance && juce::String (control.caption) == "Width")
                addAndMakeVisible (widthKnobs.add (new BlockWidthKnob (block.resonance, block.frequency)));
            else
                addAndMakeVisible (knobs.add (new ParameterKnob (*control.parameter, control.caption)));
        }
    }
    else
    {
        description.setText ("Wire blocks between In and Out. One output can feed several blocks; a Mix (+) joins two; "
                             "a wire back to an earlier block makes a feedback loop.",
                             juce::dontSendNotification);
    }

    removeBlockButton.setEnabled (index >= 0);
    canvas.repaint();
    resized();
}

void PatchPanel::refreshTargetChoices()
{
    if (selectedNode < 0 || ! custom_blocks::isModulator (pedal.getNode (selectedNode).getType()))
        return;

    targetBox.clear (juce::dontSendNotification);
    targetBox.addItem ("Nothing", 1);

    for (auto n : nodes)
    {
        const auto type = pedal.getNode (n).getType();
        const juce::String knob (custom_blocks::getModulatedKnobName (type));

        if (n != selectedNode && knob.isNotEmpty())
            targetBox.addItem (juce::String (n + 1) + ": " + custom_blocks::getShortName (type) + " " + knob, n + 2);
    }

    const auto target = pedal.getNode (selectedNode).block.getTargetSlot();
    targetBox.setSelectedId (target >= 0 ? target + 2 : 1, juce::dontSendNotification);
}

void PatchPanel::refreshGraph()
{
    // Only measured again when something that shapes the sound has changed (not a block's place).
    juce::String key;

    for (auto* parameter : pedal.getControls())
        if (parameter != &pedal.level && ! juce::String (parameter->id).endsWith (".x") && ! juce::String (parameter->id).endsWith (".y"))
            key << parameter->get() << ';';

    if (key == lastResponseKey)
        return;

    lastResponseKey = key;
    bool hasBlocks = false;
    auto curve = pedal.getResponseDb (ResponseGraph::getFrequencies(), hasBlocks);
    graph.setCurve (std::move (curve), hasBlocks, "Add a block to see the curve");
}

void PatchPanel::timerCallback()
{
    // Undo, A/B and presets can change the patch underneath the panel.
    if (pedal.getNodesInUse() != nodes)
        refreshNodes();
    else if (selectedNode >= 0 && targetBox.isVisible())
        refreshTargetChoices();

    refreshGraph();
    canvas.repaint();
}

void PatchPanel::resized()
{
    auto header = getHeaderArea();
    power.setBounds (header.removeFromRight (60));
    header.removeFromRight (12);
    removeBlockButton.setBounds (header.removeFromRight (110));
    header.removeFromRight (8);
    addButton.setBounds (header.removeFromRight (100));

    auto area = getContentArea();
    auto editor = area.removeFromBottom (juce::jmin (ParameterKnob::preferredHeight, area.getHeight() / 2));
    area.removeFromBottom (2);
    description.setBounds (area.removeFromBottom (18));
    area.removeFromBottom (4);
    canvas.setBounds (area);

    // Below the canvas: the graph, the selected block's controls, and the pedal's level.
    graph.setBounds (editor.removeFromLeft (180).reduced (0, 10));
    editor.removeFromLeft (12);
    level.setBounds (editor.removeFromRight (ParameterKnob::preferredWidth));
    editor.removeFromRight (8);

    const auto numTargetRows = targetBox.isVisible() ? 1 : 0;

    if (! switches.isEmpty() || ! choices.isEmpty() || numTargetRows > 0)
    {
        auto column = editor.removeFromRight (150);
        column = column.withSizeKeepingCentre (column.getWidth(),
                                               juce::jmin (column.getHeight(),
                                                           (choices.size() + numTargetRows) * ParameterChoice::preferredHeight
                                                             + switches.size() * 32));

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

    layoutKnobRow (editor, row);
}

void PatchPanel::showAddMenu (std::optional<juce::Point<float>> clicked)
{
    const bool full = (int) nodes.size() >= PatchPedal::maxNodes;
    constexpr int removePedalItem = 1000;

    if (full && (! clicked.has_value() || removePedal == nullptr))
        return;

    juce::PopupMenu menu;
    menu.addSectionHeader (full ? "All 16 blocks are in use" : "Add a block");

    for (int t = 1; t < (int) BlockType::numTypes; ++t)
    {
        const auto type = (BlockType) t;

        if (type == BlockType::split || type == BlockType::pathB)
            continue;   // on the canvas, any output can feed several blocks

        if (type == BlockType::gain || type == BlockType::lfo)
            menu.addSeparator();

        menu.addItem (t, type == BlockType::mix ? juce::String ("Mix (+, joins two wires)") : juce::String (custom_blocks::getTypeName (type)),
                      ! full);
    }

    if (clicked.has_value() && removePedal != nullptr)
    {
        menu.addSeparator();
        menu.addItem (removePedalItem, "Remove the Patch pedal from the board");
    }

    // Where it lands: where you clicked, or an empty spot.
    const auto spot = clicked.has_value() ? canvas.toNodePosition (*clicked) : canvas.findFreeSpot();
    const auto x = spot.x;
    const auto y = spot.y;

    const auto options = clicked.has_value()
        ? juce::PopupMenu::Options().withTargetScreenArea (canvas.localAreaToGlobal (juce::Rectangle<int> (juce::roundToInt (clicked->x), juce::roundToInt (clicked->y), 1, 1)))
        : juce::PopupMenu::Options().withTargetComponent (addButton);

    menu.showMenuAsync (options,
                        [safe = juce::Component::SafePointer<PatchPanel> (this), x, y] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;

                            if (result == removePedalItem)
                            {
                                if (safe->removePedal != nullptr)
                                    safe->removePedal();   // this panel may be gone afterwards

                                return;
                            }

                            const auto node = safe->pedal.addNode ((BlockType) result, x, y);
                            safe->nodes = safe->pedal.getNodesInUse();
                            safe->selectNode (node >= 0 ? node : safe->selectedNode);
                        });
}

void PatchPanel::showNodeMenu (int index)
{
    juce::PopupMenu menu;
    menu.addItem (1, "Remove " + juce::String (custom_blocks::getTypeName (pedal.getNode (index).getType())));

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                        [safe = juce::Component::SafePointer<PatchPanel> (this), index] (int result)
                        {
                            if (safe != nullptr && result == 1)
                                safe->removeNode (index);
                        });
}

void PatchPanel::removeNode (int index)
{
    if (index < 0)
        return;

    pedal.removeNode (index);
    nodes = pedal.getNodesInUse();
    selectNode (nodes.empty() ? -1 : nodes.back());
}
