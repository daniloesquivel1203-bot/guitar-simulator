#include "CustomPedal.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <string>


//==============================================================================
namespace
{
using custom_blocks::BlockType;

constexpr double crossfadeSeconds = 0.006;

/** Parameter ids must outlive every pedal, so they are made once and kept. */
const char* slotId (int slot, int field)
{
    static const auto table = []
    {
        std::array<std::array<std::string, custom_blocks::BlockParams::numFields>, CustomPedal::maxBlocks> ids;

        for (int s = 0; s < CustomPedal::maxBlocks; ++s)
            for (int f = 0; f < custom_blocks::BlockParams::numFields; ++f)
                ids[(size_t) s][(size_t) f] = "custom.b" + std::to_string (s + 1) + "." + custom_blocks::BlockParams::fieldNames[f];

        return ids;
    }();

    return table[(size_t) slot][(size_t) field].c_str();
}

const std::vector<const char*>& targetNames()
{
    static const std::vector<const char*> names { "None", "Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5",
                                                  "Slot 6", "Slot 7", "Slot 8", "Slot 9", "Slot 10" };
    return names;
}
}

//==============================================================================

CustomPedal::CustomPedal()
    : MonoEffect ("Custom Pedal", "custom.enabled")
{
    for (int i = 0; i < maxBlocks; ++i)
        slots[(size_t) i] = std::make_unique<Slot> ([i] (int field) { return slotId (i, field); }, targetNames());

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
    for (auto& runner : runners)
        runner.prepare (sampleRate);

    wetMix.reset (sampleRate, crossfadeSeconds);
    outputGain.reset (sampleRate, 0.03);
    reset();
}


