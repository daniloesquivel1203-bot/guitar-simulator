#include "CustomPedal.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace custom_blocks
{
const char* getTypeName (BlockType type) noexcept
{
    switch (type)
    {
        case BlockType::lowPass:    return "Low-pass filter";
        case BlockType::highPass:   return "High-pass filter";
        case BlockType::bandPass:   return "Band-pass filter";
        case BlockType::notch:      return "Notch filter";
        case BlockType::peak:       return "Peak (bell) EQ";
        case BlockType::lowShelf:   return "Low shelf EQ";
        case BlockType::highShelf:  return "High shelf EQ";
        case BlockType::gain:       return "Gain";
        case BlockType::clipper:    return "Clipper";
        case BlockType::lfo:        return "LFO (moves a knob)";
        case BlockType::envelope:   return "Envelope (follows your picking)";
        case BlockType::delay:      return "Delay line";
        case BlockType::allPass:    return "All-pass (phaser)";
        case BlockType::reducer:    return "Bit / rate reducer";
        case BlockType::split:      return "Split (start two paths)";
        case BlockType::pathB:      return "Path B (start the second path)";
        case BlockType::mix:        return "Mix (join the paths)";
        case BlockType::empty:
        case BlockType::numTypes:   break;
    }

    return "Empty";
}

const char* getShortName (BlockType type) noexcept
{
    switch (type)
    {
        case BlockType::lowPass:    return "LPF";
        case BlockType::highPass:   return "HPF";
        case BlockType::bandPass:   return "BPF";
        case BlockType::notch:      return "Notch";
        case BlockType::peak:       return "Peak";
        case BlockType::lowShelf:   return "Lo Shelf";
        case BlockType::highShelf:  return "Hi Shelf";
        case BlockType::gain:       return "Gain";
        case BlockType::clipper:    return "Clip";
        case BlockType::lfo:        return "LFO";
        case BlockType::envelope:   return "Env";
        case BlockType::delay:      return "Delay";
        case BlockType::allPass:    return "All-pass";
        case BlockType::reducer:    return "Crush";
        case BlockType::split:      return "Split";
        case BlockType::pathB:      return "Path B";
        case BlockType::mix:        return "Mix";
        case BlockType::empty:
        case BlockType::numTypes:   break;
    }

    return "";
}

const char* getDescription (BlockType type) noexcept
{
    switch (type)
    {
        case BlockType::lowPass:    return "Lets lows through and rolls off everything above the cutoff. Resonance adds a peak at the cutoff.";
        case BlockType::highPass:   return "Lets highs through and rolls off everything below the cutoff. Good for tightening low end.";
        case BlockType::bandPass:   return "Keeps only a band around the centre frequency. Narrow widths sound nasal, like a half-cocked wah.";
        case BlockType::notch:      return "Cuts a narrow slice around the centre frequency and leaves the rest alone.";
        case BlockType::peak:       return "Boosts or cuts a band around one frequency, like one band of a parametric EQ.";
        case BlockType::lowShelf:   return "Boosts or cuts everything below the frequency by the same amount.";
        case BlockType::highShelf:  return "Boosts or cuts everything above the frequency by the same amount.";
        case BlockType::gain:       return "Turns the signal up or down. Invert flips its polarity.";
        case BlockType::clipper:    return "Squashes peaks to add distortion. Put filters before it to shape what distorts, and after it to tame fizz.";
        case BlockType::lfo:        return "Makes no sound itself. It sweeps another block's main knob up and down at a steady rate: a filter for a wobble, a gain block for tremolo.";
        case BlockType::envelope:   return "Makes no sound itself. It moves another block's main knob as you play harder: a band-pass for auto-wah. Negative depth moves it the other way.";
        case BlockType::delay:      return "Repeats the signal after a delay. Short times with an LFO on Time give chorus and vibrato; long times give echoes.";
        case BlockType::allPass:    return "Shifts the timing of some frequencies and mixes that back in, cutting notches. Put an LFO on it for a phaser.";
        case BlockType::reducer:    return "Lowers the bit depth and sample rate for a gritty, lo-fi digital sound.";
        case BlockType::split:      return "Copies the signal into two paths. Blocks after it are path A, up to a Path B marker or the Mix. With no Path B, path B is the clean signal.";
        case BlockType::pathB:      return "Starts the second path. Blocks after it, up to the Mix, get the signal from the Split.";
        case BlockType::mix:        return "Joins the two paths back together. Blend sets how much of path B you hear.";
        case BlockType::empty:
        case BlockType::numTypes:   break;
    }

    return "";
}

void SvfStage::setCoefficients (BlockType type, float hz, float q, float gainDb, double sampleRate) noexcept
{
    constexpr auto pi = juce::MathConstants<float>::pi;
    hz = juce::jlimit (10.0f, (float) (sampleRate * 0.49), hz);
    q = juce::jmax (0.05f, q);

    auto g = std::tan (pi * hz / (float) sampleRate);
    auto k = 1.0f / q;
    const auto a = std::pow (10.0f, gainDb / 40.0f);

    switch (type)
    {
        case BlockType::lowPass:    m0 = 0.0f; m1 = 0.0f;                     m2 = 1.0f;  break;
        case BlockType::highPass:   m0 = 1.0f; m1 = -k;                       m2 = -1.0f; break;
        case BlockType::bandPass:   m0 = 0.0f; m1 = k;                        m2 = 0.0f;  break;   // peak of 1
        case BlockType::notch:      m0 = 1.0f; m1 = -k;                       m2 = 0.0f;  break;
        case BlockType::peak:       k = 1.0f / (q * a);
                                    m0 = 1.0f; m1 = k * (a * a - 1.0f);       m2 = 0.0f;  break;
        case BlockType::lowShelf:   g /= std::sqrt (a);
                                    m0 = 1.0f; m1 = k * (a - 1.0f);           m2 = a * a - 1.0f; break;
        case BlockType::highShelf:  g *= std::sqrt (a);
                                    m0 = a * a; m1 = k * (1.0f - a) * a;      m2 = 1.0f - a * a; break;
        case BlockType::empty:
        case BlockType::gain:
        case BlockType::clipper:
        case BlockType::lfo:
        case BlockType::envelope:
        case BlockType::delay:
        case BlockType::allPass:
        case BlockType::reducer:
        case BlockType::split:
        case BlockType::pathB:
        case BlockType::mix:
        case BlockType::numTypes:   m0 = 1.0f; m1 = 0.0f;                     m2 = 0.0f;  break;
    }

    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}
}

