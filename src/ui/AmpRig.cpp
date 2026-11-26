#include "AmpRig.h"

#include "Theme.h"

namespace
{
const char* const matchSettingKey = "cab.matchVoicing";
constexpr int headHeight = 168;
constexpr int stripHeight = 138;
constexpr int margin = 16;

const auto gold = pedal_look::AmpKnobs::gold;
}

AmpRig::AmpRig (AmpModel& ampToUse, AmpCapture& captureToUse, Cabinet& cabinetToUse, juce::PropertiesFile& settingsToUse)
    : amp (ampToUse), capture (captureToUse), cabinet (cabinetToUse), settings (settingsToUse),
      gain (amp.gain), bass (amp.bass), mid (amp.mid),
      treble (amp.treble), presence (amp.presence), master (amp.master),
      captureInput (capture.input), captureBass (capture.bass), captureMid (capture.mid),
      captureTreble (capture.treble), captureOutput (capture.output),
      lowCut (cabinet.lowCut), highCut (cabinet.highCut)
{
    for (auto* knob : { &gain, &bass, &mid, &treble, &presence, &master,
                        &captureInput, &captureBass, &captureMid, &captureTreble, &captureOutput, &lowCut, &highCut })
    {
        knob->setLookAndFeel (&ampKnobsLook);
        knob->setInk (gold);
        addAndMakeVisible (knob);
    }

    // Gain and Master get gold caps, like the knobs people reach for most.
    for (auto* knob : { &gain, &master, &captureInput, &captureOutput })
        knob->getSlider().getProperties().set ("goldCap", true);

    for (auto* box : { &ampChoice, &cabChoice })
    {
        box->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff0b0f0b));
        box->setColour (juce::ComboBox::textColourId, juce::Colour (0xff9dff9a));
        box->setColour (juce::ComboBox::arrowColourId, juce::Colour (0xff9dff9a).withAlpha (0.7f));
        box->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (*box);
    }

    ampChoice.setTooltip ("Pick an amp voicing, or load a capture (.nam file) of a real amp.");
    ampChoice.onChange = [this] { ampChoiceChanged(); };

    cabChoice.setTooltip ("Pick a built-in cabinet, or load an impulse response file (WAV or AIFF).");
    cabChoice.onChange = [this] { cabChoiceChanged(); };

    matchButton.setTooltip ("When on, picking a voicing also picks the cabinet that suits it. "
                            "Choosing a cabinet yourself turns this off.");
    matchButton.setColour (juce::ToggleButton::textColourId, gold);
    matchButton.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xffe08a2b));
    matchButton.setToggleState (settings.getBoolValue (matchSettingKey, true), juce::dontSendNotification);
    matchButton.onClick = [this]
    {
        settings.setValue (matchSettingKey, matchButton.getToggleState());

        if (matchButton.getToggleState())
            applyMatch();
    };
    addAndMakeVisible (matchButton);

    for (auto* note : { &captureNote, &cabNote })
    {
        note->setFont (juce::FontOptions (12.0f));
        note->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.55f));
        note->setJustificationType (juce::Justification::topLeft);
        note->setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (*note);
    }

    cabNote.setJustificationType (juce::Justification::centredRight);
    refreshAll();
}

AmpRig::~AmpRig()
{
    for (auto* knob : { &gain, &bass, &mid, &treble, &presence, &master,
                        &captureInput, &captureBass, &captureMid, &captureTreble, &captureOutput, &lowCut, &highCut })
        knob->setLookAndFeel (nullptr);
}

int AmpRig::getMatchingCabinet (int voicing)
{
    // Indices into Cabinet::getBuiltInNames(), by AmpModel::Voicing.
    static constexpr int matches[AmpModel::numVoicings] = {
        7,    // Clean: 2x12 American Open Back
        1,    // Crunch: 2x12 British
        2,    // Lead: 4x12 Closed Back
        6,    // Bright Clean: 2x10 Open Back
        5,    // Jazz Clean: 1x15 Jazz
        8,    // Chime: 2x12 Smooth Chime
        9,    // Tweed: 4x10 Vintage
        10,   // British Vintage: 4x12 Warm Vintage
        2,    // Hot-Rod British: 4x12 Closed Back
        4,    // Smooth Overdrive: 1x12 Closed Back
        11,   // Modern Scooped: 4x12 Modern Aggressive
        11    // Tight Modern: 4x12 Modern Aggressive
    };

    return matches[juce::jlimit (0, (int) AmpModel::numVoicings - 1, voicing)];
}

//==============================================================================
juce::Rectangle<int> AmpRig::getHeadArea() const
{
    const auto width = juce::jmin (getWidth() - 2 * margin, 520);
    const auto totalHeight = juce::jmin (getHeight() - 2 * margin - 40, headHeight + 4 + 420);
    const auto top = juce::jmax (margin + 12, (getHeight() - totalHeight - 40) / 2);
    return { (getWidth() - width) / 2, top, width, headHeight };
}

