#include "PatchPedal.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace
{
using custom_blocks::BlockType;

constexpr double crossfadeSeconds = 0.006;

/** Parameter ids and choice names must outlive every pedal, so they are made once and kept. */
const char* nodeId (int node, const char* field)
{
    static const auto table = []
    {
        static const char* const extra[] = { "inA", "inB", "x", "y" };
        std::array<std::vector<std::string>, PatchPedal::maxNodes> ids;

        for (int n = 0; n < PatchPedal::maxNodes; ++n)
        {
            const auto prefix = "patch.n" + std::to_string (n + 1) + ".";

            for (auto* f : custom_blocks::BlockParams::fieldNames)
                ids[(size_t) n].push_back (prefix + f);

            for (auto* f : extra)
                ids[(size_t) n].push_back (prefix + f);
        }

        return ids;
    }();

    static const auto fieldIndex = [] (const char* f)
    {
        static const char* const extra[] = { "inA", "inB", "x", "y" };

        for (int i = 0; i < custom_blocks::BlockParams::numFields; ++i)
            if (std::string (custom_blocks::BlockParams::fieldNames[i]) == f)
                return i;

        for (int i = 0; i < 4; ++i)
            if (std::string (extra[i]) == f)
                return custom_blocks::BlockParams::numFields + i;

        jassertfalse;
        return 0;
    };

    return table[(size_t) node][(size_t) fieldIndex (field)].c_str();
}

const std::vector<const char*>& namesWithNodes (bool withInput)
{
    static const auto make = [] (bool input)
    {
        static std::vector<std::string> storage[2];
        auto& names = storage[input ? 1 : 0];
        names.push_back ("None");

        if (input)
            names.push_back ("Input");

        for (int n = 0; n < PatchPedal::maxNodes; ++n)
            names.push_back ("Node " + std::to_string (n + 1));

        std::vector<const char*> pointers;

        for (auto& name : names)
            pointers.push_back (name.c_str());

        return pointers;
    };

    static const auto targets = make (false);
    static const auto sources = make (true);
    return withInput ? sources : targets;
}

bool isSoundBlock (BlockType type) noexcept
{
    return type != BlockType::empty && type != BlockType::numTypes && ! custom_blocks::isModulator (type);
}
}

//==============================================================================
PatchPedal::Node::Node (int index)
    : block ([index] (int field) { return nodeId (index, custom_blocks::BlockParams::fieldNames[field]); }, namesWithNodes (false)),
      inA (nodeId (index, "inA"), "Input A", namesWithNodes (true)),
      inB (nodeId (index, "inB"), "Input B", namesWithNodes (true)),
      x (nodeId (index, "x"), "X", 0.0f, 1.0f, 0.5f, 0.001f),
      y (nodeId (index, "y"), "Y", 0.0f, 1.0f, 0.5f, 0.001f)
{
}

std::vector<Parameter*> PatchPedal::Node::getParameters()
{
    auto list = block.getParameters();
    list.insert (list.end(), { &inA, &inB, &x, &y });
    return list;
}

PatchPedal::PatchPedal()
    : MonoEffect ("Patch Pedal", "patch.enabled"),
      output ("patch.out", "Output", namesWithNodes (true), 1)
{
    for (int i = 0; i < maxNodes; ++i)
        nodes[(size_t) i] = std::make_unique<Node> (i);

    applied = readStructure();
    numInOrder = computeOrder (applied, order);
}

std::vector<Parameter*> PatchPedal::getControls()
{
    std::vector<Parameter*> controls { &level, &output };

    for (auto& node : nodes)
        for (auto* parameter : node->getParameters())
            controls.push_back (parameter);

    return controls;
}

//==============================================================================
std::vector<int> PatchPedal::getNodesInUse() const
{
    std::vector<int> result;

    for (int i = 0; i < maxNodes; ++i)
    {
        const auto type = nodes[(size_t) i]->getType();

        if (type != BlockType::empty && type != BlockType::numTypes)
            result.push_back (i);
    }

    return result;
}

