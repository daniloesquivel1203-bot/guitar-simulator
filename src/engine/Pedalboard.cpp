#include "Pedalboard.h"

namespace
{
constexpr double fadeSeconds = 0.005;

template <typename EffectType>
std::function<std::unique_ptr<Effect>()> maker()
{
    return [] { return std::make_unique<EffectType>(); };
}
}

const std::vector<PedalType>& getPedalTypes()
{
    static const std::vector<PedalType> types =
    {
        // Dynamics and utility
        { "gate",        "Gate",     "Dynamics",   maker<NoiseGate>() },
        { "comp",        "Comp",     "Dynamics",   maker<Compressor>() },
        { "opto",        "Opto",     "Dynamics",   maker<OptoCompressor>() },
        { "boost",       "Boost",    "Dynamics",   maker<CleanBoost>() },
        { "swell",       "Swell",    "Dynamics",   maker<AutoSwell>() },

        // Overdrive, distortion and fuzz
        { "treble",      "Treble",   "Drive",      maker<TrebleBooster>() },
        { "blues",       "Blues",    "Drive",      maker<BluesDrive>() },
        { "od",          "Drive",    "Drive",      maker<Overdrive>() },
        { "dist",        "Dist",     "Drive",      maker<Distortion>() },
        { "metal",       "Metal",    "Drive",      maker<MetalDistortion>() },
        { "fuzz",        "Fuzz",     "Drive",      maker<VintageFuzz>() },
        { "bigfuzz",     "Sustain",  "Drive",      maker<SustainFuzz>() },
        { "octfuzz",     "OctFuzz",  "Drive",      maker<OctaveFuzz>() },
        { "crusher",     "Crush",    "Drive",      maker<BitCrusher>() },

        // Filters and EQ
        { "eq",          "EQ",       "Filter",     maker<Equaliser>() },
        { "geq",         "GEQ",      "Filter",     maker<GraphicEq>() },
        { "wah",         "Wah",      "Filter",     maker<Wah>() },
        { "envfilter",   "Env",      "Filter",     maker<EnvelopeFilter>() },
        { "vowel",       "Vowel",    "Filter",     maker<VowelFilter>() },

        // Modulation
        { "chorus",      "Chorus",   "Modulation", maker<Chorus>() },
        { "flanger",     "Flanger",  "Modulation", maker<Flanger>() },
        { "phaser",      "Phaser",   "Modulation", maker<Phaser>() },
        { "vibe",        "Vibe",     "Modulation", maker<Vibe>() },
        { "vibrato",     "Vibrato",  "Modulation", maker<Vibrato>() },
        { "trem",        "Trem",     "Modulation", maker<Tremolo>() },
        { "rotary",      "Rotary",   "Modulation", maker<RotarySpeaker>() },
        { "ringmod",     "Ring",     "Modulation", maker<RingModulator>() },

        // Pitch
        { "octave",      "Octave",   "Pitch",      maker<Octaver>() },
        { "pitch",       "Pitch",    "Pitch",      maker<PitchShifter>() },
        { "detune",      "Detune",   "Pitch",      maker<Detune>() },

        // Delay
        { "delay",       "Delay",    "Delay",      maker<Delay>() },
        { "tape",        "Tape",     "Delay",      maker<TapeEcho>() },
        { "analogdelay", "Analog",   "Delay",      maker<AnalogDelay>() },
        { "reverse",     "Reverse",  "Delay",      maker<ReverseDelay>() },

        // Reverb
        { "reverb",      "Reverb",   "Reverb",     maker<Reverb>() },
        { "spring",      "Spring",   "Reverb",     maker<SpringReverb>() },
        { "plate",       "Plate",    "Reverb",     maker<PlateReverb>() },
        { "hall",        "Hall",     "Reverb",     maker<HallReverb>() },
        { "shimmer",     "Shimmer",  "Reverb",     maker<ShimmerReverb>() },
    };

    return types;
}

const PedalType* findPedalType (const juce::String& key)
{
    for (const auto& type : getPedalTypes())
        if (key == type.key)
            return &type;

    return nullptr;
}

