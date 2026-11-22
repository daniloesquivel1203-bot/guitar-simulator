#include "CustomPedal.h"

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
                                              "rate", "depth", "target", "wave", "sens", "attack", "release" };
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
        && ! custom_blocks::isModulator (type) && type != BlockType::numTypes;
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
              "LFO", "Envelope" }),
      frequency (slotId (index, 1), "Frequency", 20.0f, 20000.0f, 1000.0f, 1.0f, " Hz", true),
      resonance (slotId (index, 2), "Resonance", 0.3f, 10.0f, 0.707f, 0.01f, "", true),
      gainDb (slotId (index, 3), "Gain", -24.0f, 24.0f, 0.0f, 0.1f, " dB"),
      driveDb (slotId (index, 4), "Drive", 0.0f, 40.0f, 12.0f, 0.1f, " dB"),
      shape (slotId (index, 5), "Shape", { "Soft", "Hard", "Diode", "Asymmetric" }),
      slope (slotId (index, 6), "Slope", { "12 dB/oct", "24 dB/oct" }),
      invert (slotId (index, 7), "Invert", 0.0f, 1.0f, 0.0f, 1.0f),
      rate (slotId (index, 8), "Rate", 0.05f, 20.0f, 2.0f, 0.01f, " Hz", true),
      depth (slotId (index, 9), "Depth", -100.0f, 100.0f, 50.0f, 1.0f, " %"),
      target (slotId (index, 10), "Target", { "None", "Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5", "Slot 6", "Slot 7", "Slot 8" }),
      wave (slotId (index, 11), "Wave", { "Sine", "Triangle", "Square", "Ramp up", "Ramp down" }),
      sensitivity (slotId (index, 12), "Sensitivity", 0.0f, 40.0f, 12.0f, 0.1f, " dB"),
      attack (slotId (index, 13), "Attack", 1.0f, 200.0f, 10.0f, 0.1f, " ms", true),
      release (slotId (index, 14), "Release", 10.0f, 1000.0f, 150.0f, 1.0f, " ms", true)
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
    std::vector<Parameter*> controls { &level, &order };

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
    dcCoefficient = (float) std::exp (-juce::MathConstants<double>::twoPi * 10.0 / sampleRate);

    for (auto& runner : runners)
    {
        runner.frequency.reset (sampleRate, 0.03);
        runner.resonance.reset (sampleRate, 0.03);
        runner.gainDb.reset (sampleRate, 0.03);
        runner.driveDb.reset (sampleRate, 0.03);
        runner.depth.reset (sampleRate, 0.03);
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
    s.order = decodeOrder (order.getIndex());

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
    }
    else
    {
        runner.frequency.setTargetValue (slot.frequency.get());
        runner.resonance.setTargetValue (slot.resonance.get());
        runner.gainDb.setTargetValue (slot.gainDb.get());
        runner.driveDb.setTargetValue (slot.driveDb.get());
        runner.depth.setTargetValue (slot.depth.get() * 0.01f);
    }
}

void CustomPedal::updateFilter (int index, float frequencyRatio) noexcept
{
    auto& runner = runners[(size_t) index];
    const auto type = applied.types[(size_t) index];

    if (! isFilter (type))
        return;

    const auto hz = runner.frequency.getCurrentValue() * frequencyRatio;
    const auto q = runner.resonance.getCurrentValue();
    const auto gainDb = runner.gainDb.getCurrentValue();

    if (type == BlockType::lowShelf || type == BlockType::highShelf)
    {
        runner.stages[0].setCoefficients (type, hz, juce::MathConstants<float>::sqrt2 * 0.5f, gainDb, rate);
    }
    else if (runner.numStages == 2)
    {
        // A 4th-order Butterworth split into two stages; the knob raises the second stage's peak.
        runner.stages[0].setCoefficients (type, hz, 0.5412f, 0.0f, rate);
        runner.stages[1].setCoefficients (type, hz, q * 1.8478f, 0.0f, rate);
    }
    else
    {
        runner.stages[0].setCoefficients (type, hz, q, gainDb, rate);
    }
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
        case BlockType::empty:
        case BlockType::lfo:
        case BlockType::envelope:
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

    for (auto slot : activeSlots)
        runSlot (slot, samples, numSamples,
                 isModulated[(size_t) slot] ? modulation.data() + slot * bufferSize : nullptr);

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

    for (auto slot : decodeOrder (order.getIndex()))
    {
        const auto type = slots[(size_t) slot]->getType();

        if (type != BlockType::empty && type != BlockType::numTypes)
            result.push_back (slot);
    }

    return result;
}

void CustomPedal::setOrder (const Order& newOrder)
{
    order.set ((float) encodeOrder (newOrder));
}

int CustomPedal::addBlock (BlockType type)
{
    if (type == BlockType::empty || type == BlockType::numTypes)
        return -1;

    auto current = decodeOrder (order.getIndex());
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
    const auto current = decodeOrder (order.getIndex());
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

    const auto current = decodeOrder (order.getIndex());
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
        case BlockType::empty:
        case BlockType::numTypes:   break;
    }

    return {};
}

//==============================================================================
CustomPedal::Order CustomPedal::decodeOrder (int code) noexcept
{
    // A Lehmer code: digit i picks one of the slots not yet placed.
    code = juce::jlimit (0, 40319, code);
    std::array<int, maxBlocks> remaining { 0, 1, 2, 3, 4, 5, 6, 7 };
    int count = maxBlocks;
    Order result {};
    int factorial = 5040;   // 7!

    for (int i = 0; i < maxBlocks; ++i)
    {
        const auto digit = code / factorial;
        code %= factorial;
        result[(size_t) i] = remaining[(size_t) digit];

        for (int j = digit; j < count - 1; ++j)
            remaining[(size_t) j] = remaining[(size_t) j + 1];

        --count;

        if (i < maxBlocks - 1)
            factorial /= (maxBlocks - 1 - i);
    }

    return result;
}

int CustomPedal::encodeOrder (const Order& orderToEncode) noexcept
{
    int code = 0;
    int factorial = 5040;

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