//==============================================================================
namespace
{
using custom_blocks::BlockType;

constexpr double crossfadeSeconds = 0.006;
constexpr float clipCeiling = 0.25f;   // about -12 dBFS, near a hot guitar signal

/** Parameter ids must outlive every pedal, so they are made once and kept. */
const char* slotId (int slot, int field)
{
    static const auto table = []
    {
        static const char* const fields[] = { "type", "freq", "q", "gain", "drive", "shape", "slope", "invert",
                                              "rate", "depth", "target", "wave", "sens", "attack", "release",
                                              "time", "feedback", "mix", "stages", "bits", "downsample" };
        constexpr int numFields = (int) std::size (fields);
        std::array<std::array<std::string, numFields>, CustomPedal::maxBlocks> ids;

        for (int s = 0; s < CustomPedal::maxBlocks; ++s)
            for (int f = 0; f < numFields; ++f)
                ids[(size_t) s][(size_t) f] = "custom.b" + std::to_string (s + 1) + "." + fields[f];

        return ids;
    }();

    return table[(size_t) slot][(size_t) field].c_str();
}

bool isFilter (BlockType type) noexcept
{
    return type != BlockType::empty && type != BlockType::gain && type != BlockType::clipper
        && type < BlockType::lfo;
}

float clip (float x, int shape) noexcept
{
    switch (shape)
    {
        case CustomPedal::hard:        return juce::jlimit (-1.0f, 1.0f, x);
        case CustomPedal::diode:       return x / std::pow (1.0f + std::pow (std::abs (x), 2.5f), 0.4f);   // sharper knee than soft
        case CustomPedal::asymmetric:  return x > 0.0f ? std::tanh (x) : 0.6f * std::tanh (x / 0.6f);
        default:                       return std::tanh (x);
    }
}
}

//==============================================================================
CustomPedal::Slot::Slot (int index)
    : type (slotId (index, 0), "Type",
            { "Empty", "Low-pass", "High-pass", "Band-pass", "Notch", "Peak", "Low shelf", "High shelf", "Gain", "Clipper",
              "LFO", "Envelope", "Delay", "All-pass", "Reducer", "Split", "Path B", "Mix" }),
      frequency (slotId (index, 1), "Frequency", 20.0f, 20000.0f, 1000.0f, 1.0f, " Hz", true),
      resonance (slotId (index, 2), "Resonance", 0.3f, 10.0f, 0.707f, 0.01f, "", true),
      gainDb (slotId (index, 3), "Gain", -24.0f, 24.0f, 0.0f, 0.1f, " dB"),
      driveDb (slotId (index, 4), "Drive", 0.0f, 40.0f, 12.0f, 0.1f, " dB"),
      shape (slotId (index, 5), "Shape", { "Soft", "Hard", "Diode", "Asymmetric" }),
      slope (slotId (index, 6), "Slope", { "12 dB/oct", "24 dB/oct" }),
      invert (slotId (index, 7), "Invert", 0.0f, 1.0f, 0.0f, 1.0f),
      rate (slotId (index, 8), "Rate", 0.05f, 20.0f, 2.0f, 0.01f, " Hz", true),
      depth (slotId (index, 9), "Depth", -100.0f, 100.0f, 50.0f, 1.0f, " %"),
      target (slotId (index, 10), "Target", { "None", "Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5", "Slot 6", "Slot 7", "Slot 8", "Slot 9", "Slot 10" }),
      wave (slotId (index, 11), "Wave", { "Sine", "Triangle", "Square", "Ramp up", "Ramp down" }),
      sensitivity (slotId (index, 12), "Sensitivity", 0.0f, 40.0f, 12.0f, 0.1f, " dB"),
      attack (slotId (index, 13), "Attack", 1.0f, 200.0f, 10.0f, 0.1f, " ms", true),
      release (slotId (index, 14), "Release", 10.0f, 1000.0f, 150.0f, 1.0f, " ms", true),
      time (slotId (index, 15), "Time", 1.0f, maxDelayMs, 300.0f, 0.1f, " ms", true),
      feedback (slotId (index, 16), "Feedback", 0.0f, 95.0f, 30.0f, 1.0f, " %"),
      mix (slotId (index, 17), "Mix", 0.0f, 100.0f, 50.0f, 1.0f, " %"),
      stages (slotId (index, 18), "Stages", { "2", "4", "6", "8" }, 1),
      bits (slotId (index, 19), "Bits", 2.0f, 16.0f, 8.0f, 1.0f),
      downsample (slotId (index, 20), "Rate divide", 1.0f, 32.0f, 4.0f, 1.0f, "x")
{
}