//==============================================================================
Pedal::Pedal (const PedalType& typeToUse)
    : type (typeToUse), effect (type.create())
{
    mono = dynamic_cast<MonoEffect*> (effect.get());
    stereo = dynamic_cast<StereoEffect*> (effect.get());
    jassert ((mono != nullptr) != (stereo != nullptr));

    if (mono != nullptr)
    {
        twin = type.create();
        monoTwin = dynamic_cast<MonoEffect*> (twin.get());

        twinLinks.emplace_back (&effect->enabled, &twin->enabled);
        const auto mainControls = effect->getControls();
        const auto twinControls = twin->getControls();

        for (size_t i = 0; i < mainControls.size(); ++i)
            twinLinks.emplace_back (mainControls[i], twinControls[i]);
    }
}

std::vector<Parameter*> Pedal::getParameters()
{
    std::vector<Parameter*> parameters { &effect->enabled };

    for (auto* control : effect->getControls())
        parameters.push_back (control);

    return parameters;
}

void Pedal::prepare (double sampleRate, int maxBlockSize)
{
    effect->prepare (sampleRate, maxBlockSize);

    if (twin != nullptr)
        twin->prepare (sampleRate, maxBlockSize);

    scratch.assign ((size_t) maxBlockSize, 0.0f);
    preparedRate = sampleRate;
    preparedBlockSize = maxBlockSize;
    reset();
}

bool Pedal::isPreparedFor (double sampleRate, int maxBlockSize) const noexcept
{
    return juce::exactlyEqual (preparedRate, sampleRate) && preparedBlockSize == maxBlockSize;
}

void Pedal::reset()
{
    effect->reset();

    if (twin != nullptr)
        twin->reset();

    twinInUse = false;
}

void Pedal::setFedByGuitar (bool isFedByGuitar) noexcept
{
    effect->setFedByGuitar (isFedByGuitar);

    if (twin != nullptr)
        twin->setFedByGuitar (isFedByGuitar);
}

void Pedal::processMono (float* samples, int numSamples) noexcept
{
    if (mono != nullptr)
    {
        twinInUse = false;
        mono->process (samples, numSamples);
        return;
    }

    // A stereo pedal on a mono signal, such as a chorus before the amp: run it
    // in stereo and mix the two sides back together.
    jassert (numSamples <= (int) scratch.size());
    std::copy (samples, samples + numSamples, scratch.data());
    stereo->process (samples, scratch.data(), numSamples);

    for (int i = 0; i < numSamples; ++i)
        samples[i] = 0.5f * (samples[i] + scratch[(size_t) i]);
}

void Pedal::processStereo (float* left, float* right, int numSamples) noexcept
{
    if (stereo != nullptr)
    {
        stereo->process (left, right, numSamples);
        return;
    }

    for (auto& [source, copy] : twinLinks)
        copy->set (source->get());

    // The twin may hold state from long ago, from before the pedal was moved.
    if (! twinInUse)
    {
        monoTwin->reset();
        twinInUse = true;
    }

    mono->process (left, numSamples);
    monoTwin->process (right, numSamples);
}

//==============================================================================
Pedalboard::Pedalboard()
{
    active = new Layout();
    fade.reset (sampleRate, fadeSeconds);
    fade.setCurrentAndTargetValue (1.0f);
}

Pedalboard::~Pedalboard()
{
    collectGarbage();
    delete pending.exchange (nullptr);
    delete active;
}

std::shared_ptr<Pedal> Pedalboard::add (Side side, const PedalType& type, int index)
{
    auto& list = model[(size_t) side];

    if ((int) list.size() >= maxPedalsPerSide)
        return nullptr;

    auto pedal = std::make_shared<Pedal>(type);
    pedal->getEffect().enabled.set (1.0f);

    if (! juce::isPositiveAndNotGreaterThan (index, (int) list.size()))
        index = (int) list.size();

    list.insert (list.begin() + index, pedal);
    publish();
    return pedal;
}

void Pedalboard::remove (Side side, int index)
{
    auto& list = model[(size_t) side];

    if (! juce::isPositiveAndBelow (index, (int) list.size()))
        return;

    list.erase (list.begin() + index);
    publish();
}

bool Pedalboard::move (Side fromSide, int fromIndex, Side toSide, int toIndex)
{
    auto& from = model[(size_t) fromSide];
    auto& to = model[(size_t) toSide];

    if (! juce::isPositiveAndBelow (fromIndex, (int) from.size()))
        return false;

    if (fromSide != toSide && (int) to.size() >= maxPedalsPerSide)
        return false;

    if (fromSide == toSide && fromIndex == toIndex)
        return false;

    auto pedal = from[(size_t) fromIndex];
    from.erase (from.begin() + fromIndex);
    toIndex = juce::jlimit (0, (int) to.size(), toIndex);
    to.insert (to.begin() + toIndex, std::move (pedal));
    publish();
    return true;
}