int PatchPedal::addNode (BlockType type, float x, float y, bool wireIn)
{
    if (type == BlockType::empty || type == BlockType::numTypes || type == BlockType::split || type == BlockType::pathB)
        return -1;

    const auto inUse = getNodesInUse();
    int chosen = -1;

    for (int i = 0; i < maxNodes; ++i)
    {
        if (std::find (inUse.begin(), inUse.end(), i) == inUse.end())
        {
            chosen = i;
            break;
        }
    }

    if (chosen < 0)
        return -1;

    auto& node = *nodes[(size_t) chosen];
    node.block.setFresh (type);
    node.x.set (x);
    node.y.set (y);
    node.inA.set (0.0f);
    node.inB.set (0.0f);
    node.block.type.set ((float) type);

    if (! wireIn)
        return chosen;   // floating: no wires until you draw them

    const auto feedingOutput = getOutputSource();

    if (custom_blocks::isModulator (type))
    {
        node.inA.set ((float) (inputSource + 2));
        setTarget (chosen, feedingOutput);
        return chosen;
    }

    // Wired in just before the output, so adding blocks makes a chain.
    node.inA.set ((float) (feedingOutput + 2));

    if (type == BlockType::mix)
        node.inB.set ((float) (feedingOutput + 2));

    output.set ((float) (chosen + 2));
    return chosen;
}

void PatchPedal::removeNode (int index)
{
    if (! juce::isPositiveAndBelow (index, maxNodes))
        return;

    auto& node = *nodes[(size_t) index];
    const auto replacement = custom_blocks::isModulator (node.getType()) ? noSource : node.getSource (0);
    const auto replace = [&] (Parameter& source)
    {
        if (source.getIndex() - 2 == index)
            source.set ((float) (replacement == index ? 0 : replacement + 2));
    };

    for (auto& other : nodes)
    {
        replace (other->inA);
        replace (other->inB);

        if (other->block.getTargetSlot() == index)
            other->block.target.set (0.0f);
    }

    replace (output);

    node.block.setFresh (BlockType::empty);
    node.inA.set (0.0f);
    node.inB.set (0.0f);
}

bool PatchPedal::connect (int source, int index, int input)
{
    if (! juce::isPositiveAndBelow (index, maxNodes) || (input != 0 && input != 1))
        return false;

    auto& node = *nodes[(size_t) index];

    if (! isSoundBlock (node.getType()) && ! custom_blocks::isModulator (node.getType()))
        return false;

    if (input == 1 && node.getType() != BlockType::mix)
        return false;

    if (source != noSource && source != inputSource)
        if (! juce::isPositiveAndBelow (source, maxNodes) || ! isSoundBlock (nodes[(size_t) source]->getType()))
            return false;

    (input == 0 ? node.inA : node.inB).set ((float) (source + 2));
    return true;
}

bool PatchPedal::connectOutput (int source)
{
    if (source != noSource && source != inputSource)
        if (! juce::isPositiveAndBelow (source, maxNodes) || ! isSoundBlock (nodes[(size_t) source]->getType()))
            return false;

    output.set ((float) (source + 2));
    return true;
}

bool PatchPedal::setTarget (int modulator, int target)
{
    if (! juce::isPositiveAndBelow (modulator, maxNodes))
        return false;

    auto& block = nodes[(size_t) modulator]->block;

    if (target < 0)
    {
        block.target.set (0.0f);
        return true;
    }

    if (! juce::isPositiveAndBelow (target, maxNodes) || target == modulator
        || custom_blocks::getModulatedKnobName (nodes[(size_t) target]->getType())[0] == 0)
        return false;

    block.target.set ((float) (target + 1));
    return true;
}

