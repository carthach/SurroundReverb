#include "ReverbEngine.h"

#include <bit>

namespace
{
    // Mutually incommensurate loop lengths, at a size scale of 1
    constexpr std::array<float, ReverbEngine::numLines> lineLengthsMs {
        29.3f, 31.7f, 34.9f, 37.1f, 41.3f, 43.7f, 47.9f, 53.3f, 59.1f, 61.3f, 67.7f, 71.9f, 79.3f, 83.9f, 89.7f, 97.1f
    };

    constexpr std::array<float, 3> diffuserLengthsMs { 2.1f, 5.3f, 8.9f };

    constexpr float minSizeScale = 0.35f;
    constexpr float maxSizeScale = 1.75f;
    constexpr float maxModulationSeconds = 0.0015f;
    constexpr float smoothingSeconds = 0.05f;

    constexpr float rearToneHz = 4000.0f;

    // The top of the LFE channel's range
    constexpr float lfeLowpassHz = 120.0f;

    float sizeToScale (float size)
    {
        return juce::jmap (juce::jlimit (0.0f, 1.0f, size), minSizeScale, maxSizeScale);
    }

    // In place fast Walsh-Hadamard transform, scaled to be orthonormal
    void hadamard (std::array<float, ReverbEngine::numLines>& v)
    {
        for (size_t length = 1; length < v.size(); length *= 2)
        {
            for (size_t i = 0; i < v.size(); i += 2 * length)
            {
                for (size_t j = i; j < i + length; ++j)
                {
                    const auto a = v[j];
                    const auto b = v[j + length];
                    v[j] = a + b;
                    v[j + length] = a - b;
                }
            }
        }

        for (auto& sample : v)
            sample *= 0.25f;
    }
}

//==============================================================================
ReverbEngine::Layout ReverbEngine::Layout::matched (int numChannels)
{
    Layout result;
    result.numInputs = numChannels;
    result.numOutputs = numChannels;

    for (size_t i = 0; i < maxChannels; ++i)
    {
        result.inputRoles[i] = Role::front;
        result.outputRoles[i] = Role::front;
        result.dryDestinations[i] = { (int) i, -1 };
    }

    return result;
}

//==============================================================================
void ReverbEngine::DelayLine::resize (int maxDelaySamples)
{
    // Leaves headroom for the extra taps the interpolated reads need
    const auto size = juce::nextPowerOfTwo (maxDelaySamples + 8);
    data.assign ((size_t) size, 0.0f);
    mask = size - 1;
    writePosition = 0;
}

void ReverbEngine::DelayLine::clear()
{
    std::fill (data.begin(), data.end(), 0.0f);
}

void ReverbEngine::DelayLine::push (float sample)
{
    data[(size_t) writePosition] = sample;
    writePosition = (writePosition + 1) & mask;
}

float ReverbEngine::DelayLine::read (int delay) const
{
    return data[(size_t) ((writePosition - delay) & mask)];
}

float ReverbEngine::DelayLine::readLinear (float delay) const
{
    const auto whole = (int) delay;
    const auto fraction = delay - (float) whole;
    const auto a = read (whole);
    const auto b = read (whole + 1);
    return a + fraction * (b - a);
}

float ReverbEngine::DelayLine::readCubic (float delay) const
{
    // Third order Lagrange interpolation. Unlike linear interpolation it barely dulls the top end,
    // and its gain never exceeds 1 here, so it can't make the feedback loop unstable.
    const auto whole = (int) delay;
    const auto d = delay - (float) whole + 1.0f;

    const auto x0 = read (whole - 1);
    const auto x1 = read (whole);
    const auto x2 = read (whole + 1);
    const auto x3 = read (whole + 2);

    const auto h0 = -(d - 1.0f) * (d - 2.0f) * (d - 3.0f) / 6.0f;
    const auto h1 = d * (d - 2.0f) * (d - 3.0f) / 2.0f;
    const auto h2 = -d * (d - 1.0f) * (d - 3.0f) / 2.0f;
    const auto h3 = d * (d - 1.0f) * (d - 2.0f) / 6.0f;

    return h0 * x0 + h1 * x1 + h2 * x2 + h3 * x3;
}

//==============================================================================
float ReverbEngine::Allpass::process (float input, float coefficient)
{
    const auto delayed = delay.read (length);
    const auto feedforward = input - coefficient * delayed;
    delay.push (feedforward);
    return delayed + coefficient * feedforward;
}