CustomPedal::CustomPedal()
    : MonoEffect ("Custom Pedal", "custom.enabled")
{
    for (int i = 0; i < maxBlocks; ++i)
        slots[(size_t) i] = std::make_unique<Slot> (i);

    activeSlots.reserve (maxBlocks);
    applied = readStructure();
}

std::vector<Parameter*> CustomPedal::getControls()
{
    std::vector<Parameter*> controls { &level, &order, &order2 };

    for (auto& slot : slots)
        for (auto* parameter : slot->getParameters())
            controls.push_back (parameter);

    return controls;
}

void CustomPedal::prepare (double sampleRate, int maxBlockSize)
{
    rate = sampleRate;
    bufferSize = juce::jmax (1, maxBlockSize);
    dry.assign ((size_t) bufferSize, 0.0f);
    modulation.assign ((size_t) (bufferSize * maxBlocks), 0.0f);
    splitInput.assign ((size_t) bufferSize, 0.0f);
    pathA.assign ((size_t) bufferSize, 0.0f);
    dcCoefficient = (float) std::exp (-juce::MathConstants<double>::twoPi * 10.0 / sampleRate);

    for (auto& runner : runners)
    {
        runner.frequency.reset (sampleRate, 0.03);
        runner.resonance.reset (sampleRate, 0.03);
        runner.gainDb.reset (sampleRate, 0.03);
        runner.driveDb.reset (sampleRate, 0.03);
        runner.depth.reset (sampleRate, 0.03);
        runner.mix.reset (sampleRate, 0.03);
        runner.feedback.reset (sampleRate, 0.03);
        runner.time.reset (sampleRate, 0.08);
        runner.delayLine.assign ((size_t) (maxDelayMs * 0.001 * sampleRate) + 4, 0.0f);   // room for modulation
    }

    wetMix.reset (sampleRate, crossfadeSeconds);
    outputGain.reset (sampleRate, 0.03);
    reset();
}

void CustomPedal::Runner::reset() noexcept
{
    for (auto& stage : stages)
        stage.reset();

    dcX1 = dcY1 = 0.0f;
    lfoPhase = 0.0;
    envelope = 0.0f;
    std::fill (delayLine.begin(), delayLine.end(), 0.0f);
    writeIndex = 0;
    allPassState.fill (0.0f);
    heldSample = 0.0f;
    holdCounter = 0;
}

void CustomPedal::reset()
{
    applyStructure (readStructure());

    for (int i = 0; i < maxBlocks; ++i)
    {
        runners[(size_t) i].reset();
        updateTargets (i, true);
        updateFilter (i);
    }

    wetMix.setCurrentAndTargetValue (1.0f);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (level.get()));
}

CustomPedal::Structure CustomPedal::readStructure() const noexcept
{
    Structure s;
    s.order = getOrder();

    for (int i = 0; i < maxBlocks; ++i)
    {
        s.types[(size_t) i] = slots[(size_t) i]->getType();
        s.slopes[(size_t) i] = slots[(size_t) i]->slope.getIndex();
    }

    return s;
}

void CustomPedal::applyStructure (const Structure& wanted) noexcept
{
    for (int i = 0; i < maxBlocks; ++i)
    {
        // A slot that changed shape starts from silence and from its knobs' current settings.
        if (wanted.types[(size_t) i] != applied.types[(size_t) i] || wanted.slopes[(size_t) i] != applied.slopes[(size_t) i])
        {
            runners[(size_t) i].reset();
            updateTargets (i, true);
        }
    }

    applied = wanted;
    activeSlots.clear();

    for (auto slot : applied.order)
    {
        const auto type = applied.types[(size_t) slot];

        if (type != BlockType::empty && type != BlockType::numTypes)
            activeSlots.push_back (slot);
    }

    for (int i = 0; i < maxBlocks; ++i)
    {
        const auto type = applied.types[(size_t) i];
        runners[(size_t) i].numStages = (type == BlockType::lowPass || type == BlockType::highPass)
                                         && applied.slopes[(size_t) i] == 1 ? 2 : 1;
        updateFilter (i);
    }
}

