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
AmpPanel::AmpPanel (AmpModel& ampToUse, AmpCapture& captureToUse, Cabinet& cabinetToUse)
    : TonePanel ("AMP", &ampToUse.enabled),
      amp (ampToUse), capture (captureToUse), cabinet (cabinetToUse),
      power (amp.enabled),
      gain (amp.gain), bass (amp.bass), mid (amp.mid),
      treble (amp.treble), presence (amp.presence), master (amp.master),
      captureInput (capture.input), captureBass (capture.bass), captureMid (capture.mid),
      captureTreble (capture.treble), captureOutput (capture.output)
{
    choice.onChange = [this] { choiceChanged(); };

    message.setFont (juce::FontOptions (13.0f));
    message.setJustificationType (juce::Justification::centredLeft);
    message.setMinimumHorizontalScale (1.0f);

    for (auto* component : std::initializer_list<juce::Component*> { &power, &choice, &gain, &bass, &mid, &treble,
                                                                     &presence, &master, &captureInput, &captureBass,
                                                                     &captureMid, &captureTreble, &captureOutput })
        addAndMakeVisible (component);

    addChildComponent (message);
    refreshChoices();
    showCaptureDetails();
}

void AmpPanel::resized()
{
    auto header = getHeaderArea();
    power.setBounds (header.removeFromRight (60));
    header.removeFromRight (12);
    choice.setBounds (header.removeFromRight (260));

    auto area = getContentArea();
    layoutKnobRow (area, { &gain, &bass, &mid, &treble, &presence, &master });

    // In capture mode the knobs sit on the left and a note about the capture on the right.
    auto captureKnobs = area.removeFromLeft (juce::jmin (area.getWidth() * 2 / 3, 5 * ParameterKnob::preferredWidth + 4 * 24));
    layoutKnobRow (captureKnobs, { &captureInput, &captureBass, &captureMid, &captureTreble, &captureOutput });
    area.removeFromLeft (24);
    message.setBounds (area);
}

void AmpPanel::refreshChoices()
{
    choice.clear (juce::dontSendNotification);
    choice.addItemList (AmpModel::getVoicingNames(), firstVoicingId);
    choice.addSeparator();

    if (capture.isLoaded())
        choice.addItem ("Capture: " + capture.getInfo().name, captureId);

    choice.addItem ("Load capture file (.nam)...", loadCaptureId);

    if (capture.isLoaded())
        choice.addItem ("Remove capture", removeCaptureId);

    choice.setSelectedId (capture.isInUse() ? (int) captureId : firstVoicingId + amp.voicing.getIndex(),
                          juce::dontSendNotification);

    const auto usingCapture = capture.isInUse();

    for (auto* knob : { &gain, &bass, &mid, &treble, &presence, &master })
        knob->setVisible (! usingCapture);

    for (auto* knob : { &captureInput, &captureBass, &captureMid, &captureTreble, &captureOutput })
        knob->setVisible (usingCapture);

    message.setVisible (usingCapture);
}

void AmpPanel::choiceChanged()
{
    const auto id = choice.getSelectedId();

    if (id == loadCaptureId)
    {
        chooseCaptureFile();
        return;
    }

    if (id == removeCaptureId)
    {
        capture.unload();
        capture.active.set (0.0f);
    }
    else if (id == captureId)
    {
        capture.active.set (1.0f);
        showCaptureDetails();
    }
    else if (id >= firstVoicingId && id < captureId)
    {
        capture.active.set (0.0f);
        amp.voicing.set ((float) (id - firstVoicingId));
    }

    refreshChoices();
}

void AmpPanel::chooseCaptureFile()
{
    const auto startFolder = capture.getLoadedFile().existsAsFile()
                                 ? capture.getLoadedFile().getParentDirectory()
                                 : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    fileChooser = std::make_unique<juce::FileChooser> ("Choose an amp capture (.nam file)", startFolder, "*.nam");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
                              {
                                  const auto file = chooser.getResult();

                                  // An empty result means the dialog was cancelled.
                                  if (file != juce::File())
                                  {
                                      const auto error = capture.loadFile (file);

                                      if (error.isNotEmpty())
                                      {
                                          refreshChoices();
                                          juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                                                  "Could not load the capture", error);
                                          return;
                                      }

                                      capture.active.set (1.0f);

                                      // A capture that includes the speaker would get a second
                                      // one from the cabinet block, so switch that off. The
                                      // user can switch it back on.
                                      if (capture.getInfo().includesCabinet())
                                          cabinet.enabled.set (0.0f);

                                      showCaptureDetails();
                                  }

                                  refreshChoices();
                              });
}

void AmpPanel::showCaptureDetails()
{
    if (! capture.isLoaded())
        return;

    const auto info = capture.getInfo();
    juce::StringArray lines;

    lines.add (info.name + ", made at " + juce::String (info.sampleRate / 1000.0, 1) + " kHz.");

    if (info.includesCabinet())
        lines.add ("It includes the speaker cabinet, so the Cab block is normally off. Switch it on to add a second cabinet.");
    else if (info.gearType.isNotEmpty())
        lines.add ("It does not include a speaker cabinet, so keep the Cab block on.");
    else
        lines.add ("The file does not say whether it includes a speaker cabinet. If it sounds muffled, switch the Cab block off.");

    if (! info.hasLoudness)
        lines.add ("The file does not report its loudness, so set the level with Output.");

    lines.add ("Input sets how hard the capture is driven.");

    message.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
    message.setText (lines.joinIntoString ("\n"), juce::dontSendNotification);
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
EffectPanel::EffectPanel (Effect& effect, std::function<void()> onRemove)
    : TonePanel (juce::String (effect.name).toUpperCase(), &effect.enabled),
      power (effect.enabled)
{
    addAndMakeVisible (power);

    if (onRemove != nullptr)
    {
        removeButton.setTooltip ("Take this pedal off the board.");
        removeButton.onClick = std::move (onRemove);
        addAndMakeVisible (removeButton);
    }

    for (auto* control : effect.getControls())
    {
        if (control->isChoice())
            addAndMakeVisible (choices.add (new ParameterChoice (*control)));
        else if (control->isSwitch())
            addAndMakeVisible (switches.add (new ParameterToggle (*control)));
        else
            addAndMakeVisible (knobs.add (new ParameterKnob (*control)));
    }
}

void EffectPanel::resized()
{
    auto header = getHeaderArea();
    power.setBounds (header.removeFromRight (60));
    header.removeFromRight (12);
    removeButton.setBounds (header.removeFromRight (80));

    auto area = getContentArea();

    // Choices and switches sit in a column on the right, and the knobs are
    // centred in what is left.
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