void CustomPedal::reset()
{
    applyStructure (readStructure());

    for (int i = 0; i < maxBlocks; ++i)
    {
        runners[(size_t) i].reset();
        runners[(size_t) i].sync (*slots[(size_t) i], true);
        runners[(size_t) i].setShape (applied.types[(size_t) i], applied.slopes[(size_t) i] == 1);
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
            runners[(size_t) i].sync (*slots[(size_t) i], true);
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
        runners[(size_t) i].setShape (applied.types[(size_t) i], applied.slopes[(size_t) i] == 1);
}




std::vector<float> CustomPedal::getResponseDb (const std::vector<float>& frequencies, bool& hasFilters)
{
    // An impulse through the blocks, routed the same way process() routes
    // the sound, then measured. Only the blocks that keep the same tone
    // whatever you play are included: filters, gain, all-pass and the paths.
    constexpr int fftOrder = 14, size = 1 << fftOrder;
    constexpr double sampleRate = 48000.0;
    std::vector<float> buffer ((size_t) size * 2, 0.0f), splitCopy ((size_t) size, 0.0f), pathACopy ((size_t) size, 0.0f);
    buffer[0] = 1.0f;
    hasFilters = false;
    bool inSplit = false, inPathB = false;

    for (auto slot : getBlocksInOrder())
    {
        auto& s = *slots[(size_t) slot];
        const auto type = s.getType();

        if (type == BlockType::split)
        {
            if (! inSplit)
            {
                std::copy (buffer.begin(), buffer.begin() + size, splitCopy.begin());
                inSplit = true;
                inPathB = false;
                hasFilters = true;
            }
        }
        else if (type == BlockType::pathB)
        {
            if (inSplit && ! inPathB)
            {
                std::copy (buffer.begin(), buffer.begin() + size, pathACopy.begin());
                std::copy (splitCopy.begin(), splitCopy.end(), buffer.begin());
                inPathB = true;
            }
        }
        else if (type == BlockType::mix)
        {
            if (inSplit)
            {
                const auto blend = s.mix.get() * 0.01f;
                const auto* a = inPathB ? pathACopy.data() : buffer.data();
                const auto* b = inPathB ? buffer.data() : splitCopy.data();

                for (int i = 0; i < size; ++i)
                    buffer[(size_t) i] = a[i] + blend * (b[i] - a[i]);
            }

            inSplit = inPathB = false;
        }
        else if (type == BlockType::gain)
        {
            const auto g = juce::Decibels::decibelsToGain (s.gainDb.get()) * (s.invert.isOn() ? -1.0f : 1.0f);

            for (int i = 0; i < size; ++i)
                buffer[(size_t) i] *= g;

            hasFilters = true;
        }
        else if (type == BlockType::allPass)
        {
            // The same first-order chain as runSlot(), with its knobs held still.
            const auto numStages = 2 * (s.stages.getIndex() + 1);
            const auto w = std::tan (juce::MathConstants<float>::pi * juce::jlimit (20.0f, (float) (sampleRate * 0.45), s.frequency.get())
                                     / (float) sampleRate);
            const auto a = (w - 1.0f) / (w + 1.0f);
            const auto depth = s.mix.get() * 0.01f;
            std::array<float, 8> state {};

            for (int i = 0; i < size; ++i)
            {
                auto x = buffer[(size_t) i];

                for (int n = 0; n < numStages; ++n)
                {
                    const auto y = a * x + state[(size_t) n];
                    state[(size_t) n] = x - a * y;
                    x = y;
                }

                buffer[(size_t) i] += 0.5f * depth * (x - buffer[(size_t) i]);
            }

            hasFilters = true;
        }
        else if (custom_blocks::isFilter (type))
        {
            std::array<custom_blocks::SvfStage, 2> stages;
            int numStages = 1;
            custom_blocks::configureFilter (type, s.frequency.get(), s.resonance.get(), s.gainDb.get(), s.slope.getIndex() == 1,
                             sampleRate, stages, numStages);

            for (int n = 0; n < numStages; ++n)
                for (int i = 0; i < size; ++i)
                    buffer[(size_t) i] = stages[(size_t) n].process (buffer[(size_t) i]);

            hasFilters = true;
        }
    }

    // A Split with a lower path but no Mix joins the paths evenly at the end.
    if (inSplit && inPathB)
        for (int i = 0; i < size; ++i)
            buffer[(size_t) i] = 0.5f * (buffer[(size_t) i] + pathACopy[(size_t) i]);

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

        runner.modulate (s, input, out, numSamples);
    }

    // Several modulators on one knob add up, within the full range.
    for (int slot = 0; slot < maxBlocks; ++slot)
        if (isModulated[(size_t) slot])
            for (auto* p = modulation.data() + slot * bufferSize, * end = p + numSamples; p != end; ++p)
                *p = juce::jlimit (-1.0f, 1.0f, *p);
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
            auto& blend = runners[(size_t) slot].getBlend();
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

        runners[(size_t) slot].process (*slots[(size_t) slot], samples, numSamples,
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
    slots[(size_t) chosen]->setFresh (type);

    // A new modulator moves the nearest block before it that it can move.
    if (custom_blocks::isModulator (type))
        for (auto it = inUse.rbegin(); it != inUse.rend(); ++it)
            if (setTarget (chosen, *it))
                break;

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
    slots[(size_t) chosen]->type.set ((float) type);
    return chosen;
}

int CustomPedal::addSplit()
{
    if ((int) getBlocksInOrder().size() > maxBlocks - 3)
        return -1;

    const auto split = addBlock (BlockType::split);
    addBlock (BlockType::pathB);
    addBlock (BlockType::mix);
    return split;
}

std::vector<int> CustomPedal::getSplitGroup (int slotIndex) const
{
    // Walks the blocks the way process() routes them.
    std::vector<int> group;
    bool inSplit = false, inPathB = false;

    for (auto slot : getBlocksInOrder())
    {
        const auto type = slots[(size_t) slot]->getType();

        if (type == BlockType::split && ! inSplit)
        {
            group = { slot };
            inSplit = true;
            inPathB = false;
        }
        else if (type == BlockType::pathB && inSplit && ! inPathB)
        {
            group.push_back (slot);
            inPathB = true;
        }
        else if (type == BlockType::mix && inSplit)
        {
            group.push_back (slot);

            if (std::find (group.begin(), group.end(), slotIndex) != group.end())
                return group;

            inSplit = inPathB = false;
        }
    }

    if (inSplit && std::find (group.begin(), group.end(), slotIndex) != group.end())
        return group;

    return { slotIndex };
}

void CustomPedal::removeBlock (int slotIndex)
{
    if (! juce::isPositiveAndBelow (slotIndex, maxBlocks))
        return;

    // Removing the Split or the + takes the whole branch's markers with it;
    // the blocks on both paths stay, one after the other.
    for (auto member : custom_blocks::isRouting (slots[(size_t) slotIndex]->getType()) ? getSplitGroup (slotIndex)
                                                                                         : std::vector<int> { slotIndex })
    {
        slots[(size_t) member]->type.set ((float) BlockType::empty);

        // Modulators that moved it now move nothing.
        for (auto& slot : slots)
            if (slot->getTargetSlot() == member)
                slot->target.set (0.0f);
    }

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