void CustomPedal::updateTargets (int index, bool snap) noexcept
{
    auto& slot = *slots[(size_t) index];
    auto& runner = runners[(size_t) index];

    if (snap)
    {
        runner.frequency.setCurrentAndTargetValue (slot.frequency.get());
        runner.resonance.setCurrentAndTargetValue (slot.resonance.get());
        runner.gainDb.setCurrentAndTargetValue (slot.gainDb.get());
        runner.driveDb.setCurrentAndTargetValue (slot.driveDb.get());
        runner.depth.setCurrentAndTargetValue (slot.depth.get() * 0.01f);
        runner.mix.setCurrentAndTargetValue (slot.mix.get() * 0.01f);
        runner.feedback.setCurrentAndTargetValue (slot.feedback.get() * 0.01f);
        runner.time.setCurrentAndTargetValue (slot.time.get());
    }
    else
    {
        runner.frequency.setTargetValue (slot.frequency.get());
        runner.resonance.setTargetValue (slot.resonance.get());
        runner.gainDb.setTargetValue (slot.gainDb.get());
        runner.driveDb.setTargetValue (slot.driveDb.get());
        runner.depth.setTargetValue (slot.depth.get() * 0.01f);
        runner.mix.setTargetValue (slot.mix.get() * 0.01f);
        runner.feedback.setTargetValue (slot.feedback.get() * 0.01f);
        runner.time.setTargetValue (slot.time.get());
    }
}

void CustomPedal::configureFilter (BlockType type, float hz, float q, float gainDb, bool steep, double sampleRate,
                                   std::array<custom_blocks::SvfStage, 2>& stages, int& numStages) noexcept
{
    numStages = steep && (type == BlockType::lowPass || type == BlockType::highPass) ? 2 : 1;

    if (type == BlockType::lowShelf || type == BlockType::highShelf)
    {
        stages[0].setCoefficients (type, hz, juce::MathConstants<float>::sqrt2 * 0.5f, gainDb, sampleRate);
    }
    else if (numStages == 2)
    {
        // A 4th-order Butterworth split into two stages; the knob raises the second stage's peak.
        stages[0].setCoefficients (type, hz, 0.5412f, 0.0f, sampleRate);
        stages[1].setCoefficients (type, hz, q * 1.8478f, 0.0f, sampleRate);
    }
    else
    {
        stages[0].setCoefficients (type, hz, q, gainDb, sampleRate);
    }
}

void CustomPedal::updateFilter (int index, float frequencyRatio) noexcept
{
    auto& runner = runners[(size_t) index];
    const auto type = applied.types[(size_t) index];

    if (! isFilter (type))
        return;

    configureFilter (type, runner.frequency.getCurrentValue() * frequencyRatio, runner.resonance.getCurrentValue(),
                     runner.gainDb.getCurrentValue(), applied.slopes[(size_t) index] == 1, rate,
                     runner.stages, runner.numStages);
}

std::vector<float> CustomPedal::getResponseDb (const std::vector<float>& frequencies, bool& hasFilters)
{
    // An impulse through every filter and gain block in order, then measured.
    constexpr int fftOrder = 14, size = 1 << fftOrder;
    constexpr double sampleRate = 48000.0;
    std::vector<float> buffer ((size_t) size * 2, 0.0f);
    buffer[0] = 1.0f;
    hasFilters = false;

    for (auto slot : getBlocksInOrder())
    {
        auto& s = *slots[(size_t) slot];
        const auto type = s.getType();

        if (type == BlockType::gain)
        {
            const auto g = juce::Decibels::decibelsToGain (s.gainDb.get()) * (s.invert.isOn() ? -1.0f : 1.0f);

            for (int i = 0; i < size; ++i)
                buffer[(size_t) i] *= g;

            hasFilters = true;
        }
        else if (isFilter (type))
        {
            std::array<custom_blocks::SvfStage, 2> stages;
            int numStages = 1;
            configureFilter (type, s.frequency.get(), s.resonance.get(), s.gainDb.get(), s.slope.getIndex() == 1,
                             sampleRate, stages, numStages);

            for (int n = 0; n < numStages; ++n)
                for (int i = 0; i < size; ++i)
                    buffer[(size_t) i] = stages[(size_t) n].process (buffer[(size_t) i]);

            hasFilters = true;
        }
    }

    juce::dsp::FFT fft (fftOrder);
    fft.performFrequencyOnlyForwardTransform (buffer.data());

    std::vector<float> result;
    result.reserve (frequencies.size());

    for (auto hz : frequencies)
    {
        const auto bin = juce::jlimit (0, size / 2, juce::roundToInt (hz / sampleRate * size));
        result.push_back (juce::Decibels::gainToDecibels (buffer[(size_t) bin], -60.0f));
    }

    return result;
}

