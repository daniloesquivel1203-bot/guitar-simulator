#include "BlockDsp.h"

#include <algorithm>
#include <cmath>

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
        case BlockType::split:      return "Split into two paths";
        case BlockType::pathB:      return "Path B (start the second path)";
        case BlockType::mix:        return "Mix (join the paths)";
        case BlockType::multiply:   return "Multiply (ring mod)";
        case BlockType::rectifier:  return "Rectifier (octave up)";
        case BlockType::ducker:     return "Ducker (one wire turns another down)";
        case BlockType::wavefolder: return "Wavefolder";
        case BlockType::noise:      return "Noise";
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
        case BlockType::multiply:   return "Ring mod";
        case BlockType::rectifier:  return "Rectify";
        case BlockType::ducker:     return "Ducker";
        case BlockType::wavefolder: return "Fold";
        case BlockType::noise:      return "Noise";
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
        case BlockType::split:      return "Copies the signal into two paths. Drag blocks onto the top or bottom path; the + joins them again. Removing the split puts the blocks back in one line.";
        case BlockType::pathB:      return "Starts the second path. Blocks after it, up to the Mix, get the signal from the Split.";
        case BlockType::mix:        return "Joins the two paths back together. Blend sets how much of the bottom path you hear.";
        case BlockType::multiply:   return "Multiplies the top input by the lower one for clangy, bell-like tones. With the lower dot empty it uses its own tone at Frequency.";
        case BlockType::rectifier:  return "Flips the bottom half of the wave up, adding an octave above what you play. Classic octave fuzz; try a clipper after it.";
        case BlockType::wavefolder: return "Folds loud peaks back down instead of flattening them, for a synth-like growl. More drive folds more times.";
        case BlockType::noise:      return "Adds hiss to whatever comes in (or plays it alone). Tone darkens it. An LFO or envelope on Level makes it breathe.";
        case BlockType::ducker:     return "Turns the top input down while the lower one is loud. Dry guitar in the lower dot and a delay in the top: echoes swell when you stop.";
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
        case BlockType::multiply:
        case BlockType::rectifier:
        case BlockType::ducker:
        case BlockType::wavefolder:
        case BlockType::noise:
        case BlockType::numTypes:   m0 = 1.0f; m1 = 0.0f;                     m2 = 0.0f;  break;
    }

    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

namespace
{
constexpr float clipCeiling = 0.25f;   // about -12 dBFS, near a hot guitar signal

bool isFilterShape (BlockType type) noexcept
{
    return type != BlockType::empty && type != BlockType::gain && type != BlockType::clipper
        && type < BlockType::lfo;
}

float clip (float x, int shape) noexcept
{
    switch (shape)
    {
        case hard:        return juce::jlimit (-1.0f, 1.0f, x);
        case diode:       return x / std::pow (1.0f + std::pow (std::abs (x), 2.5f), 0.4f);   // sharper knee than soft
        case asymmetric:  return x > 0.0f ? std::tanh (x) : 0.6f * std::tanh (x / 0.6f);
        default:                       return std::tanh (x);
    }
}
}

bool isFilter (BlockType type) noexcept   { return isFilterShape (type); }

const char* const BlockParams::fieldNames[BlockParams::numFields] = { "type", "freq", "q", "gain", "drive", "shape", "slope", "invert",
                                                                      "rate", "depth", "target", "wave", "sens", "attack", "release",
                                                                      "time", "feedback", "mix", "stages", "bits", "downsample" };