bool PatchPedal::isLoopWire (int index, int input) const
{
    const auto s = readStructure();
    const auto source = input == 0 ? s.sourceA[(size_t) index] : s.sourceB[(size_t) index];

    if (source < 0)
        return false;

    std::array<int, maxNodes> sorted {};
    const auto count = computeOrder (s, sorted);
    const auto at = [&] (int n) { return (int) (std::find (sorted.begin(), sorted.begin() + count, n) - sorted.begin()); };
    return at (source) >= at (index);
}

//==============================================================================
PatchPedal::Structure PatchPedal::readStructure() const noexcept
{
    Structure s;

    for (int i = 0; i < maxNodes; ++i)
    {
        const auto& node = *nodes[(size_t) i];
        s.types[(size_t) i] = node.getType();
        s.slopes[(size_t) i] = node.block.slope.getIndex();
    }

    // Wires to nothing, or from a modulator, carry no sound.
    const auto clean = [&s] (int source)
    {
        if (source >= 0 && (source >= maxNodes || ! isSoundBlock (s.types[(size_t) source])))
            return (int) noSource;

        return juce::jmax ((int) noSource, source);
    };

    for (int i = 0; i < maxNodes; ++i)
    {
        const auto& node = *nodes[(size_t) i];
        const auto type = s.types[(size_t) i];
        const bool used = type != BlockType::empty && type != BlockType::numTypes;
        s.sourceA[(size_t) i] = used ? clean (node.getSource (0)) : noSource;
        s.sourceB[(size_t) i] = used && type == BlockType::mix ? clean (node.getSource (1)) : noSource;

        const auto target = node.block.getTargetSlot();
        s.targets[(size_t) i] = custom_blocks::isModulator (type) && juce::isPositiveAndBelow (target, maxNodes) && target != i
                                    && custom_blocks::getModulatedKnobName (s.types[(size_t) target])[0] != 0
                                ? target : -1;
    }

    s.out = clean (getOutputSource());
    return s;
}

int PatchPedal::computeOrder (const Structure& s, std::array<int, maxNodes>& sorted) noexcept
{
    // Kahn's method over the wires and the modulator links, without allocating.
    std::array<bool, maxNodes> used {}, placed {};
    std::array<int, maxNodes> waitingFor {};
    int remaining = 0;

    for (int i = 0; i < maxNodes; ++i)
    {
        used[(size_t) i] = s.types[(size_t) i] != BlockType::empty && s.types[(size_t) i] != BlockType::numTypes;
        remaining += used[(size_t) i] ? 1 : 0;
    }

    const auto forEachDependency = [&s] (int node, auto&& visit)
    {
        if (s.sourceA[(size_t) node] >= 0)
            visit (s.sourceA[(size_t) node]);

        if (s.sourceB[(size_t) node] >= 0)
            visit (s.sourceB[(size_t) node]);

        for (int m = 0; m < maxNodes; ++m)
            if (s.targets[(size_t) m] == node)
                visit (m);
    };

    for (int i = 0; i < maxNodes; ++i)
        if (used[(size_t) i])
            forEachDependency (i, [&] (int) { ++waitingFor[(size_t) i]; });

    int count = 0;

    while (count < remaining)
    {
        int next = -1;

        for (int i = 0; i < maxNodes && next < 0; ++i)
            if (used[(size_t) i] && ! placed[(size_t) i] && waitingFor[(size_t) i] == 0)
                next = i;

        // A loop: start it at its lowest-numbered node.
        for (int i = 0; i < maxNodes && next < 0; ++i)
            if (used[(size_t) i] && ! placed[(size_t) i])
                next = i;

        placed[(size_t) next] = true;
        sorted[(size_t) count++] = next;

        for (int i = 0; i < maxNodes; ++i)
            if (used[(size_t) i] && ! placed[(size_t) i])
                forEachDependency (i, [&] (int dependency)
                {
                    if (dependency == next)
                        --waitingFor[(size_t) i];
                });
    }

    return count;
}