void CustomPedal::runSlot (int index, float* samples, int numSamples, const float* mod) noexcept
{
    auto& slot = *slots[(size_t) index];
    auto& runner = runners[(size_t) index];
    const auto type = applied.types[(size_t) index];

    updateTargets (index, false);

    if (isFilter (type))
    {
        const bool moving = runner.frequency.isSmoothing() || runner.resonance.isSmoothing() || runner.gainDb.isSmoothing()
                            || mod != nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            if (moving)
            {
                runner.frequency.getNextValue();
                runner.resonance.getNextValue();
                runner.gainDb.getNextValue();

                // Recalculating every 8 samples is smooth to the ear and a fraction of the cost.
                // A modulator sweeps the frequency up to 3 octaves either way.
                if ((i & 7) == 0)
                    updateFilter (index, mod != nullptr ? std::exp2 (3.0f * mod[i]) : 1.0f);
            }

            auto x = runner.stages[0].process (samples[i]);

            if (runner.numStages == 2)
                x = runner.stages[1].process (x);

            samples[i] = x;
        }

        if (moving)
            updateFilter (index, mod != nullptr ? std::exp2 (3.0f * mod[numSamples - 1]) : 1.0f);

        return;
    }

    if (type == BlockType::gain)
    {
        const auto sign = slot.invert.isOn() ? -1.0f : 1.0f;

        // A modulator scales the level between silence and double (full depth).
        for (int i = 0; i < numSamples; ++i)
            samples[i] *= sign * juce::Decibels::decibelsToGain (runner.gainDb.getNextValue())
                        * (mod != nullptr ? juce::jmax (0.0f, 1.0f + mod[i]) : 1.0f);

        return;
    }

    if (type == BlockType::delay)
    {
        // A modulator moves the delay time up to an octave either way, for chorus and vibrato.
        auto& line = runner.delayLine;
        const auto size = (int) line.size();

        for (int i = 0; i < numSamples; ++i)
        {
            const auto ms = runner.time.getNextValue() * (mod != nullptr ? std::exp2 (mod[i]) : 1.0f);
            const auto delaySamples = juce::jlimit (1.0f, (float) (size - 2), ms * 0.001f * (float) rate);

            // Whole and fractional parts kept apart, so rounding can never read past the end.
            const auto whole = (int) delaySamples;
            const auto frac = 1.0f - (delaySamples - (float) whole);
            auto i0 = runner.writeIndex - whole - 1;
            if (i0 < 0)
                i0 += size;
            const auto delayed = line[(size_t) i0] + frac * (line[(size_t) ((i0 + 1) % size)] - line[(size_t) i0]);

            line[(size_t) runner.writeIndex] = samples[i] + runner.feedback.getNextValue() * delayed;

            if (++runner.writeIndex >= size)
                runner.writeIndex = 0;

            samples[i] += runner.mix.getNextValue() * delayed;
        }

        return;
    }

    if (type == BlockType::allPass)
    {
        // A chain of first-order all-passes, mixed with the input to cut notches.
        const auto numStages = 2 * (slot.stages.getIndex() + 1);

        for (int i = 0; i < numSamples; ++i)
        {
            runner.frequency.getNextValue();
            const auto hz = runner.frequency.getCurrentValue() * (mod != nullptr ? std::exp2 (3.0f * mod[i]) : 1.0f);
            const auto w = std::tan (juce::MathConstants<float>::pi * juce::jlimit (20.0f, (float) (rate * 0.45), hz) / (float) rate);
            const auto a = (w - 1.0f) / (w + 1.0f);

            auto x = samples[i];

            for (int s = 0; s < numStages; ++s)
            {
                const auto y = a * x + runner.allPassState[(size_t) s];
                runner.allPassState[(size_t) s] = x - a * y;
                x = y;
            }

            samples[i] += 0.5f * runner.mix.getNextValue() * (x - samples[i]);
        }

        return;
    }

    if (type == BlockType::reducer)
    {
        const auto levels = std::exp2 (slot.bits.get() - 1.0f);
        const auto hold = juce::jmax (1, slot.downsample.getIndex());

        for (int i = 0; i < numSamples; ++i)
        {
            if (runner.holdCounter <= 0)
            {
                runner.heldSample = std::round (samples[i] * levels) / levels;
                runner.holdCounter = hold;
            }

            --runner.holdCounter;
            samples[i] += runner.mix.getNextValue() * (runner.heldSample - samples[i]);
        }

        return;
    }

    if (type == BlockType::clipper)
    {
        const auto shape = slot.shape.getIndex();
        const bool removeOffset = shape == asymmetric;

        for (int i = 0; i < numSamples; ++i)
        {
            // A modulator pushes the drive up to 24 dB either way.
            const auto drive = juce::Decibels::decibelsToGain (runner.driveDb.getNextValue()
                                                               + (mod != nullptr ? 24.0f * mod[i] : 0.0f));
            const auto output = juce::Decibels::decibelsToGain (runner.gainDb.getNextValue());
            auto y = clipCeiling * clip (samples[i] * drive / clipCeiling, shape);

            if (removeOffset)
            {
                // Uneven clipping leaves a slow offset behind; take it out.
                const auto blocked = y - runner.dcX1 + dcCoefficient * runner.dcY1;
                runner.dcX1 = y;
                runner.dcY1 = blocked;
                y = blocked;
            }

            samples[i] = y * output;
        }
    }
}

