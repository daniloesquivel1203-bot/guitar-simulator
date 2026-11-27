#include "PedalTray.h"

#include "MiniPedal.h"

namespace
{
const juce::String myPedalsTab { "My pedals" };
constexpr int tileWidth = 58;
constexpr int tileHeight = 84;
constexpr int captionHeight = 18;
constexpr int tileGap = 12;
}

/** One pedal in the tray, drawn as a tiny stompbox with its name underneath. */
class PedalTray::MiniPedal final : public juce::Component,
                                    public juce::SettableTooltipClient
{
public:
    MiniPedal (PedalTray& ownerToUse, juce::String descriptionToUse, juce::String nameToUse, juce::Colour colourToUse, int numKnobsToUse)
        : owner (ownerToUse), description (std::move (descriptionToUse)), name (std::move (nameToUse)),
          colour (colourToUse), numKnobs (numKnobsToUse)
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip (name + ". Click to add it after the selected pedal, or drag it onto the stack.");
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        auto caption = area.removeFromBottom ((float) captionHeight);
        paintBody (g, area.reduced (2.0f, 2.0f), isMouseOver());

        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText (name, caption.toNearestInt(), juce::Justification::centred, 1, 0.7f);
    }

    void paintBody (juce::Graphics& g, juce::Rectangle<float> body, bool highlighted) const
    {
        pedal_look::paintEnclosure (g, body, highlighted ? colour.brighter (0.15f) : colour, 7.0f);

        // The pedal's own knobs, in rows like the big stompbox.
        const auto rows = numKnobs <= 3 ? 1 : (numKnobs <= 6 ? 2 : 3);
        mini_pedal::paintKnobs (g, juce::Rectangle<float> (body.getX() + 4.0f, body.getY() + 5.0f, body.getWidth() - 8.0f, 15.0f * (float) rows),
                                numKnobs, 12.0f, false);

        pedal_look::paintFootswitch (g, juce::Rectangle<float> (18.0f, 18.0f).withCentre ({ body.getCentreX(), body.getBottom() - 15.0f }), false);
    }

    void mouseEnter (const juce::MouseEvent&) override   { repaint(); }
    void mouseExit (const juce::MouseEvent&) override    { repaint(); }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (dragging || event.getDistanceFromDragStart() < 6)
            return;

        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            dragging = true;
            const auto scale = 2.0f;
            juce::Image image (juce::Image::ARGB, juce::roundToInt (tileWidth * scale), juce::roundToInt ((tileHeight - captionHeight) * scale), true);

            {
                juce::Graphics g (image);
                g.addTransform (juce::AffineTransform::scale (scale));
                paintBody (g, juce::Rectangle<float> ((float) tileWidth, (float) (tileHeight - captionHeight)).reduced (2.0f), true);
            }

            container->startDragging (description, this, juce::ScaledImage (image, scale), true);
        }
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        const auto wasDragging = dragging;
        dragging = false;

        if (! wasDragging && ! event.mouseWasDraggedSinceMouseDown() && owner.onPick != nullptr)
            owner.onPick (description);
    }

private:
    PedalTray& owner;
    juce::String description, name;
    juce::Colour colour;
    int numKnobs;
    bool dragging = false;
};

//==============================================================================
PedalTray::PedalTray()
{
    for (const auto& type : getPedalTypes())
        categories.addIfNotAlreadyThere (type.category);

    categories.add (myPedalsTab);

    for (const auto& category : categories)
    {
        auto* tab = tabs.add (new juce::TextButton (category));
        tab->setClickingTogglesState (false);
        tab->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe08a2b));
        tab->setColour (juce::TextButton::textColourOnId, juce::Colour (0xff111111));
        tab->onClick = [this, category] { showCategory (category); };
        addAndMakeVisible (tab);
    }

    viewport.setViewedComponent (&tileHolder, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    hint.setFont (juce::FontOptions (12.0f));
    hint.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.5f));
    hint.setJustificationType (juce::Justification::centredLeft);
    addChildComponent (hint);

    showCategory (categories[0]);
}

PedalTray::~PedalTray() = default;

void PedalTray::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff15171a));
    g.fillRect (getLocalBounds());
    g.setColour (juce::Colours::black);
    g.fillRect (getLocalBounds().removeFromTop (1));
}

void PedalTray::resized()
{
    auto area = getLocalBounds().reduced (12, 8);
    auto tabRow = area.removeFromTop (26);

    // Tabs are as wide as their names, squeezed together if the tray is narrow.
    std::vector<int> widths;
    int total = 0;

    for (auto* tab : tabs)
        total += widths.emplace_back (juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (13.0f), tab->getButtonText()) + 18) + 4;

    const auto scale = juce::jmin (1.0f, (float) tabRow.getWidth() / (float) juce::jmax (1, total));

    for (int i = 0; i < tabs.size(); ++i)
    {
        tabs[i]->setBounds (tabRow.removeFromLeft (juce::roundToInt ((float) widths[(size_t) i] * scale)));
        tabRow.removeFromLeft (juce::roundToInt (4.0f * scale));
    }

    area.removeFromTop (8);
    viewport.setBounds (area);
    hint.setBounds (area.removeFromTop (30));

    const auto width = juce::jmax (area.getWidth(), tiles.size() * (tileWidth + tileGap));
    tileHolder.setBounds (0, 0, width, tileHeight + 2);

    for (int i = 0; i < tiles.size(); ++i)
        tiles[i]->setBounds (i * (tileWidth + tileGap), 0, tileWidth, tileHeight);
}

void PedalTray::showCategory (const juce::String& category)
{
    current = categories.contains (category) ? category : categories[0];

    for (auto* tab : tabs)
        tab->setToggleState (tab->getButtonText() == current, juce::dontSendNotification);

    rebuildTiles();
    resized();
}

void PedalTray::rebuildTiles()
{
    tiles.clear();

    if (current == myPedalsTab)
    {
        const auto names = getMyPedals != nullptr ? getMyPedals() : juce::StringArray();
        const auto colour = pedal_look::categoryColour ("Custom").brighter (0.1f);

        for (const auto& name : names)
            tileHolder.addAndMakeVisible (tiles.add (new MiniPedal (*this, "mine:" + name, name, colour, 2)));

        hint.setText ("None yet. Open a Custom pedal and use Save as my pedal.", juce::dontSendNotification);
        hint.setVisible (names.isEmpty());
        return;
    }

    hint.setVisible (false);

    for (const auto& type : getPedalTypes())
    {
        if (current != type.category)
            continue;

        const auto effect = type.create();
        const auto numKnobs = mini_pedal::countKnobs (*effect);
        tileHolder.addAndMakeVisible (tiles.add (new MiniPedal (*this, juce::String ("pedal:") + type.key, effect->name,
                                                                pedal_look::pedalColour (type), numKnobs)));
    }
}
