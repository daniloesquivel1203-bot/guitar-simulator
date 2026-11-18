#include "AmpCapture.h"

#include <NAM/dsp.h>
#include <NAM/get_dsp.h>

#include <filesystem>
#include <stdexcept>

namespace
{
/** Most captures are made at 48 kHz, so that is assumed when a file does not say. */
constexpr double defaultCaptureRate = 48000.0;

/** Zeros held in the output converter so it never runs short of audio. */
constexpr int converterPriming = 16;

constexpr double gainSmoothingSeconds = 0.05;
constexpr double swapFadeSeconds = 0.03;

/** The tone knobs go from -12 dB at 0 to +12 dB at 10. */
float knobToDb (float knob) noexcept   { return (knob - 5.0f) * 2.4f; }

std::filesystem::path toPath (const juce::File& file)
{
   #if JUCE_WINDOWS
    return std::filesystem::path (file.getFullPathName().toWideCharPointer());
   #else
    return std::filesystem::path (file.getFullPathName().toStdString());
   #endif
}
}

//==============================================================================
struct AmpCapture::Model
{
    std::unique_ptr<nam::DSP> dsp;

    /** Brings captures with a known loudness to the common target volume. */
    float levellingGain = 1.0f;

    /** Set when the capture's rate differs from the device's. */
    bool converting = false;
    RateConverter toCapture, fromCapture;

    std::vector<float> captureInput, captureOutput;
};

//==============================================================================
AmpCapture::AmpCapture() = default;
AmpCapture::~AmpCapture() = default;

std::unique_ptr<AmpCapture::Model> AmpCapture::makeModel (std::unique_ptr<nam::DSP> dsp) const
{
    auto newModel = std::make_unique<Model>();

    const auto deviceRate = sampleRate;
    const auto blockSize = juce::jmax (1, maxBlockSize);

    auto captureRate = dsp->GetExpectedSampleRate();

    if (captureRate <= 0.0)
        captureRate = defaultCaptureRate;

    // Some newer captures can run at any rate. The rest need converting when
    // the rates differ by more than rounding.
    newModel->converting = ! dsp->SupportsArbitrarySampleRate() && std::abs (captureRate - deviceRate) > 0.5;

    auto captureBlockSize = blockSize;

    if (newModel->converting)
    {
        newModel->toCapture.prepare (deviceRate, captureRate, blockSize);
        captureBlockSize = newModel->toCapture.getMaxOutputFor (blockSize);
        newModel->fromCapture.prepare (captureRate, deviceRate, captureBlockSize, converterPriming);
    }

    newModel->captureInput.assign ((size_t) captureBlockSize, 0.0f);
    newModel->captureOutput.assign ((size_t) captureBlockSize, 0.0f);

    // Reset() sets the rate and buffer size and settles the network, so its
    // first real output is not a thump. It allocates, so it never runs on the audio thread.
    dsp->Reset (newModel->converting ? captureRate : deviceRate, captureBlockSize);

    if (dsp->HasLoudness())
        newModel->levellingGain = (float) juce::Decibels::decibelsToGain (targetLoudnessDb - dsp->GetLoudness());

    newModel->dsp = std::move (dsp);
    return newModel;
}

juce::String AmpCapture::loadFile (const juce::File& file)
{
    if (loaded && file == loadedFile)
        return {};

    if (! file.existsAsFile())
        return "The file \"" + file.getFileName() + "\" could not be found.";

    std::unique_ptr<nam::DSP> dsp;
    juce::String reason;

    try
    {
        dsp = nam::get_dsp (toPath (file));
    }
    catch (const std::exception& error)
    {
        reason = error.what();
    }
    catch (...)
    {
    }

    if (dsp == nullptr)
        return "\"" + file.getFileName() + "\" is not a capture this app can read. Use a .nam file from Neural Amp Modeler."
               + (reason.isNotEmpty() ? " (" + reason + ")" : juce::String());

    if (dsp->NumInputChannels() != 1 || dsp->NumOutputChannels() != 1)
        return "\"" + file.getFileName() + "\" is not a mono capture, so it cannot be used here.";

    std::unique_ptr<Model> newModel;

    try
    {
        newModel = makeModel (std::move (dsp));
    }
    catch (const std::exception& error)
    {
        return "\"" + file.getFileName() + "\" could not be started (" + juce::String (error.what()) + ").";
    }

    // The file is JSON. The library reads the network, and the extra details
    // shown to the user come from the same file.
    Info newInfo;
    newInfo.name = file.getFileNameWithoutExtension();
    newInfo.sampleRate = newModel->dsp->GetExpectedSampleRate() > 0.0 ? newModel->dsp->GetExpectedSampleRate()
                                                                      : defaultCaptureRate;
    newInfo.hasLoudness = newModel->dsp->HasLoudness();

    const auto json = juce::JSON::parse (file);
    newInfo.gearType = json["metadata"]["gear_type"].toString().toLowerCase();

    {
        const juce::SpinLock::ScopedLockType lock (modelLock);
        std::swap (model, newModel);
    }

    // The old model, now in newModel, is freed here on this thread rather than on the audio thread.
    newModel.reset();

    fadeInPending.store (true);
    loaded = true;
    loadedFile = file;
    info = newInfo;
    return {};
}