void CustomPedal::runModulators (const float* input, int numSamples) noexcept
{
    isModulated.fill (false);

    for (auto slot : activeSlots)
    {
        const auto type = applied.types[(size_t) slot];

        if (! custom_blocks::isModulator (type))
            continue;

        auto& s = *slots[(size_t) slot];
        auto& runner = runners[(size_t) slot];
        const auto targetSlot = s.getTargetSlot();
        updateTargets (slot, false);

        // A modulator with nothing valid to move still runs, so its phase keeps going.
        const bool hasTarget = juce::isPositiveAndBelow (targetSlot, maxBlocks) && targetSlot != slot
                               && getModulatedKnobName (applied.types[(size_t) targetSlot])[0] != 0;

        float* out = nullptr;

        if (hasTarget)
        {
            out = modulation.data() + targetSlot * bufferSize;

            if (! isModulated[(size_t) targetSlot])
            {
                std::fill (out, out + numSamples, 0.0f);
                isModulated[(size_t) targetSlot] = true;
            }
        }

        if (type == BlockType::lfo)
        {
            const auto step = (double) s.rate.get() / rate;
            const auto wave = s.wave.getIndex();

            for (int i = 0; i < numSamples; ++i)
            {
                runner.lfoPhase += step;
                runner.lfoPhase -= std::floor (runner.lfoPhase);
                const auto p = runner.lfoPhase;
                float value = 0.0f;

                switch (wave)
                {
                    case triangle:  value = (float) (p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p); break;
                    case square:    value = std::tanh (6.0f * std::sin (juce::MathConstants<float>::twoPi * (float) p)) / std::tanh (6.0f); break;
                    case rampUp:    value = (float) (2.0 * p - 1.0); break;
                    case rampDown:  value = (float) (1.0 - 2.0 * p); break;
                    default:        value = std::sin (juce::MathConstants<float>::twoPi * (float) p); break;
                }

                const auto depth = runner.depth.getNextValue();

                if (out != nullptr)
                    out[i] += depth * value;
            }
        }
        else
        {
            // Follows how hard you play: fast up (attack), slower down (release).
            const auto gain = juce::Decibels::decibelsToGain (s.sensitivity.get()) / clipCeiling;
            const auto up = (float) (1.0 - std::exp (-1.0 / (s.attack.get() * 0.001 * rate)));
            const auto down = (float) (1.0 - std::exp (-1.0 / (s.release.get() * 0.001 * rate)));

            for (int i = 0; i < numSamples; ++i)
            {
                const auto x = std::abs (input[i]) * gain;
                runner.envelope += (x > runner.envelope ? up : down) * (x - runner.envelope);
                const auto depth = runner.depth.getNextValue();

                if (out != nullptr)
                    out[i] += depth * juce::jmin (1.0f, runner.envelope);
            }
        }
    }

    // Several modulators on one knob add up, within the full range.
    for (int slot = 0; slot < maxBlocks; ++slot)
        if (isModulated[(size_t) slot])
            for (auto* p = modulation.data() + slot * bufferSize, * end = p + numSamples; p != end; ++p)
                *p = juce::jlimit (-1.0f, 1.0f, *p);
}

const char* CustomPedal::getModulatedKnobName (BlockType type) noexcept
{
    switch (type)
    {
        case BlockType::lowPass:
        case BlockType::highPass:   return "cutoff";
        case BlockType::bandPass:
        case BlockType::notch:      return "centre";
        case BlockType::peak:
        case BlockType::lowShelf:
        case BlockType::highShelf:  return "frequency";
        case BlockType::gain:       return "level";
        case BlockType::clipper:    return "drive";
        case BlockType::delay:      return "time";
        case BlockType::allPass:    return "frequency";
        case BlockType::empty:
        case BlockType::lfo:
        case BlockType::envelope:
        case BlockType::reducer:
        case BlockType::split:
        case BlockType::pathB:
        case BlockType::mix:
        case BlockType::numTypes:   break;
    }

    return "";
}

bool CustomPedal::setTarget (int modulatorSlot, int targetSlot)
{
    if (! juce::isPositiveAndBelow (modulatorSlot, maxBlocks))
        return false;

    if (targetSlot < 0)
    {
        slots[(size_t) modulatorSlot]->target.set (0.0f);
        return true;
    }

    if (! juce::isPositiveAndBelow (targetSlot, maxBlocks) || targetSlot == modulatorSlot
        || getModulatedKnobName (slots[(size_t) targetSlot]->getType())[0] == 0)
        return false;

    slots[(size_t) modulatorSlot]->target.set ((float) (targetSlot + 1));
    return true;
}

void CustomPedal::process (float* samples, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    jassert (numSamples <= (int) dry.size());
    numSamples = juce::jmin (numSamples, (int) dry.size());

    // A change to which blocks are in use, or their order, blends to the dry
    // signal, switches while only the dry signal is heard, then blends back.
    if (const auto wanted = readStructure(); ! (wanted == applied))
    {
        if (wetMix.getTargetValue() > 0.0f)
            wetMix.setTargetValue (0.0f);
        else if (! wetMix.isSmoothing())
        {
            applyStructure (wanted);
            wetMix.setTargetValue (1.0f);
        }
    }

    const bool blending = wetMix.isSmoothing() || wetMix.getTargetValue() < 1.0f;

    if (blending)
        std::copy (samples, samples + numSamples, dry.data());

    runModulators (samples, numSamples);

    // Split, Path B and Mix route the signal through two parallel paths.
    // Without a Path B, path B is the signal as it was at the Split.
    bool inSplit = false, inPathB = false;

    for (auto slot : activeSlots)
    {
        const auto type = applied.types[(size_t) slot];

        if (type == BlockType::split)
        {
            if (! inSplit)
            {
                std::copy (samples, samples + numSamples, splitInput.data());
                inSplit = true;
                inPathB = false;
            }

            continue;
        }

        if (type == BlockType::pathB)
        {
            if (inSplit && ! inPathB)
            {
                std::copy (samples, samples + numSamples, pathA.data());
                std::copy (splitInput.data(), splitInput.data() + numSamples, samples);
                inPathB = true;
            }

            continue;
        }

        if (type == BlockType::mix)
        {
            auto& blend = runners[(size_t) slot].mix;
            blend.setTargetValue (slots[(size_t) slot]->mix.get() * 0.01f);

            if (inSplit)
            {
                // Blend 0 is path A, 1 is path B.
                const auto* a = inPathB ? pathA.data() : samples;
                const auto* b = inPathB ? samples : splitInput.data();

                for (int i = 0; i < numSamples; ++i)
                {
                    const auto amount = blend.getNextValue();
                    samples[i] = a[i] + amount * (b[i] - a[i]);
                }
            }
            else
            {
                blend.skip (numSamples);
            }

            inSplit = inPathB = false;
            continue;
        }

        runSlot (slot, samples, numSamples,
                 isModulated[(size_t) slot] ? modulation.data() + slot * bufferSize : nullptr);
    }

    // A Split with a Path B but no Mix joins the paths evenly at the end.
    if (inSplit && inPathB)
        for (int i = 0; i < numSamples; ++i)
            samples[i] = 0.5f * (samples[i] + pathA[(size_t) i]);

    if (blending)
        for (int i = 0; i < numSamples; ++i)
            samples[i] = dry[(size_t) i] + wetMix.getNextValue() * (samples[i] - dry[(size_t) i]);

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (level.get()));

    for (int i = 0; i < numSamples; ++i)
        samples[i] *= outputGain.getNextValue();
}