void PatchPedal::applyStructure (const Structure& wanted) noexcept
{
    for (int i = 0; i < maxNodes; ++i)
    {
        // A node that changed shape starts from silence and from its knobs' current settings.
        if (wanted.types[(size_t) i] != applied.types[(size_t) i] || wanted.slopes[(size_t) i] != applied.slopes[(size_t) i])
        {
            runners[(size_t) i].reset();
            runners[(size_t) i].sync (nodes[(size_t) i]->block, true);
            outputs[(size_t) i].fill (0.0f);
        }
    }

    applied = wanted;
    numInOrder = computeOrder (applied, order);
    position.fill (maxNodes);

    for (int i = 0; i < numInOrder; ++i)
        position[(size_t) order[(size_t) i]] = i;

    modulatedBy.fill (0);

    for (int m = 0; m < maxNodes; ++m)
        if (applied.targets[(size_t) m] >= 0)
            modulatedBy[(size_t) applied.targets[(size_t) m]] |= 1u << m;

    for (int i = 0; i < maxNodes; ++i)
        runners[(size_t) i].setShape (applied.types[(size_t) i], applied.slopes[(size_t) i] == 1);
}

//==============================================================================
void PatchPedal::prepare (double sampleRate, int maxBlockSize)
{
    dry.assign ((size_t) juce::jmax (1, maxBlockSize), 0.0f);

    for (auto& runner : runners)
        runner.prepare (sampleRate);

    wetMix.reset (sampleRate, crossfadeSeconds);
    outputGain.reset (sampleRate, 0.03);
    reset();
}

void PatchPedal::reset()
{
    applyStructure (readStructure());

    for (int i = 0; i < maxNodes; ++i)
    {
        runners[(size_t) i].reset();
        runners[(size_t) i].sync (nodes[(size_t) i]->block, true);
        runners[(size_t) i].setShape (applied.types[(size_t) i], applied.slopes[(size_t) i] == 1);
        outputs[(size_t) i].fill (0.0f);
    }

    wetMix.setCurrentAndTargetValue (1.0f);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (level.get()));
}

const float* PatchPedal::sourceBuffer (int source, int forNode, float* scratch) noexcept
{
    if (source == noSource)
        return silence.data();

    if (source == inputSource)
        return inputStep.data();

    if (! juce::isPositiveAndBelow (source, maxNodes))
        return silence.data();

    // A loop hears the previous step, softly limited to full scale so it cannot run away.
    if (juce::isPositiveAndBelow (forNode, maxNodes) && position[(size_t) source] >= position[(size_t) forNode])
    {
        for (int i = 0; i < stepSize; ++i)
            scratch[i] = std::tanh (outputs[(size_t) source][(size_t) i]);

        return scratch;
    }

    return outputs[(size_t) source].data();
}