//==============================================================================
BlockParams::BlockParams (const std::function<const char* (int)>& idFor, const std::vector<const char*>& targetNames)
    : type (idFor (0), "Type",
            { "Empty", "Low-pass", "High-pass", "Band-pass", "Notch", "Peak", "Low shelf", "High shelf", "Gain", "Clipper",
              "LFO", "Envelope", "Delay", "All-pass", "Reducer", "Split", "Path B", "Mix",
              "Multiply", "Rectifier", "Ducker", "Wavefolder", "Noise" }),
      frequency (idFor (1), "Frequency", 20.0f, 20000.0f, 1000.0f, 1.0f, " Hz", true),
      resonance (idFor (2), "Resonance", 0.3f, 10.0f, 0.707f, 0.01f, "", true),
      gainDb (idFor (3), "Gain", -24.0f, 24.0f, 0.0f, 0.1f, " dB"),
      driveDb (idFor (4), "Drive", 0.0f, 40.0f, 12.0f, 0.1f, " dB"),
      shape (idFor (5), "Shape", { "Soft", "Hard", "Diode", "Asymmetric" }),
      slope (idFor (6), "Slope", { "12 dB/oct", "24 dB/oct" }),
      invert (idFor (7), "Invert", 0.0f, 1.0f, 0.0f, 1.0f),
      rate (idFor (8), "Rate", 0.05f, 20.0f, 2.0f, 0.01f, " Hz", true),
      depth (idFor (9), "Depth", -100.0f, 100.0f, 50.0f, 1.0f, " %"),
      target (idFor (10), "Target", targetNames),
      wave (idFor (11), "Wave", { "Sine", "Triangle", "Square", "Ramp up", "Ramp down" }),
      sensitivity (idFor (12), "Sensitivity", 0.0f, 40.0f, 12.0f, 0.1f, " dB"),
      attack (idFor (13), "Attack", 1.0f, 200.0f, 10.0f, 0.1f, " ms", true),
      release (idFor (14), "Release", 10.0f, 1000.0f, 150.0f, 1.0f, " ms", true),
      time (idFor (15), "Time", 1.0f, maxDelayMs, 300.0f, 0.1f, " ms", true),
      feedback (idFor (16), "Feedback", 0.0f, 95.0f, 30.0f, 1.0f, " %"),
      mix (idFor (17), "Mix", 0.0f, 100.0f, 50.0f, 1.0f, " %"),
      stages (idFor (18), "Stages", { "2", "4", "6", "8" }, 1),
      bits (idFor (19), "Bits", 2.0f, 16.0f, 8.0f, 1.0f),
      downsample (idFor (20), "Rate divide", 1.0f, 32.0f, 4.0f, 1.0f, "x")
{
}

//==============================================================================
void BlockRunner::prepare (double sampleRate)
{
    rate = sampleRate;
    dcCoefficient = (float) std::exp (-juce::MathConstants<double>::twoPi * 10.0 / sampleRate);
    frequency.reset (sampleRate, 0.03);
    resonance.reset (sampleRate, 0.03);
    gainDb.reset (sampleRate, 0.03);
    driveDb.reset (sampleRate, 0.03);
    depth.reset (sampleRate, 0.03);
    mix.reset (sampleRate, 0.03);
    feedback.reset (sampleRate, 0.03);
    time.reset (sampleRate, 0.08);
    delayLine.assign ((size_t) (maxDelayMs * 0.001 * sampleRate) + 4, 0.0f);   // room for modulation
}

void BlockRunner::reset() noexcept
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
    carrierPhase = 0.0;
    noiseTone = 0.0f;
}

void BlockRunner::setShape (BlockType newType, bool newSteep) noexcept
{
    type = newType;
    steep = newSteep;
    numStages = (type == BlockType::lowPass || type == BlockType::highPass) && steep ? 2 : 1;
    updateFilter();
}