//==============================================================================
std::vector<int> CustomPedal::getBlocksInOrder() const
{
    std::vector<int> result;

    for (auto slot : getOrder())
    {
        const auto type = slots[(size_t) slot]->getType();

        if (type != BlockType::empty && type != BlockType::numTypes)
            result.push_back (slot);
    }

    return result;
}

void CustomPedal::setOrder (const Order& newOrder)
{
    order2.set ((float) encodeOrder (newOrder));
    order.set (0.0f);
}

namespace
{
/** Lehmer code for a permutation of n slots: digit i picks one of the slots not yet placed. */
template <size_t n>
std::array<int, n> decodeLehmer (int code) noexcept
{
    std::array<int, n> remaining {}, result {};

    for (size_t i = 0; i < n; ++i)
        remaining[i] = (int) i;

    int factorial = 1;

    for (int k = 2; k < (int) n; ++k)
        factorial *= k;

    code = juce::jlimit (0, factorial * (int) n - 1, code);
    int count = (int) n;

    for (size_t i = 0; i < n; ++i)
    {
        const auto digit = code / factorial;
        code %= factorial;
        result[i] = remaining[(size_t) digit];

        for (int j = digit; j < count - 1; ++j)
            remaining[(size_t) j] = remaining[(size_t) j + 1];

        --count;

        if (i + 1 < n)
            factorial /= (int) (n - 1 - i);
    }

    return result;
}
}

CustomPedal::Order CustomPedal::getOrder() const noexcept
{
    if (order2.getIndex() != 0 || order.getIndex() == 0)
        return decodeOrder (order2.getIndex());

    // Saved by a first test build with eight slots: the two new slots go last.
    const auto old = decodeLehmer<8> (order.getIndex());
    Order result {};

    for (size_t i = 0; i < old.size(); ++i)
        result[i] = old[i];

    for (size_t i = old.size(); i < (size_t) maxBlocks; ++i)
        result[i] = (int) i;

    return result;
}

int CustomPedal::addBlock (BlockType type)
{
    if (type == BlockType::empty || type == BlockType::numTypes)
        return -1;

    auto current = getOrder();
    const auto inUse = getBlocksInOrder();

    // The first empty slot, in signal order.
    int chosen = -1;

    for (auto slot : current)
    {
        if (std::find (inUse.begin(), inUse.end(), slot) == inUse.end())
        {
            chosen = slot;
            break;
        }
    }

    if (chosen < 0)
        return -1;

    // Fresh settings first, while the slot is still empty and silent.
    auto& slot = *slots[(size_t) chosen];

    for (auto* parameter : slot.getParameters())
        parameter->set (parameter->defaultValue);

    // Sensible starting points, so a new block does something you can hear.
    switch (type)
    {
        case BlockType::lowPass:    slot.frequency.set (3000.0f); break;
        case BlockType::highPass:   slot.frequency.set (150.0f);  break;
        case BlockType::bandPass:   slot.frequency.set (800.0f);  slot.resonance.set (2.0f); break;
        case BlockType::notch:      slot.frequency.set (1000.0f); slot.resonance.set (2.0f); break;
        case BlockType::peak:       slot.frequency.set (800.0f);  slot.gainDb.set (6.0f); slot.resonance.set (1.0f); break;
        case BlockType::lowShelf:   slot.frequency.set (200.0f);  slot.gainDb.set (4.0f); break;
        case BlockType::highShelf:  slot.frequency.set (3000.0f); slot.gainDb.set (4.0f); break;
        case BlockType::gain:       break;
        case BlockType::clipper:    slot.gainDb.set (-4.0f); break;
        case BlockType::allPass:    slot.frequency.set (700.0f); slot.mix.set (100.0f); break;
        case BlockType::reducer:    slot.mix.set (100.0f); break;
        case BlockType::delay:
        case BlockType::split:
        case BlockType::pathB:
        case BlockType::mix:        break;
        case BlockType::lfo:
        case BlockType::envelope:
            // Point it at the nearest block before it that it can move.
            for (auto it = inUse.rbegin(); it != inUse.rend(); ++it)
                if (setTarget (chosen, *it))
                    break;
            break;
        case BlockType::empty:
        case BlockType::numTypes:   break;
    }

    // Put it right after the blocks already in use.
    Order next {};
    size_t n = 0;

    for (auto s : inUse)
        next[n++] = s;

    next[n++] = chosen;

    for (auto s : current)
        if (s != chosen && std::find (inUse.begin(), inUse.end(), s) == inUse.end())
            next[n++] = s;

    setOrder (next);
    slot.type.set ((float) type);
    return chosen;
}

