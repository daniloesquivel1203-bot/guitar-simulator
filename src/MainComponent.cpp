#include "MainComponent.h"

MainComponent::MainComponent()
{
    setSize (900, 560);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    auto area = getLocalBounds();

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (32.0f, juce::Font::bold));
    g.drawText ("Guitar Simulator", area.removeFromTop (getHeight() / 2).removeFromBottom (48),
                juce::Justification::centred);

    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.setFont (juce::FontOptions (16.0f));
    g.drawText ("Version " JUCE_APPLICATION_VERSION_STRING "  -  the app skeleton is working. Audio comes next.",
                area.removeFromTop (32), juce::Justification::centred);
}