juce::Rectangle<int> AmpRig::getCabArea() const
{
    const auto head = getHeadArea();
    const auto bottom = juce::jmin (getHeight() - margin - 40, head.getBottom() + 4 + 420);
    return { head.getX(), head.getBottom() + 4, head.getWidth(), bottom - head.getBottom() - 4 };
}

juce::Rectangle<int> AmpRig::getAmpLightArea() const
{
    return getHeadArea().reduced (12).removeFromTop (30).removeFromRight (24);
}

juce::Rectangle<int> AmpRig::getCabLightArea() const
{
    return getCabArea().reduced (10).removeFromBottom (stripHeight).reduced (10, 8).removeFromTop (28).removeFromRight (24);
}

int AmpRig::countSpeakers() const
{
    // Built-in names start with the layout, such as "4x12".
    const auto name = cabinet.getCurrentName();

    if (cabinet.getBuiltInIndex() >= 0 && name.length() > 1 && name[1] == 'x')
        return juce::jlimit (1, 4, (int) name[0] - '0');

    return 2;
}

void AmpRig::paint (juce::Graphics& g)
{
    // A soft spotlight behind the rig.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2c32), (float) getWidth() * 0.5f, (float) getHeight() * 0.4f,
                                             juce::Colour (0xff141518), 0.0f, (float) getHeight(), true));
    g.fillAll();
    g.setColour (juce::Colours::black);
    g.fillRect (0, 0, 1, getHeight());

    // Head: handle, tolex, and a dark control panel with a gold outline.
    const auto head = getHeadArea().toFloat();
    g.setColour (juce::Colour (0xff0f0f0f));
    g.fillRoundedRectangle (juce::Rectangle<float> (head.getWidth() * 0.25f, 14.0f).withCentre ({ head.getCentreX(), head.getY() - 4.0f }), 6.0f);
    pedal_look::paintTolex (g, head, 9.0f);

    const auto panel = head.reduced (10.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2e2e30), panel.getX(), panel.getY(),
                                             juce::Colour (0xff1c1c1e), panel.getX(), panel.getBottom(), false));
    g.fillRoundedRectangle (panel, 5.0f);
    g.setColour (juce::Colour (0xff4a4a4a));
    g.drawRoundedRectangle (panel.reduced (0.5f), 5.0f, 1.0f);

    g.setColour (gold);
    g.setFont (juce::FontOptions ("Serif", 21.0f, juce::Font::italic));
    g.drawText ("Amp & Cab", panel.reduced (12.0f, 2.0f).removeFromTop (30.0f), juce::Justification::centredLeft);

    pedal_look::paintLed (g, getAmpLightArea().toFloat().getCentre(), 10.0f, amp.enabled.isOn());

    // Cabinet: tolex, grill cloth, and the control strip along the bottom.
    const auto cab = getCabArea().toFloat();
    pedal_look::paintTolex (g, cab, 9.0f);
    auto inside = cab.reduced (10.0f);
    const auto strip = inside.removeFromBottom ((float) stripHeight);
    inside.removeFromBottom (8.0f);
    pedal_look::paintGrill (g, inside, countSpeakers());

    if (! cabinet.enabled.isOn())
    {
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (inside, 4.0f);
    }

    g.setColour (juce::Colour (0xff0f0f10));
    g.fillRoundedRectangle (strip, 5.0f);
    g.setColour (juce::Colour (0xff333333));
    g.drawRoundedRectangle (strip.reduced (0.5f), 5.0f, 1.0f);

    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText ("CABINET", strip.reduced (12.0f, 8.0f).removeFromTop (28.0f), juce::Justification::centredLeft);

    pedal_look::paintLed (g, getCabLightArea().toFloat().getCentre(), 10.0f, cabinet.enabled.isOn());
}

void AmpRig::resized()
{
    auto panel = getHeadArea().reduced (10).reduced (12, 6);
    auto top = panel.removeFromTop (30);
    top.removeFromRight (28);
    ampChoice.setBounds (top.removeFromRight (juce::jmin (210, top.getWidth() - 130)).reduced (0, 2));
    panel.removeFromTop (6);

    const auto layoutRow = [] (juce::Rectangle<int> area, const std::vector<ParameterKnob*>& knobs)
    {
        const auto width = area.getWidth() / (int) knobs.size();

        for (auto* knob : knobs)
            knob->setBounds (area.removeFromLeft (width));
    };

    layoutRow (panel, { &gain, &bass, &mid, &treble, &presence, &master });
    layoutRow (panel, { &captureInput, &captureBass, &captureMid, &captureTreble, &captureOutput });

    auto strip = getCabArea().reduced (10).removeFromBottom (stripHeight).reduced (10, 8);
    auto row = strip.removeFromTop (28);
    row.removeFromLeft (70);
    row.removeFromRight (28);
    matchButton.setBounds (row.removeFromRight (130));
    row.removeFromRight (8);
    cabChoice.setBounds (row.reduced (0, 1));

    strip.removeFromTop (4);
    auto knobs = strip.removeFromLeft (170);
    lowCut.setBounds (knobs.removeFromLeft (85));
    highCut.setBounds (knobs);
    strip.removeFromLeft (10);
    cabNote.setBounds (strip);

    const auto cab = getCabArea();
    captureNote.setBounds (cab.getX(), cab.getBottom() + 8, cab.getWidth(), getHeight() - cab.getBottom() - 10);
}

