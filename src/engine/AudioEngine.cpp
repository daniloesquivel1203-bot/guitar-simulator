#include "AudioEngine.h"

#include "InputRouting.h"

namespace
{
const juce::Identifier deviceStateKey { "audioDeviceState" };
const char* const inputChannelKey = "inputChannel";
const char* const inputGainKey = "inputGainDb";
const char* const outputGainKey = "outputGainDb";
const char* const cabinetSourceKey = "cab.source";

constexpr int watchdogIntervalMs = 1000;
constexpr int stalledTicksBeforeRecovery = 3;
}

AudioEngine::AudioEngine (juce::PropertiesFile& settingsToUse)
    : settings (settingsToUse)
{
    inputChannel.store (juce::jmax (0, settings.getIntValue (inputChannelKey, 0)));
    chain.setInputGainDb ((float) settings.getDoubleValue (inputGainKey, 0.0));
    chain.setOutputGainDb ((float) settings.getDoubleValue (outputGainKey, -6.0));

    loadToneSettings();

    const auto savedState = settings.getXmlValue (deviceStateKey.toString());

    // Ask for two channels each way by default. More can be enabled in the settings screen.
    lastMessage = deviceManager.initialise (2, 2, savedState.get(), true);

    if (savedState == nullptr)
        preferAsioOnFirstRun();

    enableAllInputs();
    rememberWorkingDevice();
    updateInputRouting();

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
    saveToneSettings();
    settings.saveIfNeeded();
}

void AudioEngine::loadToneSettings()
{
    for (auto* parameter : chain.getAmp().getParameters())
        parameter->set ((float) settings.getDoubleValue (parameter->id, parameter->defaultValue));

    for (auto* parameter : chain.getCabinet().getParameters())
        parameter->set ((float) settings.getDoubleValue (parameter->id, parameter->defaultValue));

    chain.getCabinet().restoreFromDescription (settings.getValue (cabinetSourceKey));
}

void AudioEngine::saveToneSettings()
{
    // setValue only marks the file as changed when a value really differs.
    for (auto* parameter : chain.getAmp().getParameters())
        settings.setValue (parameter->id, (double) parameter->get());

    for (auto* parameter : chain.getCabinet().getParameters())
        settings.setValue (parameter->id, (double) parameter->get());

    settings.setValue (cabinetSourceKey, chain.getCabinet().getSourceDescription());
}

float AudioEngine::readInputChannelPeak (int deviceChannelIndex) noexcept
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const auto bufferIndex = findInputBufferIndex (device->getActiveInputChannels(),
                                                       device->getInputChannelNames().size(),
                                                       deviceChannelIndex);

        if (juce::isPositiveAndBelow (bufferIndex, maxMeteredInputs))
            return inputChannelPeaks[(size_t) bufferIndex].exchange (0.0f);
    }

    return 0.0f;
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

juce::StringArray AudioEngine::getInputChannelNames() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getInputChannelNames();

    return {};
}

void AudioEngine::setInputChannel (int deviceChannelIndex)
{
    inputChannel.store (juce::jmax (0, deviceChannelIndex));
    settings.setValue (inputChannelKey, inputChannel.load());

    updateInputRouting();
    sendChangeMessage();
}

void AudioEngine::enableAllInputs()
{
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return;

    // Opening only some of a device's inputs makes the channel numbering hard to
    // follow, so every input is always open and the guitar input picker chooses
    // among them. Try once per device, in case a driver refuses the request.
    const auto deviceId = device->getTypeName() + "/" + device->getName();
    const auto numInputs = device->getInputChannelNames().size();

    if (numInputs == 0
        || device->getActiveInputChannels().countNumberOfSetBits() == numInputs
        || deviceId == inputsEnabledForDevice)
        return;

    inputsEnabledForDevice = deviceId;

    auto setup = deviceManager.getAudioDeviceSetup();
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setRange (0, numInputs, true);
    deviceManager.setAudioDeviceSetup (setup, true);
}

void AudioEngine::updateInputRouting()
{
    int bufferIndex = -1;

    if (auto* device = deviceManager.getCurrentAudioDevice())
        bufferIndex = findInputBufferIndex (device->getActiveInputChannels(),
                                            device->getInputChannelNames().size(),
                                            inputChannel.load());

    inputBufferIndex.store (bufferIndex);
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

    for (int channel = 0; channel < juce::jmin (numInputChannels, (int) maxMeteredInputs); ++channel)
    {
        const auto range = juce::FloatVectorOperations::findMinAndMax (inputChannelData[channel], numSamples);
        const auto peak = juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd()));
        auto& stored = inputChannelPeaks[(size_t) channel];

        if (peak > stored.load (std::memory_order_relaxed))
            stored.store (peak, std::memory_order_relaxed);
    }

    if (numOutputChannels <= 0)
        return;

    // An unavailable channel gives silence instead of quietly using a different input.
    const auto bufferIndex = inputBufferIndex.load();
    const float* guitar = juce::isPositiveAndBelow (bufferIndex, numInputChannels)
                              ? inputChannelData[bufferIndex]
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

    // Channel layout may have changed. Resolve it again before audio starts.
    inputBufferIndex.store (findInputBufferIndex (device->getActiveInputChannels(),
                                                  device->getInputChannelNames().size(),
                                                  inputChannel.load()));
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
    {
        enableAllInputs();
        rememberWorkingDevice();
    }

    updateInputRouting();
    sendChangeMessage();
}

void AudioEngine::timerCallback()
{
    saveToneSettings();

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

    updateInputRouting();
    sendChangeMessage();
}
