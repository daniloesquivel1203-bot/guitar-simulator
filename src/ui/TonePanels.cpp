#include "TonePanels.h"

#include "Theme.h"

void TonePanel::paint (juce::Graphics& g)
{
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (title, getHeaderArea(), juce::Justification::centredLeft);
}

void TonePanel::refreshDimming()
{
    const bool shouldDim = powerSwitch != nullptr && ! powerSwitch->isOn();

    if (shouldDim == dimmed)
        return;

    dimmed = shouldDim;

    for (auto* child : getChildren())
        if (dynamic_cast<ParameterKnob*> (child) != nullptr)
            child->setAlpha (dimmed ? 0.4f : 1.0f);
}

void TonePanel::layoutKnobRow (juce::Rectangle<int> area, const std::vector<juce::Component*>& knobs,
                               juce::Justification alignment)
{
    if (knobs.empty())
        return;

    const auto count = (int) knobs.size();
    const auto width = juce::jmin ((int) ParameterKnob::preferredWidth, area.getWidth() / count);
    const auto height = juce::jmin ((int) ParameterKnob::preferredHeight, area.getHeight());
    const auto gap = count > 1 ? juce::jlimit (0, 36, (area.getWidth() - count * width) / (count - 1)) : 0;
    const auto rowWidth = count * width + (count - 1) * gap;

    auto x = alignment.testFlags (juce::Justification::left) ? area.getX()
                                                             : area.getX() + (area.getWidth() - rowWidth) / 2;
    const auto y = area.getY() + (area.getHeight() - height) / 2;

    for (auto* knob : knobs)
    {
        knob->setBounds (x, y, width, height);
        x += width + gap;
    }
}