void CustomPedal::removeBlock (int slotIndex)
{
    if (! juce::isPositiveAndBelow (slotIndex, maxBlocks))
        return;

    slots[(size_t) slotIndex]->type.set ((float) BlockType::empty);

    // Modulators that moved it now move nothing.
    for (auto& slot : slots)
        if (slot->getTargetSlot() == slotIndex)
            slot->target.set (0.0f);

    // Empty slots wait at the end of the order.
    const auto inUse = getBlocksInOrder();
    const auto current = getOrder();
    Order next {};
    size_t n = 0;

    for (auto s : inUse)
        next[n++] = s;

    for (auto s : current)
        if (std::find (inUse.begin(), inUse.end(), s) == inUse.end())
            next[n++] = s;

    setOrder (next);
}

void CustomPedal::moveBlock (int from, int to)
{
    auto inUse = getBlocksInOrder();

    if (! juce::isPositiveAndBelow (from, (int) inUse.size()))
        return;

    const auto moving = inUse[(size_t) from];
    inUse.erase (inUse.begin() + from);
    to = juce::jlimit (0, (int) inUse.size(), to);
    inUse.insert (inUse.begin() + to, moving);

    const auto current = getOrder();
    Order next {};
    size_t n = 0;

    for (auto s : inUse)
        next[n++] = s;

    for (auto s : current)
        if (std::find (inUse.begin(), inUse.end(), s) == inUse.end())
            next[n++] = s;

    setOrder (next);
}

std::vector<CustomPedal::Control> CustomPedal::getControlsFor (Slot& slot, BlockType type)
{
    switch (type)
    {
        case BlockType::lowPass:
        case BlockType::highPass:   return { { &slot.frequency, "Cutoff" }, { &slot.resonance, "Resonance" }, { &slot.slope, "Slope" } };
        case BlockType::bandPass:
        case BlockType::notch:      return { { &slot.frequency, "Centre" }, { &slot.resonance, "Width" } };
        case BlockType::peak:       return { { &slot.frequency, "Frequency" }, { &slot.gainDb, "Boost/Cut" }, { &slot.resonance, "Width" } };
        case BlockType::lowShelf:
        case BlockType::highShelf:  return { { &slot.frequency, "Frequency" }, { &slot.gainDb, "Boost/Cut" } };
        case BlockType::gain:       return { { &slot.gainDb, "Level" }, { &slot.invert, "Invert" } };
        case BlockType::clipper:    return { { &slot.driveDb, "Drive" }, { &slot.gainDb, "Output" }, { &slot.shape, "Shape" } };
        case BlockType::lfo:        return { { &slot.rate, "Rate" }, { &slot.depth, "Depth" }, { &slot.wave, "Wave" }, { &slot.target, "Moves" } };
        case BlockType::envelope:   return { { &slot.sensitivity, "Sensitivity" }, { &slot.attack, "Attack" }, { &slot.release, "Release" },
                                             { &slot.depth, "Depth" }, { &slot.target, "Moves" } };
        case BlockType::delay:      return { { &slot.time, "Time" }, { &slot.feedback, "Feedback" }, { &slot.mix, "Echo level" } };
        case BlockType::allPass:    return { { &slot.frequency, "Frequency" }, { &slot.mix, "Depth" }, { &slot.stages, "Stages" } };
        case BlockType::reducer:    return { { &slot.bits, "Bits" }, { &slot.downsample, "Rate divide" }, { &slot.mix, "Mix" } };
        case BlockType::mix:        return { { &slot.mix, "Blend" } };
        case BlockType::split:
        case BlockType::pathB:
        case BlockType::empty:
        case BlockType::numTypes:   break;
    }

    return {};
}

//==============================================================================
CustomPedal::Order CustomPedal::decodeOrder (int code) noexcept
{
    return decodeLehmer<(size_t) maxBlocks> (code);
}

int CustomPedal::encodeOrder (const Order& orderToEncode) noexcept
{
    int code = 0;
    int factorial = 1;

    for (int k = 2; k < maxBlocks; ++k)
        factorial *= k;

    for (int i = 0; i < maxBlocks; ++i)
    {
        int smaller = 0;

        for (int j = i + 1; j < maxBlocks; ++j)
            if (orderToEncode[(size_t) j] < orderToEncode[(size_t) i])
                ++smaller;

        code += smaller * factorial;

        if (i < maxBlocks - 1)
            factorial /= (maxBlocks - 1 - i);
    }

    return code;
}
