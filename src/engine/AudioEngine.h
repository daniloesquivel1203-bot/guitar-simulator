#pragma once

#include "Practice.h"
#include "Presets.h"
#include "SignalChain.h"
#include "Tuner.h"

#include <array>

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
    PresetManager& getPresets() noexcept                    { return presets; }
    TunerTap& getTunerTap() noexcept                        { return tunerTap; }
    PracticeTools& getPractice() noexcept                   { return practice; }

    /** The app's settings file, for panels that remember their own choices. */
    juce::PropertiesFile& getSettings() noexcept            { return settings; }

    /** The tuner's remembered choices: which tuning, and which string (-1 = pick automatically). */
    int getTunerTuning() const          { return settings.getIntValue ("tuner.tuning", 0); }
    void setTunerTuning (int index)     { settings.setValue ("tuner.tuning", index); }
    int getTunerString() const          { return settings.getIntValue ("tuner.string", -1); }
    void setTunerString (int index)     { settings.setValue ("tuner.string", index); }

    Status getStatus() const;

    /** Names of every input channel the device has, enabled or not. */
    juce::StringArray getInputChannelNames() const;

    /** The device input channel the guitar is on, counted from 0 across all of
        the device's inputs. The engine keeps every input switched on, so this
        always matches the numbering on the interface. */
    int getInputChannel() const noexcept   { return inputChannel.load(); }
    void setInputChannel (int deviceChannelIndex);

    /** False when the chosen channel does not exist on the current device or is
        switched off, in which case the app outputs silence. */
    bool isInputChannelAvailable() const noexcept   { return inputBufferIndex.load() >= 0; }

    /** Highest level seen on one device input since this was last called, as
        linear gain. Lets the settings screen show which input carries a signal. */
    float readInputChannelPeak (int deviceChannelIndex) noexcept;

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
    void loadToneSettings();
    void saveToneSettings();
    void rememberWorkingDevice();
    void updateInputRouting();
    void enableAllInputs();
    void tryToRecoverDevice();

    juce::PropertiesFile& settings;
    juce::AudioDeviceManager deviceManager;
    SignalChain chain;
    PresetManager presets;
    TunerTap tunerTap;
    PracticeTools practice;

    std::atomic<int> inputChannel { 0 };

    // Position of the chosen channel within the buffers the device delivers,
    // which only contain enabled channels. -1 means the channel is unavailable.
    std::atomic<int> inputBufferIndex { -1 };
    static constexpr int maxMeteredInputs = 32;
    std::array<std::atomic<float>, maxMeteredInputs> inputChannelPeaks {};

    std::atomic<juce::uint32> callbackCount { 0 };
    std::atomic<bool> deviceErrored { false };

    juce::uint32 lastSeenCallbackCount = 0;
    int stalledTicks = 0;
    bool recovering = false;
    bool hasWorkingSetup = false;
    juce::String workingDeviceType;
    juce::AudioDeviceManager::AudioDeviceSetup workingSetup;
    juce::String lastMessage;
    juce::String inputsEnabledForDevice;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