void BlockRunner::sync (const BlockParams& slot, bool snap) noexcept
{
    auto& runner = *this;

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

void configureFilter (BlockType type, float hz, float q, float gainDb, bool steep, double sampleRate,
                                   std::array<SvfStage, 2>& stages, int& numStages) noexcept
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

void BlockRunner::updateFilter (float frequencyRatio) noexcept
{
    if (! isFilter (type))
        return;

    configureFilter (type, frequency.getCurrentValue() * frequencyRatio, resonance.getCurrentValue(),
                     gainDb.getCurrentValue(), steep, rate, stages, numStages);
}

void BlockRunner::process (const BlockParams& slot, float* samples, int numSamples, const float* mod, const float* side) noexcept
{
    auto& runner = *this;
    sync (slot, false);

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
                    updateFilter (mod != nullptr ? std::exp2 (3.0f * mod[i]) : 1.0f);
            }

            auto x = runner.stages[0].process (samples[i]);

            if (runner.numStages == 2)
                x = runner.stages[1].process (x);

            samples[i] = x;
        }

        if (moving)
            updateFilter (mod != nullptr ? std::exp2 (3.0f * mod[numSamples - 1]) : 1.0f);

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
        const auto chainLength = 2 * (slot.stages.getIndex() + 1);

        for (int i = 0; i < numSamples; ++i)
        {
            runner.frequency.getNextValue();
            const auto hz = runner.frequency.getCurrentValue() * (mod != nullptr ? std::exp2 (3.0f * mod[i]) : 1.0f);
            const auto w = std::tan (juce::MathConstants<float>::pi * juce::jlimit (20.0f, (float) (rate * 0.45), hz) / (float) rate);
            const auto a = (w - 1.0f) / (w + 1.0f);

            auto x = samples[i];

            for (int s = 0; s < chainLength; ++s)
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

    if (type == BlockType::multiply)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float other;

            if (side != nullptr)
            {
                // Two guitar-level signals multiplied come out very quiet; make up most of it.
                other = 4.0f * side[i];
            }
            else
            {
                // Its own sine at Frequency, which a modulator sweeps up to 3 octaves either way.
                const auto hz = runner.frequency.getNextValue() * (mod != nullptr ? std::exp2 (3.0f * mod[i]) : 1.0f);
                runner.carrierPhase += juce::jlimit (0.0, 0.49, (double) hz / rate);
                runner.carrierPhase -= std::floor (runner.carrierPhase);
                other = std::sin (juce::MathConstants<float>::twoPi * (float) runner.carrierPhase);
            }

            const auto ringed = samples[i] * other;
            samples[i] = (samples[i] + runner.mix.getNextValue() * (ringed - samples[i]))
                       * juce::Decibels::decibelsToGain (runner.gainDb.getNextValue());
        }

        return;
    }

    if (type == BlockType::rectifier)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            // Full-wave: the wave's bottom half flipped up doubles its pitch, and leaves an offset to take out.
            const auto flipped = std::abs (samples[i]);
            const auto blocked = flipped - runner.dcX1 + dcCoefficient * runner.dcY1;
            runner.dcX1 = flipped;
            runner.dcY1 = blocked;

            samples[i] = (samples[i] + runner.mix.getNextValue() * (blocked - samples[i]))
                       * juce::Decibels::decibelsToGain (runner.gainDb.getNextValue());
        }

        return;
    }

    if (type == BlockType::wavefolder)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            // A sine fold: gentle like a soft clip at first, then the peaks fold back over and over.
            // A modulator pushes the drive up to 24 dB either way.
            const auto drive = juce::Decibels::decibelsToGain (runner.driveDb.getNextValue()
                                                               + (mod != nullptr ? 24.0f * mod[i] : 0.0f));
            const auto folded = clipCeiling * std::sin (juce::MathConstants<float>::halfPi * samples[i] * drive / clipCeiling);
            samples[i] = (samples[i] + runner.mix.getNextValue() * (folded - samples[i]))
                       * juce::Decibels::decibelsToGain (runner.gainDb.getNextValue());
        }

        return;
    }

    if (type == BlockType::noise)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            // xorshift: cheap, and never the same hiss twice.
            auto& x = runner.noiseState;
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            const auto white = (float) (int32_t) x * (1.0f / 2147483648.0f);

            // Tone: a gentle one-pole low-pass.
            const auto hz = runner.frequency.getNextValue();
            const auto coefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * juce::jmin (hz, (float) (rate * 0.45)) / (float) rate);
            runner.noiseTone += coefficient * (white - runner.noiseTone);

            // Level, squared so the knob feels even; a modulator moves it between silence and double.
            const auto level = runner.mix.getNextValue() * (mod != nullptr ? juce::jmax (0.0f, 1.0f + mod[i]) : 1.0f);
            samples[i] += clipCeiling * level * level * runner.noiseTone;
        }

        return;
    }

    if (type == BlockType::ducker)
    {
        // Follows the lower input (or, with none wired, the block's own input) like an Envelope does.
        const auto* key = side != nullptr ? side : samples;
        const auto gain = juce::Decibels::decibelsToGain (slot.sensitivity.get()) / clipCeiling;
        const auto up = (float) (1.0 - std::exp (-1.0 / (slot.attack.get() * 0.001 * rate)));
        const auto down = (float) (1.0 - std::exp (-1.0 / (slot.release.get() * 0.001 * rate)));

        for (int i = 0; i < numSamples; ++i)
        {
            const auto x = std::abs (key[i]) * gain;
            runner.envelope += (x > runner.envelope ? up : down) * (x - runner.envelope);
            samples[i] *= 1.0f - runner.mix.getNextValue() * juce::jmin (1.0f, runner.envelope);
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

void BlockRunner::modulate (const BlockParams& s, const float* input, float* out, int numSamples) noexcept
{
    auto& runner = *this;
    sync (s, false);

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

            const auto amount = runner.depth.getNextValue();

            if (out != nullptr)
                out[i] += amount * value;
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
            const auto amount = runner.depth.getNextValue();

            if (out != nullptr)
                out[i] += amount * juce::jmin (1.0f, runner.envelope);
        }
    }
}

