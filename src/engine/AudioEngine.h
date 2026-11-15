#pragma once

#include "SignalChain.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>

/** Owns the audio device and feeds it through the SignalChain.

    Works with any interface the operating system exposes. Nothing here is
    specific to one device. The chosen device, channel and levels are saved to
    the settings file and restored on the next launch, and the engine tries to
    reopen the device if it disappears while the app is running.

    Sends a change message whenever the device or its settings change.
*/
class AudioEngine final : public juce::ChangeBroadcaster,
                          private juce::AudioIODeviceCallback,
                          private juce::ChangeListener,
                          private juce::Timer
{
public:
    struct Status
    {
        bool running = false;
        juce::String deviceType;
        juce::String deviceName;
        double sampleRate = 0.0;
        int bufferSize = 0;
        int inputLatencySamples = 0;
        int outputLatencySamples = 0;
        double roundTripMs = 0.0;
        double cpuUsage = 0.0;
        int xruns = 0;
        juce::String message;
    };

    explicit AudioEngine (juce::PropertiesFile& settingsToUse);
    ~AudioEngine() override;

    juce::AudioDeviceManager& getDeviceManager() noexcept   { return deviceManager; }
    SignalChain& getChain() noexcept                        { return chain; }

    Status getStatus() const;

    /** Names of the input channels currently enabled on the device. */
    juce::StringArray getActiveInputChannelNames() const;

    /** Index into getActiveInputChannelNames() of the channel the guitar is on. */
    int getInputChannel() const noexcept   { return inputChannel.load(); }
    void setInputChannel (int index);

    void setInputGainDb (float db);
    void setOutputGainDb (float db);

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceError (const juce::String& errorMessage) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void preferAsioOnFirstRun();
    void rememberWorkingDevice();
    void tryToRecoverDevice();

    juce::PropertiesFile& settings;
    juce::AudioDeviceManager deviceManager;
    SignalChain chain;

    std::atomic<int> inputChannel { 0 };
    std::atomic<juce::uint32> callbackCount { 0 };
    std::atomic<bool> deviceErrored { false };

    juce::uint32 lastSeenCallbackCount = 0;
    int stalledTicks = 0;
    bool recovering = false;
    bool hasWorkingSetup = false;
    juce::String workingDeviceType;
    juce::AudioDeviceManager::AudioDeviceSetup workingSetup;
    juce::String lastMessage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