void AmpRig::mouseUp (const juce::MouseEvent& event)
{
    // The lights switch the amp and the cabinet on and off.
    if (getAmpLightArea().expanded (6).contains (event.getPosition()))
        amp.enabled.set (amp.enabled.isOn() ? 0.0f : 1.0f);
    else if (getCabLightArea().expanded (6).contains (event.getPosition()))
        cabinet.enabled.set (cabinet.enabled.isOn() ? 0.0f : 1.0f);
    else
        return;

    refreshDimming();
    repaint();
}

void AmpRig::refreshAll()
{
    for (auto* knob : { &gain, &bass, &mid, &treble, &presence, &master,
                        &captureInput, &captureBass, &captureMid, &captureTreble, &captureOutput, &lowCut, &highCut })
        knob->refresh();

    refreshAmpChoices();
    refreshCabChoices();
    ampDimmed = ! ampDimmed;   // forces the next call to apply
    cabDimmed = ! cabDimmed;
    refreshDimming();
    repaint();
}

void AmpRig::refreshDimming()
{
    const bool ampOff = ! amp.enabled.isOn();
    const bool cabOff = ! cabinet.enabled.isOn();

    if (ampOff != ampDimmed)
    {
        ampDimmed = ampOff;

        for (auto* knob : { &gain, &bass, &mid, &treble, &presence, &master,
                            &captureInput, &captureBass, &captureMid, &captureTreble, &captureOutput })
            knob->setAlpha (ampOff ? 0.4f : 1.0f);
    }

    if (cabOff != cabDimmed)
    {
        cabDimmed = cabOff;

        for (auto* knob : { &lowCut, &highCut })
            knob->setAlpha (cabOff ? 0.4f : 1.0f);
    }

    // The lights can also be changed from the chain stack.
    if (lastAmpOn != amp.enabled.isOn() || lastCabOn != cabinet.enabled.isOn())
    {
        lastAmpOn = amp.enabled.isOn();
        lastCabOn = cabinet.enabled.isOn();
        repaint();
    }
}

//==============================================================================
void AmpRig::refreshAmpChoices()
{
    ampChoice.clear (juce::dontSendNotification);
    ampChoice.addItemList (AmpModel::getVoicingNames(), firstVoicingId);
    ampChoice.addSeparator();

    if (capture.isLoaded())
        ampChoice.addItem ("Capture: " + capture.getInfo().name, captureId);

    ampChoice.addItem ("Load capture file (.nam)...", loadCaptureId);

    if (capture.isLoaded())
        ampChoice.addItem ("Remove capture", removeCaptureId);

    ampChoice.setSelectedId (capture.isInUse() ? (int) captureId : firstVoicingId + amp.voicing.getIndex(),
                             juce::dontSendNotification);

    const auto usingCapture = capture.isInUse();

    for (auto* knob : { &gain, &bass, &mid, &treble, &presence, &master })
        knob->setVisible (! usingCapture);

    for (auto* knob : { &captureInput, &captureBass, &captureMid, &captureTreble, &captureOutput })
        knob->setVisible (usingCapture);

    captureNote.setText (usingCapture ? describeCapture() : juce::String(), juce::dontSendNotification);
}

void AmpRig::ampChoiceChanged()
{
    const auto id = ampChoice.getSelectedId();

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
    }
    else if (id >= firstVoicingId && id < captureId)
    {
        capture.active.set (0.0f);
        amp.voicing.set ((float) (id - firstVoicingId));
        applyMatch();
    }

    refreshAmpChoices();
}

void AmpRig::applyMatch()
{
    if (! matchButton.getToggleState() || capture.isInUse())
        return;

    const auto match = getMatchingCabinet (amp.voicing.getIndex());

    if (cabinet.getBuiltInIndex() != match)
    {
        cabinet.selectBuiltIn (match);
        cabNote.setText ({}, juce::dontSendNotification);
        refreshCabChoices();
        repaint();
    }
}