void AmpCapture::unload()
{
    std::unique_ptr<Model> oldModel;

    {
        const juce::SpinLock::ScopedLockType lock (modelLock);
        std::swap (model, oldModel);
    }

    loaded = false;
    loadedFile = juce::File();
    info = {};
}

void AmpCapture::prepare (double newSampleRate, int newMaxBlockSize)
{
    sampleRate = newSampleRate;
    maxBlockSize = newMaxBlockSize;

    inputGain.reset (sampleRate, gainSmoothingSeconds);
    outputGain.reset (sampleRate, gainSmoothingSeconds);
    swapFade.reset (sampleRate, swapFadeSeconds);

    lastBass = lastMid = lastTreble = -1.0f;
    updateTone();

    // The capture has to be set up again for the new rate and block size.
    std::unique_ptr<Model> oldModel;

    {
        const juce::SpinLock::ScopedLockType lock (modelLock);
        oldModel = std::move (model);
    }

    if (oldModel != nullptr)
    {
        try
        {
            auto newModel = makeModel (std::move (oldModel->dsp));
            const juce::SpinLock::ScopedLockType lock (modelLock);
            model = std::move (newModel);
        }
        catch (const std::exception&)
        {
            loaded = false;
        }
    }

    reset();
}

void AmpCapture::reset()
{
    bassFilter.reset();
    midFilter.reset();
    trebleFilter.reset();

    inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (input.get()));
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (output.get()));
    swapFade.setCurrentAndTargetValue (1.0f);

    const juce::SpinLock::ScopedLockType lock (modelLock);

    if (model != nullptr && model->converting)
    {
        model->toCapture.reset();
        model->fromCapture.reset();
    }
}

void AmpCapture::updateTone()
{
    const auto newBass = bass.get(), newMid = mid.get(), newTreble = treble.get();

    if (! juce::exactlyEqual (newBass, lastBass))
    {
        lastBass = newBass;
        bassFilter.coefficients = Coefficients::makeLowShelf (sampleRate, 120.0f, 0.7f,
                                                              juce::Decibels::decibelsToGain (knobToDb (newBass)));
    }

    if (! juce::exactlyEqual (newMid, lastMid))
    {
        lastMid = newMid;
        midFilter.coefficients = Coefficients::makePeakFilter (sampleRate, 700.0f, 0.7f,
                                                               juce::Decibels::decibelsToGain (knobToDb (newMid)));
    }

    if (! juce::exactlyEqual (newTreble, lastTreble))
    {
        lastTreble = newTreble;
        trebleFilter.coefficients = Coefficients::makeHighShelf (sampleRate, 3000.0f, 0.7f,
                                                                 juce::Decibels::decibelsToGain (knobToDb (newTreble)));
    }
}

void AmpCapture::process (float* samples, int numSamples) noexcept
{
    const juce::SpinLock::ScopedTryLockType lock (modelLock);

    if (! lock.isLocked() || model == nullptr)
    {
        // A capture is being swapped in. This lasts one block at most.
        juce::FloatVectorOperations::clear (samples, numSamples);
        return;
    }

    if (fadeInPending.exchange (false))
    {
        swapFade.setCurrentAndTargetValue (0.0f);
        swapFade.setTargetValue (1.0f);
    }

    updateTone();
    inputGain.setTargetValue (juce::Decibels::decibelsToGain (input.get()));
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (output.get()));

    for (int i = 0; i < numSamples; ++i)
        samples[i] *= inputGain.getNextValue();

    auto& m = *model;

    if (m.converting)
    {
        m.toCapture.push (samples, numSamples);
        const auto captureSamples = m.toCapture.pull (m.captureInput.data(), (int) m.captureInput.size());

        if (captureSamples > 0)
        {
            float* in[] = { m.captureInput.data() };
            float* out[] = { m.captureOutput.data() };
            m.dsp->process (in, out, captureSamples);
            m.fromCapture.push (m.captureOutput.data(), captureSamples);
        }

        const auto produced = m.fromCapture.pull (samples, numSamples);

        // The priming in the converter makes this very unlikely. If it does
        // happen, pad the start with silence rather than leave old samples.
        if (produced < numSamples)
        {
            std::copy_backward (samples, samples + produced, samples + numSamples);
            juce::FloatVectorOperations::clear (samples, numSamples - produced);
        }
    }
    else
    {
        float* in[] = { samples };
        float* out[] = { m.captureOutput.data() };
        m.dsp->process (in, out, numSamples);
        std::copy (m.captureOutput.begin(), m.captureOutput.begin() + numSamples, samples);
    }

    for (int i = 0; i < numSamples; ++i)
    {
        auto sample = trebleFilter.processSample (midFilter.processSample (bassFilter.processSample (samples[i])));
        samples[i] = sample * m.levellingGain * outputGain.getNextValue() * swapFade.getNextValue();
    }
}