void Pedalboard::setPedals (PedalList before, PedalList after)
{
    before.resize (juce::jmin (before.size(), (size_t) maxPedalsPerSide));
    after.resize (juce::jmin (after.size(), (size_t) maxPedalsPerSide));

    model[beforeAmp] = std::move (before);
    model[afterAmp] = std::move (after);
    publish();
}

void Pedalboard::makeClassicLayout (PedalList& before, PedalList& after)
{
    before.clear();
    after.clear();

    for (const auto* key : { "gate", "comp", "od", "dist" })
        before.push_back (std::make_shared<Pedal> (*findPedalType (key)));

    for (const auto* key : { "eq", "chorus", "delay", "reverb" })
        after.push_back (std::make_shared<Pedal> (*findPedalType (key)));
}

void Pedalboard::publish()
{
    auto layout = std::make_unique<Layout>();
    layout->pedals = model;

    {
        const std::scoped_lock lock (prepareMutex);
        prepareNewPedals (*layout);
    }

    // If the audio thread never took the previous edit, it is simply replaced.
    delete pending.exchange (layout.release());
    collectGarbage();
}

void Pedalboard::prepareNewPedals (Layout& layout)
{
    for (auto& list : layout.pedals)
        for (auto& pedal : list)
            if (! pedal->isPreparedFor (sampleRate, blockSize))
                pedal->prepare (sampleRate, blockSize);
}

void Pedalboard::collectGarbage()
{
    const auto scope = retiredFifo.read (retiredFifo.getNumReady());

    scope.forEach ([this] (int index)
    {
        delete retired[(size_t) index];
        retired[(size_t) index] = nullptr;
    });
}

void Pedalboard::prepare (double newSampleRate, int maxBlockSize)
{
    const std::scoped_lock lock (prepareMutex);

    sampleRate = newSampleRate;
    blockSize = maxBlockSize;

    // Nothing is processing now, so a waiting edit can be applied at once.
    if (auto* next = pending.exchange (nullptr))
    {
        delete active;
        active = next;
    }

    for (auto& list : active->pedals)
        for (auto& pedal : list)
            pedal->prepare (sampleRate, blockSize);

    fade.reset (sampleRate, fadeSeconds);
    fade.setCurrentAndTargetValue (1.0f);
}

void Pedalboard::reset()
{
    for (auto& list : active->pedals)
        for (auto& pedal : list)
            pedal->reset();

    fade.setCurrentAndTargetValue (1.0f);
}

void Pedalboard::beginChunk() noexcept
{
    if (pending.load() == nullptr)
        return;

    // Fade out first, then switch while the sound is silent.
    if (fade.getTargetValue() > 0.0f)
    {
        fade.setTargetValue (0.0f);
        return;
    }

    if (fade.isSmoothing() || retiredFifo.getFreeSpace() == 0)
        return;

    if (auto* next = pending.exchange (nullptr))
    {
        retiredFifo.write (1).forEach ([this] (int index) { retired[(size_t) index] = active; });
        active = next;
    }

    fade.setTargetValue (1.0f);
}

void Pedalboard::processBeforeAmp (float* mono, int numSamples) noexcept
{
    // A pedal that is switched off passes the guitar straight through, so a
    // pedal only stops hearing the pickups once one in front of it is on.
    bool fedByGuitar = true;

    for (auto& pedal : active->pedals[beforeAmp])
    {
        pedal->setFedByGuitar (fedByGuitar);
        pedal->processMono (mono, numSamples);
        fedByGuitar = fedByGuitar && ! pedal->getEffect().enabled.isOn();
    }
}

void Pedalboard::processAfterAmp (float* left, float* right, int numSamples) noexcept
{
    // The signal stays mono until the first stereo pedal, so mono pedals
    // before it (such as the EQ) need no twin.
    bool isStereo = false;

    for (auto& pedal : active->pedals[afterAmp])
    {
        pedal->setFedByGuitar (false);

        if (pedal->isStereo() && ! isStereo)
        {
            std::copy (left, left + numSamples, right);
            isStereo = true;
        }

        if (isStereo)
            pedal->processStereo (left, right, numSamples);
        else
            pedal->processMono (left, numSamples);
    }

    if (! isStereo)
        std::copy (left, left + numSamples, right);
}
