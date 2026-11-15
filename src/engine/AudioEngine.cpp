#include "AudioEngine.h"

namespace
{
const juce::Identifier deviceStateKey { "audioDeviceState" };
const char* const inputChannelKey = "inputChannel";
const char* const inputGainKey = "inputGainDb";
const char* const outputGainKey = "outputGainDb";

constexpr int watchdogIntervalMs = 1000;
constexpr int stalledTicksBeforeRecovery = 3;
}

AudioEngine::AudioEngine (juce::PropertiesFile& settingsToUse)
    : settings (settingsToUse)
{
    inputChannel.store (juce::jmax (0, settings.getIntValue (inputChannelKey, 0)));
    chain.setInputGainDb ((float) settings.getDoubleValue (inputGainKey, 0.0));
    chain.setOutputGainDb ((float) settings.getDoubleValue (outputGainKey, -6.0));

    const auto savedState = settings.getXmlValue (deviceStateKey.toString());

    // Ask for two channels each way by default. More can be enabled in the settings screen.
    lastMessage = deviceManager.initialise (2, 2, savedState.get(), true);

    if (savedState == nullptr)
        preferAsioOnFirstRun();

    rememberWorkingDevice();

    deviceManager.addAudioCallback (this);
    deviceManager.addChangeListener (this);
    startTimer (watchdogIntervalMs);
}

AudioEngine::~AudioEngine()
{
    stopTimer();
    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
    settings.saveIfNeeded();
}

void AudioEngine::preferAsioOnFirstRun()
{
    // On Windows, ASIO has far lower latency than the default driver type, so
    // pick it when a driver is installed. This does nothing on other systems.
    for (auto* type : deviceManager.getAvailableDeviceTypes())
    {
        if (type->getTypeName() != "ASIO")
            continue;

        type->scanForDevices();

        if (! type->getDeviceNames().isEmpty())
        {
            deviceManager.setCurrentAudioDeviceType ("ASIO", true);

            // If the ASIO driver could not be opened, fall back to the system default.
            if (deviceManager.getCurrentAudioDevice() == nullptr)
                lastMessage = deviceManager.initialise (2, 2, nullptr, true);
        }

        return;
    }
}

AudioEngine::Status AudioEngine::getStatus() const
{
    Status status;
    status.message = lastMessage;

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        status.running = device->isPlaying();
        status.deviceType = device->getTypeName();
        status.deviceName = device->getName();
        status.sampleRate = device->getCurrentSampleRate();
        status.bufferSize = device->getCurrentBufferSizeSamples();
        status.inputLatencySamples = device->getInputLatencyInSamples();
        status.outputLatencySamples = device->getOutputLatencyInSamples();
        status.cpuUsage = deviceManager.getCpuUsage();
        status.xruns = deviceManager.getXRunCount();

        if (status.sampleRate > 0.0)
            status.roundTripMs = 1000.0 * (status.inputLatencySamples + status.outputLatencySamples)
                                 / status.sampleRate;
    }

    return status;
}

juce::StringArray AudioEngine::getActiveInputChannelNames() const
{
    juce::StringArray names;

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const auto allNames = device->getInputChannelNames();
        const auto active = device->getActiveInputChannels();

        for (int i = 0; i < allNames.size(); ++i)
            if (active[i])
                names.add (allNames[i]);
    }

    return names;
}

void AudioEngine::setInputChannel (int index)
{
    inputChannel.store (juce::jmax (0, index));
    settings.setValue (inputChannelKey, inputChannel.load());
    sendChangeMessage();
}

void AudioEngine::setInputGainDb (float db)
{
    chain.setInputGainDb (db);
    settings.setValue (inputGainKey, (double) chain.getInputGainDb());
}

void AudioEngine::setOutputGainDb (float db)
{
    chain.setOutputGainDb (db);
    settings.setValue (outputGainKey, (double) chain.getOutputGainDb());
}

//==============================================================================
void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                                    float* const* outputChannelData, int numOutputChannels,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;
    callbackCount.fetch_add (1);

    if (numOutputChannels <= 0)
        return;

    const float* guitar = numInputChannels > 0
                              ? inputChannelData[juce::jlimit (0, numInputChannels - 1, inputChannel.load())]
                              : nullptr;

    // The mono guitar signal goes to the first two outputs so it is heard in both ears.
    float* left = outputChannelData[0];
    float* right = numOutputChannels > 1 ? outputChannelData[1] : left;

    chain.process (guitar, left, right, numSamples);

    for (int channel = 2; channel < numOutputChannels; ++channel)
        juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    chain.prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
    deviceErrored.store (false);
}

void AudioEngine::audioDeviceStopped()
{
    chain.reset();
}

void AudioEngine::audioDeviceError (const juce::String&)
{
    deviceErrored.store (true);
}

//==============================================================================
void AudioEngine::rememberWorkingDevice()
{
    if (deviceManager.getCurrentAudioDevice() == nullptr)
        return;

    workingDeviceType = deviceManager.getCurrentAudioDeviceType();
    workingSetup = deviceManager.getAudioDeviceSetup();
    hasWorkingSetup = true;
    lastMessage.clear();

    if (const auto state = deviceManager.createStateXml())
        settings.setValue (deviceStateKey.toString(), state.get());
}

void AudioEngine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Only save a state that has a device, so a lost device is not forgotten.
    if (! recovering)
        rememberWorkingDevice();

    sendChangeMessage();
}

void AudioEngine::timerCallback()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    const auto count = callbackCount.load();

    // A device that is open but no longer delivering audio has usually been unplugged.
    if (device != nullptr && count == lastSeenCallbackCount)
        ++stalledTicks;
    else
        stalledTicks = 0;

    lastSeenCallbackCount = count;

    const bool lost = device == nullptr
                      || deviceErrored.load()
                      || stalledTicks >= stalledTicksBeforeRecovery;

    if (lost && hasWorkingSetup)
        tryToRecoverDevice();
}

void AudioEngine::tryToRecoverDevice()
{
    const juce::ScopedValueSetter<bool> scope (recovering, true);

    if (deviceManager.getCurrentAudioDevice() != nullptr)
        deviceManager.closeAudioDevice();

    stalledTicks = 0;
    lastMessage = "Audio device disconnected. Waiting for it to return.";

    if (deviceManager.getCurrentAudioDeviceType() != workingDeviceType)
        deviceManager.setCurrentAudioDeviceType (workingDeviceType, false);

    if (auto* type = deviceManager.getCurrentDeviceTypeObject())
    {
        type->scanForDevices();

        const bool outputPresent = workingSetup.outputDeviceName.isEmpty()
                                   || type->getDeviceNames (false).contains (workingSetup.outputDeviceName);
        const bool inputPresent = workingSetup.inputDeviceName.isEmpty()
                                  || type->getDeviceNames (true).contains (workingSetup.inputDeviceName);

        if (outputPresent && inputPresent
            && deviceManager.setAudioDeviceSetup (workingSetup, true).isEmpty()
            && deviceManager.getCurrentAudioDevice() != nullptr)
        {
            deviceErrored.store (false);
            lastMessage.clear();
        }
    }

    sendChangeMessage();
}