void AmpRig::chooseCaptureFile()
{
    const auto startFolder = capture.getLoadedFile().existsAsFile()
                                 ? capture.getLoadedFile().getParentDirectory()
                                 : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    captureChooser = std::make_unique<juce::FileChooser> ("Choose an amp capture (.nam file)", startFolder, "*.nam");

    captureChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                 [this] (const juce::FileChooser& chooser)
                                 {
                                     const auto file = chooser.getResult();

                                     // An empty result means the dialog was cancelled.
                                     if (file != juce::File())
                                     {
                                         const auto error = capture.loadFile (file);

                                         if (error.isNotEmpty())
                                         {
                                             refreshAmpChoices();
                                             juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                                                     "Could not load the capture", error);
                                             return;
                                         }

                                         capture.active.set (1.0f);

                                         // A capture that includes the speaker would get a second
                                         // one from the cabinet, so switch that off. The user can
                                         // switch it back on.
                                         if (capture.getInfo().includesCabinet())
                                             cabinet.enabled.set (0.0f);
                                     }

                                     refreshAmpChoices();
                                     repaint();
                                 });
}

juce::String AmpRig::describeCapture() const
{
    if (! capture.isLoaded())
        return {};

    const auto info = capture.getInfo();
    juce::StringArray lines;
    lines.add (info.name + ", made at " + juce::String (info.sampleRate / 1000.0, 1) + " kHz.");

    if (info.includesCabinet())
        lines.add ("It includes the speaker cabinet, so the cabinet is normally off (its light). Switch it on to add a second one.");
    else if (info.gearType.isNotEmpty())
        lines.add ("It does not include a speaker cabinet, so keep the cabinet on.");
    else
        lines.add ("The file does not say whether it includes a speaker cabinet. If it sounds muffled, switch the cabinet off.");

    if (! info.hasLoudness)
        lines.add ("The file does not report its loudness, so set the level with Output.");

    lines.add ("Input sets how hard the capture is driven.");
    return lines.joinIntoString ("\n");
}

//==============================================================================
void AmpRig::refreshCabChoices()
{
    cabChoice.clear (juce::dontSendNotification);
    cabChoice.addItemList (Cabinet::getBuiltInNames(), firstBuiltInId);
    cabChoice.addSeparator();

    if (cabinet.getBuiltInIndex() < 0)
        cabChoice.addItem ("File: " + cabinet.getCurrentName(), loadedFileId);

    cabChoice.addItem ("Load impulse response file...", loadFileId);

    cabChoice.setSelectedId (cabinet.getBuiltInIndex() >= 0 ? firstBuiltInId + cabinet.getBuiltInIndex()
                                                            : (int) loadedFileId,
                             juce::dontSendNotification);

    if (cabNote.getText().isEmpty() || cabNote.getText().startsWith ("Built-in"))
        cabNote.setText ("Built-in cabinets are approximations. Load an IR file for the most realistic sound.",
                         juce::dontSendNotification);
}

void AmpRig::cabChoiceChanged()
{
    const auto id = cabChoice.getSelectedId();

    if (id == loadFileId)
    {
        chooseCabFile();
        return;
    }

    if (id >= firstBuiltInId && id < loadedFileId)
    {
        // Choosing a cabinet yourself means you want to keep it.
        if (matchButton.getToggleState() && id - firstBuiltInId != getMatchingCabinet (amp.voicing.getIndex()))
        {
            matchButton.setToggleState (false, juce::dontSendNotification);
            settings.setValue (matchSettingKey, false);
        }

        cabinet.selectBuiltIn (id - firstBuiltInId);
        refreshCabChoices();
        repaint();
    }
}

void AmpRig::chooseCabFile()
{
    const auto startFolder = cabinet.getLoadedFile().existsAsFile()
                                 ? cabinet.getLoadedFile().getParentDirectory()
                                 : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    cabChooser = std::make_unique<juce::FileChooser> ("Choose an impulse response file", startFolder,
                                                      "*.wav;*.aif;*.aiff;*.flac");

    cabChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [this] (const juce::FileChooser& chooser)
                             {
                                 const auto file = chooser.getResult();

                                 // An empty result means the dialog was cancelled.
                                 if (file != juce::File())
                                 {
                                     const auto error = cabinet.loadFile (file);

                                     if (error.isNotEmpty())
                                     {
                                         cabNote.setColour (juce::Label::textColourId, Theme::warning);
                                         cabNote.setText (error, juce::dontSendNotification);
                                     }
                                     else
                                     {
                                         // A loaded file is your own cabinet, so keep it.
                                         matchButton.setToggleState (false, juce::dontSendNotification);
                                         settings.setValue (matchSettingKey, false);
                                         cabNote.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.55f));
                                         cabNote.setText ("Loaded " + file.getFileName() + ".", juce::dontSendNotification);
                                     }
                                 }

                                 refreshCabChoices();
                                 repaint();
                             });
}
