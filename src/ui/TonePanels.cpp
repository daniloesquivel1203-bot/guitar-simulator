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
AmpPanel::AmpPanel (AmpModel& ampToUse)
    : TonePanel ("AMP", &ampToUse.enabled),
      amp (ampToUse),
      power (amp.enabled),
      gain (amp.gain), bass (amp.bass), mid (amp.mid),
      treble (amp.treble), presence (amp.presence), master (amp.master)
{
    voicing.addItemList (AmpModel::getVoicingNames(), 1);
    voicing.setSelectedItemIndex (amp.voicing.getIndex(), juce::dontSendNotification);
    voicing.onChange = [this] { amp.voicing.set ((float) voicing.getSelectedItemIndex()); };

    for (auto* component : std::initializer_list<juce::Component*> { &power, &voicing, &gain, &bass, &mid,
                                                                     &treble, &presence, &master })
        addAndMakeVisible (component);
}

void AmpPanel::resized()
{
    auto header = getHeaderArea();
    power.setBounds (header.removeFromRight (60));
    header.removeFromRight (12);
    voicing.setBounds (header.removeFromRight (140));

    layoutKnobRow (getContentArea(), { &gain, &bass, &mid, &treble, &presence, &master });
}

//==============================================================================
CabinetPanel::CabinetPanel (Cabinet& cabinetToUse)
    : TonePanel ("CABINET", &cabinetToUse.enabled),
      cabinet (cabinetToUse),
      power (cabinet.enabled),
      lowCut (cabinet.lowCut), highCut (cabinet.highCut)
{
    choice.onChange = [this] { choiceChanged(); };
    refreshChoices();

    message.setFont (juce::FontOptions (13.0f));
    message.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
    message.setJustificationType (juce::Justification::centredLeft);
    message.setText ("Built-in cabinets are approximations. For the most realistic sound, load an impulse response file "
                     "(WAV or AIFF). Many free ones are available online.",
                     juce::dontSendNotification);

    for (auto* component : std::initializer_list<juce::Component*> { &power, &choice, &lowCut, &highCut, &message })
        addAndMakeVisible (component);
}

void CabinetPanel::resized()
{
    auto header = getHeaderArea();
    power.setBounds (header.removeFromRight (60));
    header.removeFromRight (12);
    choice.setBounds (header.removeFromRight (240));

    auto area = getContentArea();
    layoutKnobRow (area.removeFromLeft (2 * ParameterKnob::preferredWidth + 36), { &lowCut, &highCut });
    area.removeFromLeft (28);
    message.setBounds (area);
}

void CabinetPanel::refreshChoices()
{
    choice.clear (juce::dontSendNotification);
    choice.addItemList (Cabinet::getBuiltInNames(), firstBuiltInId);
    choice.addSeparator();

    if (cabinet.getBuiltInIndex() < 0)
        choice.addItem ("File: " + cabinet.getCurrentName(), loadedFileId);

    choice.addItem ("Load impulse response file...", loadFileId);

    choice.setSelectedId (cabinet.getBuiltInIndex() >= 0 ? firstBuiltInId + cabinet.getBuiltInIndex()
                                                         : (int) loadedFileId,
                          juce::dontSendNotification);
}

void CabinetPanel::choiceChanged()
{
    const auto id = choice.getSelectedId();

    if (id == loadFileId)
    {
        chooseFile();
        return;
    }

    if (id >= firstBuiltInId && id < loadedFileId)
    {
        cabinet.selectBuiltIn (id - firstBuiltInId);
        refreshChoices();
    }
}

void CabinetPanel::chooseFile()
{
    const auto startFolder = cabinet.getLoadedFile().existsAsFile()
                                 ? cabinet.getLoadedFile().getParentDirectory()
                                 : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    fileChooser = std::make_unique<juce::FileChooser> ("Choose an impulse response file", startFolder,
                                                       "*.wav;*.aif;*.aiff;*.flac");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
                              {
                                  const auto file = chooser.getResult();

                                  // An empty result means the dialog was cancelled.
                                  if (file != juce::File())
                                  {
                                      const auto error = cabinet.loadFile (file);

                                      if (error.isNotEmpty())
                                      {
                                          message.setColour (juce::Label::textColourId, juce::Colour (0xfff5a524));
                                          message.setText (error, juce::dontSendNotification);
                                      }
                                      else
                                      {
                                          message.setColour (juce::Label::textColourId,
                                                             juce::Colours::white.withAlpha (0.6f));
                                          message.setText ("Loaded " + file.getFileName() + ".",
                                                           juce::dontSendNotification);
                                      }
                                  }

                                  refreshChoices();
                              });
}

//==============================================================================
EffectPanel::EffectPanel (Effect& effect)
    : TonePanel (juce::String (effect.name).toUpperCase(), &effect.enabled),
      power (effect.enabled)
{
    addAndMakeVisible (power);

    for (auto* control : effect.getControls())
    {
        if (control->isSwitch())
            addAndMakeVisible (switches.add (new ParameterToggle (*control)));
        else
            addAndMakeVisible (knobs.add (new ParameterKnob (*control)));
    }
}

void EffectPanel::resized()
{
    auto header = getHeaderArea();
    power.setBounds (header.removeFromRight (60));

    auto area = getContentArea();

    // Switches sit in a column on the right, and the knobs are centred in what is left.
    if (! switches.isEmpty())
    {
        auto column = area.removeFromRight (150);
        column = column.withSizeKeepingCentre (column.getWidth(), switches.size() * 32);

        for (auto* toggle : switches)
            toggle->setBounds (column.removeFromTop (32).reduced (0, 3));
    }

    std::vector<juce::Component*> row;

    for (auto* knob : knobs)
        row.push_back (knob);

    layoutKnobRow (area, row);
}