void PatchPedal::runStep (const float* input, float* out, int numSamples) noexcept
{
    std::copy (input, input + numSamples, inputStep.data());

    for (int k = 0; k < numInOrder; ++k)
    {
        const auto n = order[(size_t) k];
        const auto type = applied.types[(size_t) n];
        auto& params = nodes[(size_t) n]->block;
        auto& runner = runners[(size_t) n];
        auto* result = outputs[(size_t) n].data();

        if (custom_blocks::isModulator (type))
        {
            // Its buffer holds how far it pushes its target's knob, not sound.
            const auto source = applied.sourceA[(size_t) n] == noSource ? inputSource : applied.sourceA[(size_t) n];
            const auto* follow = sourceBuffer (source, n, loopScratch.data());
            std::fill (result, result + numSamples, 0.0f);
            runner.modulate (params, follow, applied.targets[(size_t) n] >= 0 ? result : nullptr, numSamples);
            continue;
        }

        if (type == BlockType::mix)
        {
            const auto* a = sourceBuffer (applied.sourceA[(size_t) n], n, loopScratch.data());
            const auto* b = sourceBuffer (applied.sourceB[(size_t) n], n, loopScratchB.data());
            runner.sync (params, false);
            auto& blend = runner.getBlend();

            for (int i = 0; i < numSamples; ++i)
            {
                const auto amount = blend.getNextValue();
                result[i] = a[i] + amount * (b[i] - a[i]);
            }

            continue;
        }

        const auto* source = sourceBuffer (applied.sourceA[(size_t) n], n, loopScratch.data());
        std::copy (source, source + numSamples, result);

        const float* mod = nullptr;

        if (const auto movers = modulatedBy[(size_t) n]; movers != 0)
        {
            // Several modulators on one knob add up, within the full range.
            modulation.fill (0.0f);

            for (int m = 0; m < maxNodes; ++m)
                if ((movers & (1u << m)) != 0)
                    for (int i = 0; i < numSamples; ++i)
                        modulation[(size_t) i] += outputs[(size_t) m][(size_t) i];

            for (int i = 0; i < numSamples; ++i)
                modulation[(size_t) i] = juce::jlimit (-1.0f, 1.0f, modulation[(size_t) i]);

            mod = modulation.data();
        }

        runner.process (params, result, numSamples, mod);

        // A safety net: nothing silly ever reaches the next block.
        for (int i = 0; i < numSamples; ++i)
            if (! (std::abs (result[i]) < 16.0f))
                result[i] = std::isfinite (result[i]) ? std::copysign (16.0f, result[i]) : 0.0f;
    }

    const auto* final = sourceBuffer (applied.out, -1, loopScratch.data());
    std::copy (final, final + numSamples, out);
}

void PatchPedal::process (float* samples, int numSamples) noexcept
{
    if (! shouldProcess())
        return;

    numSamples = juce::jmin (numSamples, (int) dry.size());

    // A change to the wiring or a block's type blends to the dry signal,
    // switches while only the dry signal is heard, then blends back.
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

    for (int offset = 0; offset < numSamples; offset += stepSize)
        runStep (samples + offset, samples + offset, juce::jmin (stepSize, numSamples - offset));

    if (blending)
        for (int i = 0; i < numSamples; ++i)
            samples[i] = dry[(size_t) i] + wetMix.getNextValue() * (samples[i] - dry[(size_t) i]);

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (level.get()));

    for (int i = 0; i < numSamples; ++i)
        samples[i] *= outputGain.getNextValue();
}

//==============================================================================
std::vector<float> PatchPedal::getResponseDb (const std::vector<float>& frequencies, bool& hasBlocks)
{
    hasBlocks = false;

    for (auto n : getNodesInUse())
        hasBlocks = hasBlocks || isSoundBlock (nodes[(size_t) n]->getType());

    // A copy of the patch, measured with an impulse quiet enough that the
    // clippers stay in their gentle region. Modulators are held still and
    // the reducer (which would round a quiet impulse away) is left out.
    PatchPedal copy;
    const auto mine = getControls(), theirs = copy.getControls();

    for (size_t i = 0; i < mine.size(); ++i)
        theirs[i]->set (mine[i]->get());

    copy.level.set (0.0f);
    copy.enabled.set (1.0f);

    for (auto& node : copy.nodes)
    {
        if (custom_blocks::isModulator (node->getType()))
            node->block.depth.set (0.0f);

        if (node->getType() == BlockType::reducer)
            node->block.mix.set (0.0f);
    }

    constexpr int fftOrder = 14, size = 1 << fftOrder, chunk = 512;
    constexpr double sampleRate = 48000.0;
    constexpr float amplitude = 0.001f;
    copy.prepare (sampleRate, chunk);

    std::vector<float> buffer ((size_t) size * 2, 0.0f);
    buffer[0] = amplitude;

    for (int offset = 0; offset < size; offset += chunk)
        copy.process (buffer.data() + offset, chunk);

    for (int i = 0; i < size; ++i)
        buffer[(size_t) i] /= amplitude;

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
