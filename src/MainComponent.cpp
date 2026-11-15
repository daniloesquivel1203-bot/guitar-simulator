#include "MainComponent.h"

#include "ui/AudioSettingsPanel.h"

namespace
{
constexpr int meterRefreshHz = 30;
constexpr int stripWidth = 120;
constexpr int headerHeight = 72;

const juce::Colour background { 0xff22282d };
const juce::Colour panel { 0xff2b3238 };
const juce::Colour good { 0xff3ecf8e };
const juce::Colour warning { 0xfff5a524 };
}

MainComponent::MainComponent (AudioEngine& engineToUse)
    : engine (engineToUse),
      ampPanel (engine.getChain().getAmp()),
      cabinetPanel (engine.getChain().getCabinet())
{
    addAndMakeVisible (ampPanel);
    addAndMakeVisible (cabinetPanel);

    title.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    addAndMakeVisible (title);

    statusLine.setFont (juce::FontOptions (14.0f));
    statusLine.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (statusLine);

    detailLine.setFont (juce::FontOptions (13.0f));
    detailLine.setJustificationType (juce::Justification::centredRight);
    detailLine.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
    addAndMakeVisible (detailLine);

    settingsButton.onClick = [this] { showAudioSettings(); };
    addAndMakeVisible (settingsButton);

    for (auto* heading : { &inputHeading, &outputHeading })
    {
        heading->setFont (juce::FontOptions (12.0f, juce::Font::bold));
        heading->setJustificationType (juce::Justification::centred);
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

    setSize (960, 560);
}

MainComponent::~MainComponent()
{
    engine.removeChangeListener (this);
}

void MainComponent::styleGainSlider (juce::Slider& slider, double minDb, double maxDb, double valueDb)
{
    slider.setSliderStyle (juce::Slider::LinearVertical);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 20);
    slider.setRange (minDb, maxDb, 0.1);
    slider.setTextValueSuffix (" dB");
    slider.setDoubleClickReturnValue (true, 0.0);
    slider.setValue (valueDb, juce::dontSendNotification);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (background);

    auto area = getLocalBounds();
    area.removeFromTop (headerHeight);

    g.setColour (panel);
    g.fillRect (area.removeFromLeft (stripWidth));
    g.fillRect (area.removeFromRight (stripWidth));
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (headerHeight).reduced (16, 12);
    settingsButton.setBounds (header.removeFromRight (130).withSizeKeepingCentre (130, 30));
    header.removeFromRight (16);
    title.setBounds (header.removeFromLeft (220));
    statusLine.setBounds (header.removeFromTop (header.getHeight() / 2));
    detailLine.setBounds (header);

    auto layoutStrip = [] (juce::Rectangle<int> strip, juce::Label& heading, LevelMeter& meter,
                           juce::Slider& slider, juce::Component* footer)
    {
        strip = strip.reduced (12);
        heading.setBounds (strip.removeFromTop (20));
        strip.removeFromTop (8);

        // Reserve the footer row on both strips so the meters and sliders line up.
        auto footerRow = strip.removeFromBottom (24);
        strip.removeFromBottom (4);

        if (footer != nullptr)
            footer->setBounds (footerRow);

        // Stop the meter above the slider's text box.
        meter.setBounds (strip.removeFromLeft (20).withTrimmedBottom (24));
        strip.removeFromLeft (4);
        slider.setBounds (strip);
    };

    layoutStrip (area.removeFromLeft (stripWidth), inputHeading, inputMeter, inputGain, nullptr);
    layoutStrip (area.removeFromRight (stripWidth), outputHeading, outputMeter, outputGain, &muteButton);

    area = area.reduced (16);
    cabinetPanel.setBounds (area.removeFromBottom (area.getHeight() * 2 / 5));
    area.removeFromBottom (16);
    ampPanel.setBounds (area);
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

    // The CPU and dropout figures change continuously, so refresh them a few times a second.
    if (++ticksSinceStatusRefresh >= meterRefreshHz / 3)
        refreshStatus();
}

void MainComponent::refreshStatus()
{
    ticksSinceStatusRefresh = 0;
    const auto status = engine.getStatus();

    if (! status.running)
    {
        statusLine.setColour (juce::Label::textColourId, warning);
        statusLine.setText (status.message.isNotEmpty() ? status.message
                                                        : "No audio device. Open Audio settings to choose one.",
                            juce::dontSendNotification);
        detailLine.setText ({}, juce::dontSendNotification);
        return;
    }

    const auto inputs = engine.getInputChannelNames();
    const auto channel = engine.getInputChannel();
    const bool inputOk = engine.isInputChannelAvailable();

    statusLine.setColour (juce::Label::textColourId, inputOk ? good : warning);
    statusLine.setText (status.deviceName + " (" + status.deviceType + ")  |  "
                            + (inputOk ? "guitar on input " + juce::String (channel + 1) + ": " + inputs[channel]
                                       : juce::String ("no guitar input selected. Open Audio settings.")),
                        juce::dontSendNotification);

    detailLine.setText (juce::String (status.sampleRate / 1000.0, 1) + " kHz  |  buffer "
                            + juce::String (status.bufferSize) + "  |  latency "
                            + juce::String (status.roundTripMs, 1) + " ms round trip (driver reported)  |  CPU "
                            + juce::String (juce::roundToInt (status.cpuUsage * 100.0)) + "%  |  dropouts "
                            + juce::String (juce::jmax (0, status.xruns)),
                        juce::dontSendNotification);
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