const char* getModulatedKnobName (BlockType type) noexcept
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
        case BlockType::multiply:   return "frequency";
        case BlockType::wavefolder: return "drive";
        case BlockType::noise:      return "level";
        case BlockType::rectifier:
        case BlockType::ducker:
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

std::vector<Control> getControlsFor (BlockParams& slot, BlockType type)
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
        case BlockType::multiply:   return { { &slot.frequency, "Frequency" }, { &slot.mix, "Mix" }, { &slot.gainDb, "Level" } };
        case BlockType::rectifier:  return { { &slot.mix, "Mix" }, { &slot.gainDb, "Level" } };
        case BlockType::wavefolder: return { { &slot.driveDb, "Drive" }, { &slot.mix, "Mix" }, { &slot.gainDb, "Output" } };
        case BlockType::noise:      return { { &slot.mix, "Level" }, { &slot.frequency, "Tone" } };
        case BlockType::ducker:     return { { &slot.mix, "Amount" }, { &slot.sensitivity, "Sensitivity" },
                                             { &slot.attack, "Attack" }, { &slot.release, "Release" } };
        case BlockType::split:
        case BlockType::pathB:
        case BlockType::empty:
        case BlockType::numTypes:   break;
    }

    return {};
}

void BlockParams::setFresh (BlockType newType)
{
    for (auto* parameter : getParameters())
        parameter->set (parameter->defaultValue);

    auto& slot = *this;

    // Sensible starting points, so a new block does something you can hear.
    switch (newType)
    {
        case BlockType::lowPass:    slot.frequency.set (3000.0f); break;
        case BlockType::highPass:   slot.frequency.set (150.0f);  break;
        case BlockType::bandPass:   slot.frequency.set (800.0f);  slot.resonance.set (2.0f); break;
        case BlockType::notch:      slot.frequency.set (1000.0f); slot.resonance.set (2.0f); break;
        case BlockType::peak:       slot.frequency.set (800.0f);  slot.gainDb.set (6.0f); slot.resonance.set (1.0f); break;
        case BlockType::lowShelf:   slot.frequency.set (200.0f);  slot.gainDb.set (4.0f); break;
        case BlockType::highShelf:  slot.frequency.set (3000.0f); slot.gainDb.set (4.0f); break;
        case BlockType::clipper:    slot.gainDb.set (-4.0f); break;
        case BlockType::allPass:    slot.frequency.set (700.0f); slot.mix.set (100.0f); break;
        case BlockType::reducer:    slot.mix.set (100.0f); break;
        case BlockType::multiply:   slot.frequency.set (440.0f); slot.mix.set (100.0f); break;
        case BlockType::rectifier:  slot.mix.set (100.0f); break;
        case BlockType::wavefolder: slot.mix.set (100.0f); slot.gainDb.set (-2.0f); break;
        case BlockType::noise:      slot.mix.set (25.0f); slot.frequency.set (8000.0f); break;
        case BlockType::ducker:     slot.mix.set (80.0f); slot.release.set (400.0f); break;
        case BlockType::gain:
        case BlockType::delay:
        case BlockType::split:
        case BlockType::pathB:
        case BlockType::mix:
        case BlockType::lfo:
        case BlockType::envelope:
        case BlockType::empty:
        case BlockType::numTypes:   break;
    }
}
}