//==============================================================================
/** The pedal's footswitch. Stepping on it (clicking) switches the pedal on or off. */
class EffectPanel::Footswitch final : public juce::Component,
                                      public juce::SettableTooltipClient
{
public:
    explicit Footswitch (Parameter& enabledToUse) : enabled (enabledToUse)
    {
        setTooltip ("Footswitch: click to switch the pedal on or off.");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    std::function<void()> onSwitch;

    void paint (juce::Graphics& g) override
    {
        pedal_look::paintFootswitch (g, getLocalBounds().toFloat().reduced (2.0f), isMouseButtonDown());
    }

    void mouseDown (const juce::MouseEvent&) override   { repaint(); }

    void mouseUp (const juce::MouseEvent& event) override
    {
        repaint();

        if (! getLocalBounds().contains (event.getPosition()))
            return;

        enabled.set (enabled.isOn() ? 0.0f : 1.0f);

        if (onSwitch != nullptr)
            onSwitch();
    }

private:
    Parameter& enabled;
};

namespace
{
constexpr int stompKnobWidth = 92;
constexpr int stompKnobHeight = 104;
constexpr int stompTopPad = 22;
constexpr int stompTitleHeight = 74;
constexpr int stompExtrasHeight = 54;
constexpr int stompFootHeight = 128;
}

EffectPanel::EffectPanel (Effect& effectToUse, juce::Colour faceToUse, const juce::String& categoryToUse,
                          std::function<void()> onRemove, SharedTempo tempo)
    : TonePanel ({}),
      effect (effectToUse),
      face (faceToUse),
      category (categoryToUse),
      sharedTempo (std::move (tempo)),
      tapTime (effectToUse.getTapTimeParameter()),
      tempoSync (effectToUse.getTempoSyncParameter())
{
    const auto ink = pedal_look::inkFor (face);

    footswitch = std::make_unique<Footswitch> (effect.enabled);
    footswitch->onSwitch = [this] { repaint(); };
    addAndMakeVisible (*footswitch);

    if (onRemove != nullptr)
    {
        removeButton.setTooltip ("Take this pedal off the board.");
        removeButton.onClick = std::move (onRemove);
        addAndMakeVisible (removeButton);
    }

    for (auto* control : effect.getControls())
    {
        if (control->isChoice())
        {
            addAndMakeVisible (choices.add (new ParameterChoice (*control)));
        }
        else if (control->isSwitch())
        {
            auto* toggle = switches.add (new ParameterToggle (*control));
            toggle->setColour (juce::ToggleButton::textColourId, ink);
            toggle->setColour (juce::ToggleButton::tickDisabledColourId, ink.withAlpha (0.6f));
            addAndMakeVisible (toggle);
        }
        else
        {
            auto* knob = knobs.add (new ParameterKnob (*control));
            knob->setLookAndFeel (&knobLook);
            knob->setInk (ink);
            addAndMakeVisible (knob);

            if (control == tapTime)
                timeKnob = knob;
        }
    }

    for (auto* choice : choices)
        choice->setInk (ink);

    if (tapTime != nullptr && tempoSync != nullptr)
    {
        tapTempo = std::make_unique<TapTempo>();

        tapButton.setTooltip ("Tap along with the beat to set the delay time. With Sync on, "
                              "this sets the metronome's tempo, and the delay follows it.");
        tapButton.onClick = [this] { tapped(); };
        addAndMakeVisible (tapButton);

        tempoInfo.setFont (juce::FontOptions (12.0f));
        tempoInfo.setJustificationType (juce::Justification::centred);
        tempoInfo.setMinimumHorizontalScale (0.8f);
        tempoInfo.setColour (juce::Label::textColourId, ink.withAlpha (0.75f));
        addAndMakeVisible (tempoInfo);

        refreshTempoInfo();
    }

    lastOn = effect.enabled.isOn();
    startTimerHz (10);
}

EffectPanel::~EffectPanel()
{
    for (auto* knob : knobs)
        knob->setLookAndFeel (nullptr);
}

int EffectPanel::getColumns() const
{
    const auto count = knobs.size();
    return count <= 3 ? juce::jmax (1, count) : (count == 4 ? 2 : (count <= 6 ? 3 : 4));
}

int EffectPanel::getKnobRows() const
{
    return knobs.isEmpty() ? 0 : (knobs.size() + getColumns() - 1) / getColumns();
}

bool EffectPanel::hasExtras() const
{
    return ! choices.isEmpty() || ! switches.isEmpty() || tapTempo != nullptr;
}

juce::Rectangle<int> EffectPanel::getEnclosure() const
{
    const auto numExtras = choices.size() + switches.size() + (tapTempo != nullptr ? 1 : 0);
    const auto width = juce::jmax (280, getColumns() * stompKnobWidth + 40, numExtras * 128 + 32);
    const auto height = stompTopPad + getKnobRows() * stompKnobHeight + stompTitleHeight
                      + (hasExtras() ? stompExtrasHeight : 0) + stompFootHeight;

    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (30);   // room for the Remove button
    const auto w = juce::jmin (width, area.getWidth());
    const auto h = juce::jmin (height, area.getHeight());
    return juce::Rectangle<int> (w, h).withCentre (area.getCentre());
}

void EffectPanel::paint (juce::Graphics& g)
{
    const auto box = getEnclosure().toFloat();
    const bool on = effect.enabled.isOn();

    // Input and output jacks on the sides.
    for (auto x : { box.getX() - 12.0f, box.getRight() - 4.0f })
    {
        const juce::Rectangle<float> jack (x, box.getY() + 70.0f, 16.0f, 30.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff999999), jack.getX(), 0.0f, juce::Colour (0xff666666), jack.getRight(), 0.0f, false));
        g.fillRoundedRectangle (jack, 3.0f);
    }

    pedal_look::paintEnclosure (g, box, face, 20.0f);

    const auto ink = pedal_look::inkFor (face);
    auto inner = box.reduced (18.0f, 0.0f);
    inner.removeFromTop ((float) (stompTopPad + getKnobRows() * stompKnobHeight));

    auto nameArea = inner.removeFromTop ((float) stompTitleHeight);
    g.setColour (ink);
    g.setFont (juce::FontOptions (juce::jlimit (24.0f, 40.0f, box.getWidth() / 7.0f), juce::Font::bold | juce::Font::italic));
    g.drawFittedText (juce::String (effect.name).toUpperCase(), nameArea.removeFromTop (nameArea.getHeight() * 0.66f).toNearestInt(),
                      juce::Justification::centredBottom, 1, 0.6f);
    g.setColour (ink.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (category.toUpperCase(), nameArea, juce::Justification::centredTop);

    if (hasExtras())
        inner.removeFromTop ((float) stompExtrasHeight);

    pedal_look::paintLed (g, { box.getCentreX(), inner.getY() + 14.0f }, 11.0f, on);
}