void ReverbEngine::FilterCoefficients::set (float frequency, float sampleRateToUse)
{
    const auto limited = juce::jlimit (10.0f, 0.45f * sampleRateToUse, frequency);
    const auto g = std::tan (juce::MathConstants<float>::pi * limited / sampleRateToUse);

    k = juce::MathConstants<float>::sqrt2; // Butterworth
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

float ReverbEngine::Filter::tick (float input, const FilterCoefficients& c, float& band)
{
    const auto v3 = input - ic2;
    band = c.a1 * ic1 + c.a2 * v3;
    const auto low = ic2 + c.a2 * ic1 + c.a3 * v3;
    ic1 = 2.0f * band - ic1;
    ic2 = 2.0f * low - ic2;
    return low;
}

float ReverbEngine::Filter::lowpass (float input, const FilterCoefficients& c)
{
    float band;
    return tick (input, c, band);
}

float ReverbEngine::Filter::highpass (float input, const FilterCoefficients& c)
{
    float band;
    const auto low = tick (input, c, band);
    return input - c.k * band - low;
}

void ReverbEngine::Filter::reset()
{
    ic1 = 0.0f;
    ic2 = 0.0f;
}

float ReverbEngine::OnePole::coefficientFor (float frequency, float sampleRateToUse)
{
    const auto limited = juce::jlimit (5.0f, 0.45f * sampleRateToUse, frequency);
    const auto g = std::tan (juce::MathConstants<float>::pi * limited / sampleRateToUse);
    return g / (1.0f + g);
}

float ReverbEngine::OnePole::lowpass (float input, float coefficient)
{
    const auto v = (input - state) * coefficient;
    const auto low = v + state;
    state = low + v;
    return low;
}

template <typename Function>
void ReverbEngine::forEachSmoother (Function&& function)
{
    for (auto* smoothed : { &dryGain, &wetGain, &sizeScale, &modulationDepth, &predelaySamples, &rearDelaySamples, &lfeSend, &lfeReturn, &decay, &diffusion, &lowCut, &highCut, &lowDampFrequency, &lowDampRatio, &highDampFrequency, &highDampRatio, &rearTone })
        function (*smoothed);

    for (auto& gain : roleGains)
        function (gain);
}

//==============================================================================
void ReverbEngine::prepare (double newSampleRate, const Layout& newLayout)
{
    jassert (newLayout.numInputs <= maxChannels && newLayout.numOutputs <= maxChannels);

    sampleRate = newSampleRate;
    layout = newLayout;
    layout.numInputs = juce::jlimit (0, maxChannels, layout.numInputs);
    layout.numOutputs = juce::jlimit (0, maxChannels, layout.numOutputs);

    const auto fs = (float) sampleRate;
    const auto msToSamples = [fs] (float ms) { return ms * 0.001f * fs; };

    for (size_t i = 0; i < maxChannels; ++i)
    {
        auto& input = inputs[i];

        if ((int) i < layout.numInputs)
        {
            input.predelay.resize (juce::roundToInt (msToSamples (maxPredelayMs)));

            // Slightly different diffuser lengths per channel keep correlated inputs from staying correlated
            const auto stretch = 1.0f + 0.061f * (float) i;

            for (size_t stage = 0; stage < numDiffusers; ++stage)
            {
                auto& diffuser = input.diffusers[stage];
                diffuser.length = juce::jmax (1, juce::roundToInt (msToSamples (diffuserLengthsMs[stage] * stretch)));
                diffuser.delay.resize (diffuser.length);
            }
        }

        if ((int) i < layout.numOutputs && layout.outputRoles[i] == Role::surround)
            rearDelays[i].resize (juce::roundToInt (msToSamples (maxRearDelayMs)));
    }

    const auto modulationHeadroom = maxModulationSeconds * fs;
    maxLineDelay = msToSamples (lineLengthsMs.back()) * maxSizeScale + modulationHeadroom;

    for (size_t i = 0; i < numLines; ++i)
    {
        auto& line = lines[i];
        line.baseSeconds = lineLengthsMs[i] * 0.001f;
        line.baseSamples = line.baseSeconds * fs;
        line.delay.resize (juce::roundToInt (maxLineDelay) + 1);

        // A sine oscillator per line, each at its own slow rate
        const auto rateHz = 0.31f + 0.057f * (float) i;
        line.lfoRate = 2.0f * std::sin (juce::MathConstants<float>::pi * rateHz / fs);
    }

    lfeCoefficients.set (lfeLowpassHz, fs);

    forEachSmoother ([this] (juce::SmoothedValue<float>& smoothed) { smoothed.reset (sampleRate, smoothingSeconds); });

    updateRouting();
    setParameters (parameters);
    reset();
}

void ReverbEngine::updateRouting()
{
    // Each input drives every line, using its own row of a Hadamard matrix as the sign pattern
    for (size_t input = 0; input < maxChannels; ++input)
    {
        const auto row = (input + 1) & (numLines - 1);

        for (size_t line = 0; line < numLines; ++line)
        {
            const auto negative = (std::popcount (row & line) & 1) != 0;
            injection[input][line] = negative ? -0.25f : 0.25f;
        }

        const auto hasTwoDestinations = layout.dryDestinations[input][1] >= 0;
        inputs[input].dryGain = hasTwoDestinations ? juce::MathConstants<float>::sqrt2 * 0.5f : 1.0f;
    }

    // Deal the lines out between the speakers. Every line belongs to exactly one speaker, which is
    // what keeps the outputs decorrelated from each other.
    std::array<int, maxChannels> speakers {};
    numSpeakers = 0;

    for (int output = 0; output < layout.numOutputs; ++output)
        if (layout.outputRoles[(size_t) output] != Role::lfe)
            speakers[(size_t) numSpeakers++] = output;

    if (numSpeakers == 0)
    {
        // Nothing but LFE channels, so there's nowhere for a tail to go
        for (auto& line : lines)
        {
            line.output = 0;
            line.tapGain = 0.0f;
            line.lfeTapGain = 0.0f;
        }

        return;
    }

    std::array<int, maxChannels> linesPerSpeaker {};

    for (int i = 0; i < numLines; ++i)
        ++linesPerSpeaker[(size_t) (i % numSpeakers)];

    for (int i = 0; i < numLines; ++i)
    {
        auto& line = lines[(size_t) i];
        const auto speaker = i % numSpeakers;
        const auto sign = ((i / numSpeakers) & 1) != 0 ? -1.0f : 1.0f;

        // Normalised so that the total wet energy doesn't depend on the number of speakers
        line.output = speakers[(size_t) speaker];
        line.tapGain = sign * std::sqrt ((float) numLines / (float) (numSpeakers * linesPerSpeaker[(size_t) speaker]));

        // The LFE return is a mix of every line, at the same level as one speaker's share
        const auto lfeSign = (std::popcount ((unsigned int) i) & 1) != 0 ? -1.0f : 1.0f;
        line.lfeTapGain = lfeSign / std::sqrt ((float) numSpeakers);
    }
}

void ReverbEngine::setParameters (const Parameters& newParameters)
{
    parameters = newParameters;

    const auto fs = (float) sampleRate;
    const auto mix = juce::jlimit (0.0f, 1.0f, parameters.mix);

    // Equal power crossfade. cos (pi / 2) isn't quite zero in floating point, so a fully wet mix is special cased.
    dryGain.setTargetValue (mix < 1.0f ? std::cos (mix * juce::MathConstants<float>::halfPi) : 0.0f);
    wetGain.setTargetValue (std::sin (mix * juce::MathConstants<float>::halfPi));
    sizeScale.setTargetValue (sizeToScale (parameters.size));
    modulationDepth.setTargetValue (juce::jlimit (0.0f, 1.0f, parameters.modulation) * maxModulationSeconds * fs);

    // Whole sample delays, so the interpolated read is transparent whenever the delay isn't moving
    predelaySamples.setTargetValue (std::round (juce::jlimit (0.0f, maxPredelayMs, parameters.predelayMs) * 0.001f * fs));
    rearDelaySamples.setTargetValue (std::round (juce::jlimit (0.0f, maxRearDelayMs, parameters.rearDelayMs) * 0.001f * fs));

    roleGains[(size_t) Role::front].setTargetValue (parameters.frontGain);
    roleGains[(size_t) Role::centre].setTargetValue (parameters.centreGain);
    roleGains[(size_t) Role::surround].setTargetValue (parameters.surroundGain);
    roleGains[(size_t) Role::height].setTargetValue (parameters.heightGain);

    lfeSend.setTargetValue (parameters.lfeSendGain);
    lfeReturn.setTargetValue (parameters.lfeReturnGain);

    decay.setTargetValue (juce::jmax (0.05f, parameters.decaySeconds));
    lowDampFrequency.setTargetValue (parameters.lowDampHz);
    lowDampRatio.setTargetValue (juce::jlimit (minDampRatio, maxDampRatio, parameters.lowDampRatio));
    highDampFrequency.setTargetValue (parameters.highDampHz);
    highDampRatio.setTargetValue (juce::jlimit (minDampRatio, maxDampRatio, parameters.highDampRatio));
    rearTone.setTargetValue (juce::jlimit (-12.0f, 12.0f, parameters.rearToneDb));
    diffusion.setTargetValue (juce::jlimit (0.0f, 1.0f, parameters.diffusion));
    lowCut.setTargetValue (parameters.lowCutHz);
    highCut.setTargetValue (parameters.highCutHz);
}

void ReverbEngine::reset()
{
    forEachSmoother ([] (juce::SmoothedValue<float>& smoothed) { smoothed.setCurrentAndTargetValue (smoothed.getTargetValue()); });

    for (auto& input : inputs)
    {
        input.predelay.clear();
        input.lowCut.reset();
        input.highCut.reset();

        for (auto& diffuser : input.diffusers)
            diffuser.delay.clear();
    }

    for (size_t i = 0; i < numLines; ++i)
    {
        auto& line = lines[i];
        line.delay.clear();
        line.lowBand.state = 0.0f;
        line.highBand.state = 0.0f;

        // Spread the oscillators' starting phases so the lines never move in step
        const auto phase = juce::MathConstants<float>::twoPi * (float) i / (float) numLines;
        line.lfoSin = std::sin (phase);
        line.lfoCos = std::cos (phase);
    }

    for (auto& rearDelay : rearDelays)
        rearDelay.clear();

    for (auto& rearToneFilter : rearTones)
        rearToneFilter.state = 0.0f;

    for (auto& lfeFilter : lfeFilters)
        lfeFilter.reset();

    updateControlRate (0);
}

//==============================================================================
void ReverbEngine::updateControlRate (int numSamples)
{
    const auto fs = (float) sampleRate;
    const auto decaySeconds = decay.skip (numSamples);
    const auto lowFrequency = lowDampFrequency.skip (numSamples);
    const auto highFrequency = highDampFrequency.skip (numSamples);
    const auto scale = sizeScale.getCurrentValue();

    // How much more (or less) attenuation the low and high bands get per pass, relative to the mids
    const auto lowExcess = 1.0f / lowDampRatio.skip (numSamples) - 1.0f;
    const auto highExcess = 1.0f / highDampRatio.skip (numSamples) - 1.0f;

    lowCutCoefficients.set (lowCut.skip (numSamples), fs);
    highCutCoefficients.set (highCut.skip (numSamples), fs);
    diffusionCoefficient = 0.75f * diffusion.skip (numSamples);

    // Each line loses 60 dB over the decay time in the mids. A low and a high shelf then scale that
    // loss in their bands, so those decay over decaySeconds times their ratio instead. A shelf can
    // boost (a ratio above 1), but because the ratios stop at 2 the loop gain always stays below 1.
    const auto log60dB = -6.9077553f;
    auto meanLoopSeconds = 0.0f;

    for (auto& line : lines)
    {
        const auto loopSeconds = line.baseSeconds * scale;
        const auto logGain = log60dB * loopSeconds / decaySeconds;

        line.feedback = std::exp (logGain);
        line.lowAmount = std::exp (logGain * lowExcess) - 1.0f;
        line.highAmount = std::exp (logGain * highExcess) - 1.0f;

        // Offsetting the corner by the square root of the shelf's gain puts its midpoint on the chosen frequency
        line.lowCoefficient = OnePole::coefficientFor (lowFrequency * std::exp (-0.5f * logGain * lowExcess), fs);
        line.highCoefficient = OnePole::coefficientFor (highFrequency * std::exp (0.5f * logGain * highExcess), fs);

        meanLoopSeconds += loopSeconds / (float) numLines;
    }

    const auto rearToneGain = juce::Decibels::decibelsToGain (rearTone.skip (numSamples));
    rearToneAmount = rearToneGain - 1.0f;
    rearToneCoefficient = OnePole::coefficientFor (rearToneHz * std::sqrt (rearToneGain), fs);

    // Long tails store far more energy than short ones. This takes back half of that (in dB), so
    // turning up the decay doesn't also turn up the reverb quite so much.
    const auto meanGainSquared = std::exp (2.0f * log60dB * meanLoopSeconds / decaySeconds);
    decayCompensation = std::sqrt (std::sqrt (1.0f - meanGainSquared));
}

void ReverbEngine::process (juce::AudioBuffer<float>& buffer)
{
    const auto numSamples = buffer.getNumSamples();
    const auto numInputs = (size_t) layout.numInputs;
    const auto numOutputs = (size_t) layout.numOutputs;

    jassert (buffer.getNumChannels() >= juce::jmax (layout.numInputs, layout.numOutputs));

    auto* const* channels = buffer.getArrayOfWritePointers();

    std::array<float, maxChannels> dry {};
    std::array<float, maxChannels> wet {};
    std::array<float, numLines> feed {};
    std::array<float, numLines> circulating {};
    std::array<float, 4> gains {};

    for (int start = 0; start < numSamples; start += controlInterval)
    {
        const auto end = juce::jmin (start + controlInterval, numSamples);
        updateControlRate (end - start);

        for (int sample = start; sample < end; ++sample)
        {
            const auto dryLevel = dryGain.getNextValue();
            const auto wetLevel = wetGain.getNextValue() * decayCompensation;
            const auto scale = sizeScale.getNextValue();
            const auto depth = modulationDepth.getNextValue();
            const auto predelay = predelaySamples.getNextValue() + 1.0f;
            const auto rearDelay = rearDelaySamples.getNextValue() + 1.0f;
            const auto lfeSendLevel = lfeSend.getNextValue();
            const auto lfeReturnLevel = lfeReturn.getNextValue();
            auto lfeSum = 0.0f;

            for (size_t role = 0; role < gains.size(); ++role)
                gains[role] = roleGains[role].getNextValue();

            dry.fill (0.0f);
            wet.fill (0.0f);
            feed.fill (0.0f);

            for (size_t i = 0; i < numInputs; ++i)
            {
                auto& input = inputs[i];
                const auto in = channels[i][sample];

                const auto& destinations = layout.dryDestinations[i];
                dry[(size_t) destinations[0]] += in * input.dryGain;

                if (destinations[1] >= 0)
                    dry[(size_t) destinations[1]] += in * input.dryGain;

                // The LFE only drives the reverb as far as its send allows
                const auto send = layout.inputRoles[i] == Role::lfe ? lfeSendLevel : 1.0f;

                input.predelay.push (in * send);
                auto x = input.predelay.readLinear (predelay);
                x = input.lowCut.highpass (x, lowCutCoefficients);
                x = input.highCut.lowpass (x, highCutCoefficients);

                for (auto& diffuser : input.diffusers)
                    x = diffuser.process (x, diffusionCoefficient);

                for (size_t line = 0; line < numLines; ++line)
                    feed[line] += injection[i][line] * x;
            }

            for (size_t i = 0; i < numLines; ++i)
            {
                auto& line = lines[i];

                line.lfoSin += line.lfoRate * line.lfoCos;
                line.lfoCos -= line.lfoRate * line.lfoSin;

                const auto delay = juce::jlimit (4.0f, maxLineDelay, line.baseSamples * scale + depth * line.lfoSin);
                const auto delayed = line.delay.readCubic (delay);

                auto damped = line.feedback * delayed;
                damped += line.lowAmount * line.lowBand.lowpass (damped, line.lowCoefficient);
                damped += line.highAmount * (damped - line.highBand.lowpass (damped, line.highCoefficient));

                circulating[i] = damped;
                wet[(size_t) line.output] += line.tapGain * damped;
                lfeSum += line.lfeTapGain * damped;
            }

            hadamard (circulating);

            for (size_t i = 0; i < numLines; ++i)
                lines[i].delay.push (circulating[i] + feed[i]);

            for (size_t i = 0; i < numOutputs; ++i)
            {
                const auto role = layout.outputRoles[i];

                if (role == Role::lfe)
                {
                    const auto lfeReverb = lfeFilters[i].lowpass (lfeSum, lfeCoefficients);
                    channels[i][sample] = dry[i] * dryLevel + lfeReverb * wetLevel * lfeReturnLevel;
                    continue;
                }

                auto reverb = wet[i];

                if (role == Role::surround)
                {
                    rearDelays[i].push (reverb);
                    reverb = rearDelays[i].readLinear (rearDelay);
                    reverb += rearToneAmount * (reverb - rearTones[i].lowpass (reverb, rearToneCoefficient));
                }

                channels[i][sample] = dry[i] * dryLevel + reverb * wetLevel * gains[(size_t) role];
            }
        }
    }
}