void EffectPanel::tapped()
{
    const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const auto bpm = tapTempo->tap (now);

    // A single tap cannot give a tempo yet.
    keepTappingUntil = bpm.has_value() ? 0.0 : now + 2.0;

    if (! bpm.has_value())
    {
        refreshTempoInfo();
        return;
    }

    if (tempo_sync::isSynced (*tempoSync) && sharedTempo.set != nullptr)
    {
        sharedTempo.set (std::round (*bpm));
    }
    else
    {
        // The gap between taps is the time between echoes.
        tapTime->set ((float) std::round (60000.0 / *bpm));

        if (timeKnob != nullptr)
            timeKnob->refresh();
    }

    refreshTempoInfo();
}

void EffectPanel::timerCallback()
{
    if (tapTempo != nullptr)
        refreshTempoInfo();

    // The light follows the switch, which can also change from the chain stack.
    if (lastOn != effect.enabled.isOn())
    {
        lastOn = effect.enabled.isOn();
        repaint();
    }
}

void EffectPanel::refreshTempoInfo()
{
    const bool synced = tempo_sync::isSynced (*tempoSync);

    // The Time knob does nothing while the delay follows the tempo.
    if (timeKnob != nullptr)
    {
        const bool dim = synced || ! effect.enabled.isOn();
        timeKnob->setAlpha (dim ? 0.4f : 1.0f);
        timeKnob->refresh();
    }

    juce::String text;

    if (juce::Time::getMillisecondCounterHiRes() / 1000.0 < keepTappingUntil)
    {
        text = "Keep tapping...";
    }
    else if (synced)
    {
        const auto bpm = sharedTempo.get != nullptr ? sharedTempo.get() : 100.0;
        const auto ms = tempo_sync::noteMs (bpm, tempoSync->getIndex(), tapTime->minValue, tapTime->maxValue);
        text = juce::String (juce::roundToInt (bpm)) + " BPM: " + juce::String (juce::roundToInt (ms)) + " ms";
    }
    else
    {
        text = "Tap sets the time";
    }

    tempoInfo.setText (text, juce::dontSendNotification);
}

void EffectPanel::resized()
{
    const auto box = getEnclosure();
    removeButton.setBounds (box.getRight() - 80, box.getY() - 36, 80, 26);

    auto inner = box.reduced (20, 0);
    inner.removeFromTop (stompTopPad);

    // Knobs in a grid, each row centred.
    const auto columns = getColumns();

    for (int row = 0; row < getKnobRows(); ++row)
    {
        auto rowArea = inner.removeFromTop (stompKnobHeight);
        const auto first = row * columns;
        const auto count = juce::jmin (columns, knobs.size() - first);
        const auto width = juce::jmin (stompKnobWidth, rowArea.getWidth() / juce::jmax (1, count));
        auto x = rowArea.getCentreX() - count * width / 2;

        for (int i = 0; i < count; ++i, x += width)
            knobs[first + i]->setBounds (x, rowArea.getY(), width, stompKnobHeight);
    }

    inner.removeFromTop (stompTitleHeight);

    // Choices, switches and Tap sit in a row under the name.
    if (hasExtras())
    {
        auto row = inner.removeFromTop (stompExtrasHeight);
        std::vector<juce::Component*> extras;

        for (auto* choice : choices)    extras.push_back (choice);
        for (auto* toggle : switches)   extras.push_back (toggle);

        const auto slotWidth = juce::jmin (124, row.getWidth() / juce::jmax (1, (int) extras.size() + (tapTempo != nullptr ? 1 : 0)));
        const auto total = slotWidth * ((int) extras.size() + (tapTempo != nullptr ? 1 : 0));
        auto x = row.getCentreX() - total / 2;

        for (auto* extra : extras)
        {
            const bool isToggle = dynamic_cast<ParameterToggle*> (extra) != nullptr;
            extra->setBounds (juce::Rectangle<int> (x, row.getY(), slotWidth, row.getHeight()).reduced (4, isToggle ? 12 : 2));
            x += slotWidth;
        }

        if (tapTempo != nullptr)
        {
            auto slot = juce::Rectangle<int> (x, row.getY(), slotWidth, row.getHeight()).reduced (4, 2);
            tapButton.setBounds (slot.removeFromTop (28));
            tempoInfo.setBounds (slot);
        }
    }

    inner.removeFromTop (34);   // the light
    footswitch->setBounds (juce::Rectangle<int> (64, 64).withCentre ({ box.getCentreX(), inner.getY() + 40 }));
}
