#include <ReverbEngine.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

namespace
{
    constexpr double sampleRate = 48000.0;

    // L R C LFE Ls Rs
    ReverbEngine::Layout make5point1()
    {
        using Role = ReverbEngine::Role;
        auto layout = ReverbEngine::Layout::matched (6);
        layout.outputRoles = { Role::front, Role::front, Role::centre, Role::lfe, Role::surround, Role::surround };
        layout.inputRoles = layout.outputRoles;
        return layout;
    }

    ReverbEngine::Parameters wetOnly()
    {
        ReverbEngine::Parameters parameters;
        parameters.mix = 1.0f;
        parameters.predelayMs = 0.0f;
        parameters.modulation = 0.0f;
        parameters.lowDampRatio = 1.0f;
        parameters.highDampRatio = 1.0f;
        parameters.highCutHz = 20000.0f;
        return parameters;
    }

    juce::AudioBuffer<float> impulse (int numChannels, int numSamples, int channel)
    {
        juce::AudioBuffer<float> buffer (numChannels, numSamples);
        buffer.clear();
        buffer.setSample (channel, 0, 1.0f);
        return buffer;
    }

    double energy (const juce::AudioBuffer<float>& buffer, int channel, double startSeconds, double endSeconds)
    {
        const auto start = juce::roundToInt (startSeconds * sampleRate);
        const auto end = juce::jmin (buffer.getNumSamples(), juce::roundToInt (endSeconds * sampleRate));
        auto sum = 0.0;

        for (int sample = start; sample < end; ++sample)
            sum += juce::square ((double) buffer.getSample (channel, sample));

        return sum;
    }

    double correlation (const juce::AudioBuffer<float>& buffer, int a, int b)
    {
        auto ab = 0.0, aa = 0.0, bb = 0.0;

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto x = (double) buffer.getSample (a, sample);
            const auto y = (double) buffer.getSample (b, sample);
            ab += x * y;
            aa += x * x;
            bb += y * y;
        }

        return ab / std::sqrt (aa * bb);
    }

    bool channelsAreIdentical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int channel)
    {
        for (int sample = 0; sample < a.getNumSamples(); ++sample)
            if (! juce::exactlyEqual (a.getSample (channel, sample), b.getSample (channel, sample)))
                return false;

        return a.getNumSamples() == b.getNumSamples();
    }

    void fillWithNoise (juce::AudioBuffer<float>& buffer, juce::Random& random)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                buffer.setSample (channel, sample, random.nextFloat() * 2.0f - 1.0f);
    }
}

TEST_CASE ("Silence in, silence out", "[engine]")
{
    ReverbEngine engine;
    engine.prepare (sampleRate, make5point1());

    juce::AudioBuffer<float> buffer (6, 4096);
    buffer.clear();
    engine.process (buffer);

    CHECK (buffer.getMagnitude (0, 4096) == 0.0f);
}

TEST_CASE ("A fully dry mix leaves the signal untouched", "[engine]")
{
    ReverbEngine engine;
    ReverbEngine::Parameters parameters;
    parameters.mix = 0.0f;
    engine.setParameters (parameters);
    engine.prepare (sampleRate, make5point1());

    juce::Random random (1);
    juce::AudioBuffer<float> buffer (6, 2048);
    fillWithNoise (buffer, random);
    const auto original = buffer;
    engine.process (buffer);

    for (int channel = 0; channel < 6; ++channel)
    {
        INFO ("channel " << channel);
        CHECK (channelsAreIdentical (buffer, original, channel));
    }
}

TEST_CASE ("One channel in reverberates every speaker", "[engine]")
{
    ReverbEngine engine;
    engine.setParameters (wetOnly());
    engine.prepare (sampleRate, make5point1());

    auto buffer = impulse (6, 48000, 0);
    engine.process (buffer);

    std::array<double, 6> energies {};

    for (size_t channel = 0; channel < 6; ++channel)
        energies[channel] = energy (buffer, (int) channel, 0.0, 1.0);

    SECTION ("the LFE gets no reverb")
    {
        CHECK (energies[3] == 0.0);
    }

    SECTION ("the speakers get roughly equal shares")
    {
        for (const auto channel : { 0, 1, 2, 4, 5 })
        {
            INFO ("channel " << channel);
            CHECK (energies[(size_t) channel] > 0.0);
            CHECK (energies[(size_t) channel] / energies[0] > 0.5);
            CHECK (energies[(size_t) channel] / energies[0] < 2.0);
        }
    }

    SECTION ("the speakers are decorrelated from each other")
    {
        for (const auto a : { 0, 1, 2, 4, 5 })
        {
            for (const auto b : { 0, 1, 2, 4, 5 })
            {
                if (a < b)
                {
                    INFO ("channels " << a << " and " << b);
                    CHECK (std::abs (correlation (buffer, a, b)) < 0.2);
                }
            }
        }
    }
}

TEST_CASE ("LFE", "[engine]")
{
    ReverbEngine engine;
    auto parameters = wetOnly();
    juce::Random random (2);

    // Full scale noise in the LFE only, or an impulse in the left channel only
    const auto processLfeNoise = [&] {
        engine.setParameters (parameters);
        engine.prepare (sampleRate, make5point1());

        juce::AudioBuffer<float> buffer (6, 24000);
        buffer.clear();

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            buffer.setSample (3, sample, random.nextFloat() * 2.0f - 1.0f);

        engine.process (buffer);
        return buffer;
    };

    const auto processLeftImpulse = [&] {
        engine.setParameters (parameters);
        engine.prepare (sampleRate, make5point1());

        auto buffer = impulse (6, 24000, 0);
        engine.process (buffer);
        return buffer;
    };

    SECTION ("by default the LFE doesn't drive the reverb")
    {
        const auto buffer = processLfeNoise();

        for (int channel = 0; channel < 6; ++channel)
            CHECK (buffer.getMagnitude (channel, 0, buffer.getNumSamples()) == 0.0f);
    }

    SECTION ("the send lets it drive the speakers' reverb")
    {
        parameters.lfeSendGain = 1.0f;
        const auto buffer = processLfeNoise();

        CHECK (energy (buffer, 0, 0.0, 0.5) > 0.0);
        CHECK (energy (buffer, 4, 0.0, 0.5) > 0.0);
        CHECK (energy (buffer, 3, 0.0, 0.5) == 0.0);
    }

    SECTION ("by default no reverb comes back in the LFE")
    {
        CHECK (energy (processLeftImpulse(), 3, 0.0, 0.5) == 0.0);
    }

    SECTION ("the return brings back only the low end")
    {
        parameters.lfeReturnGain = 1.0f;
        const auto buffer = processLeftImpulse();

        // The network was hit with a full band impulse, so the 120 Hz lowpass leaves the LFE far quieter than a speaker
        CHECK (energy (buffer, 3, 0.0, 0.5) > 0.0);
        CHECK (energy (buffer, 3, 0.0, 0.5) < 0.05 * energy (buffer, 0, 0.0, 0.5));
    }

    SECTION ("the dry LFE follows the mix like every other channel")
    {
        parameters.mix = 0.5f;
        engine.setParameters (parameters);
        engine.prepare (sampleRate, make5point1());

        juce::AudioBuffer<float> buffer (6, 512);
        buffer.clear();
        buffer.setSample (3, 10, 1.0f);
        engine.process (buffer);

        CHECK_THAT (buffer.getSample (3, 10), Catch::Matchers::WithinAbs (std::sqrt (0.5), 1.0e-6));
    }
}

TEST_CASE ("Decay sets the RT60", "[engine]")
{
    for (const auto decaySeconds : { 1.0f, 3.0f })
    {
        ReverbEngine engine;
        auto parameters = wetOnly();
        parameters.decaySeconds = decaySeconds;
        parameters.lowCutHz = 20.0f;
        engine.setParameters (parameters);
        engine.prepare (sampleRate, ReverbEngine::Layout::matched (2));

        auto buffer = impulse (2, juce::roundToInt (sampleRate * 2.0), 0);
        engine.process (buffer);

        // An ideal tail falls by 60 dB per decay time, so by 60 / decaySeconds over one second
        const auto early = energy (buffer, 0, 0.3, 0.5) + energy (buffer, 1, 0.3, 0.5);
        const auto late = energy (buffer, 0, 1.3, 1.5) + energy (buffer, 1, 1.3, 1.5);
        const auto dropDb = 10.0 * std::log10 (early / late);

        INFO ("decay " << decaySeconds << " s");
        CHECK_THAT (dropDb, Catch::Matchers::WithinRel (60.0 / (double) decaySeconds, 0.15));
    }
}

TEST_CASE ("Damping sets the decay time of the lows and highs", "[engine]")
{
    // Returns how far one band of the tail falls over a second, in dB
    const auto bandDropDb = [] (ReverbEngine::Parameters parameters, bool measureHighs) {
        parameters.decaySeconds = 2.0f;
        parameters.lowDampHz = 400.0f;
        parameters.highDampHz = 2000.0f;

        ReverbEngine engine;
        engine.setParameters (parameters);
        engine.prepare (sampleRate, ReverbEngine::Layout::matched (2));

        auto buffer = impulse (2, juce::roundToInt (sampleRate * 2.0), 0);
        engine.process (buffer);

        // Isolate the band well away from the shelf's corner
        juce::IIRFilter filter;
        filter.setCoefficients (measureHighs ? juce::IIRCoefficients::makeHighPass (sampleRate, 9000.0)
                                             : juce::IIRCoefficients::makeLowPass (sampleRate, 80.0));
        filter.processSamples (buffer.getWritePointer (0), buffer.getNumSamples());

        return 10.0 * std::log10 (energy (buffer, 0, 0.4, 0.7) / energy (buffer, 0, 1.4, 1.7));
    };

    // With a 2 second decay the mids fall 30 dB per second, and a band at ratio r falls 30 / r
    SECTION ("lows that ring twice as long")
    {
        auto parameters = wetOnly();
        parameters.lowDampRatio = 2.0f;
        CHECK_THAT (bandDropDb (parameters, false), Catch::Matchers::WithinRel (15.0, 0.2));
    }

    SECTION ("lows that die twice as fast")
    {
        auto parameters = wetOnly();
        parameters.lowDampRatio = 0.5f;
        CHECK_THAT (bandDropDb (parameters, false), Catch::Matchers::WithinRel (60.0, 0.2));
    }

    SECTION ("highs that die twice as fast")
    {
        auto parameters = wetOnly();
        parameters.highDampRatio = 0.5f;
        CHECK_THAT (bandDropDb (parameters, true), Catch::Matchers::WithinRel (60.0, 0.2));
    }

    SECTION ("ratios of 1 leave every band at the decay time")
    {
        CHECK_THAT (bandDropDb (wetOnly(), false), Catch::Matchers::WithinRel (30.0, 0.2));
    }
}

TEST_CASE ("The longest decay stays stable", "[engine]")
{
    ReverbEngine engine;
    auto parameters = wetOnly();
    parameters.decaySeconds = 20.0f;
    parameters.size = 1.0f;
    parameters.modulation = 1.0f;

    // The worst case for the damping shelves: both boosting as hard as they can, with overlapping bands
    parameters.lowDampRatio = ReverbEngine::maxDampRatio;
    parameters.highDampRatio = ReverbEngine::maxDampRatio;
    parameters.lowDampHz = 1600.0f;
    parameters.highDampHz = 1000.0f;
    parameters.lfeSendGain = 1.0f;
    parameters.lfeReturnGain = 1.0f;
    engine.setParameters (parameters);
    engine.prepare (sampleRate, make5point1());

    // Charge the network with full scale noise on every channel
    juce::Random random (3);
    juce::AudioBuffer<float> buffer (6, 48000);
    fillWithNoise (buffer, random);
    engine.process (buffer);

    // ...then let it ring. The tail must only ever get quieter.
    auto previous = std::numeric_limits<double>::max();

    for (int second = 0; second < 6; ++second)
    {
        buffer.clear();
        engine.process (buffer);

        auto total = 0.0;

        for (int channel = 0; channel < 6; ++channel)
            total += energy (buffer, channel, 0.0, 1.0);

        INFO ("second " << second);
        REQUIRE (std::isfinite (total));
        CHECK (total > 0.0);
        CHECK (total < previous);
        previous = total;
    }
}

TEST_CASE ("Speaker group levels", "[engine]")
{
    auto parameters = wetOnly();

    SECTION ("a group at zero is silent while the others still ring")
    {
        parameters.centreGain = 0.0f;
        parameters.surroundGain = 0.0f;

        ReverbEngine engine;
        engine.setParameters (parameters);
        engine.prepare (sampleRate, make5point1());

        auto buffer = impulse (6, 24000, 0);
        engine.process (buffer);

        CHECK (energy (buffer, 0, 0.0, 0.5) > 0.0);
        CHECK (energy (buffer, 2, 0.0, 0.5) == 0.0);
        CHECK (energy (buffer, 4, 0.0, 0.5) == 0.0);
        CHECK (energy (buffer, 5, 0.0, 0.5) == 0.0);
    }

    SECTION ("rear tone darkens the surrounds only")
    {
        const auto energies = [&parameters] (float rearToneDb) {
            parameters.rearToneDb = rearToneDb;

            ReverbEngine engine;
            engine.setParameters (parameters);
            engine.prepare (sampleRate, make5point1());

            auto buffer = impulse (6, 24000, 0);
            engine.process (buffer);
            return std::pair { energy (buffer, 0, 0.0, 0.5), energy (buffer, 4, 0.0, 0.5) };
        };

        const auto flat = energies (0.0f);
        const auto dark = energies (-12.0f);

        CHECK_THAT (dark.first, Catch::Matchers::WithinRel (flat.first, 1.0e-6));
        CHECK (dark.second < 0.6 * flat.second);
    }

    SECTION ("rear delay holds back the surrounds only")
    {
        parameters.rearDelayMs = 100.0f;

        ReverbEngine engine;
        engine.setParameters (parameters);
        engine.prepare (sampleRate, make5point1());

        auto buffer = impulse (6, 24000, 0);
        engine.process (buffer);

        CHECK (energy (buffer, 0, 0.0, 0.1) > 0.0);
        CHECK (energy (buffer, 4, 0.0, 0.1) == 0.0);
        CHECK (energy (buffer, 4, 0.1, 0.5) > 0.0);
    }
}

TEST_CASE ("Pre-delay holds back the reverb", "[engine]")
{
    ReverbEngine engine;
    auto parameters = wetOnly();
    parameters.predelayMs = 200.0f;
    engine.setParameters (parameters);
    engine.prepare (sampleRate, ReverbEngine::Layout::matched (2));

    auto buffer = impulse (2, 24000, 0);
    engine.process (buffer);

    CHECK (energy (buffer, 0, 0.0, 0.2) == 0.0);
    CHECK (energy (buffer, 0, 0.2, 0.5) > 0.0);
}

TEST_CASE ("Narrow inputs into wide outputs", "[engine]")
{
    // Mono into quad, with the dry signal split between left and right
    auto layout = ReverbEngine::Layout::matched (4);
    layout.numInputs = 1;
    layout.dryDestinations[0] = { 0, 1 };

    ReverbEngine engine;
    ReverbEngine::Parameters parameters;
    parameters.mix = 0.0f;
    engine.setParameters (parameters);
    engine.prepare (sampleRate, layout);

    juce::AudioBuffer<float> buffer (4, 512);
    buffer.clear();

    for (int sample = 0; sample < 512; ++sample)
        buffer.setSample (0, sample, 1.0f);

    // Junk in the channels that aren't inputs must not leak through
    for (int sample = 0; sample < 512; ++sample)
        buffer.setSample (2, sample, 0.9f);

    engine.process (buffer);

    CHECK_THAT (buffer.getSample (0, 100), Catch::Matchers::WithinAbs (std::sqrt (0.5), 1.0e-6));
    CHECK_THAT (buffer.getSample (1, 100), Catch::Matchers::WithinAbs (std::sqrt (0.5), 1.0e-6));
    CHECK (buffer.getMagnitude (2, 0, 512) == 0.0f);
    CHECK (buffer.getMagnitude (3, 0, 512) == 0.0f);
}

TEST_CASE ("Reset clears the tail", "[engine]")
{
    ReverbEngine engine;
    engine.setParameters (wetOnly());
    engine.prepare (sampleRate, make5point1());

    juce::Random random (4);
    juce::AudioBuffer<float> buffer (6, 8192);
    fillWithNoise (buffer, random);
    engine.process (buffer);

    engine.reset();
    buffer.clear();
    engine.process (buffer);

    CHECK (buffer.getMagnitude (0, 8192) == 0.0f);
}

TEST_CASE ("Every channel count runs", "[engine]")
{
    juce::Random random (5);

    for (int numChannels = 1; numChannels <= ReverbEngine::maxChannels; ++numChannels)
    {
        ReverbEngine engine;
        engine.setParameters (wetOnly());
        engine.prepare (sampleRate, ReverbEngine::Layout::matched (numChannels));

        juce::AudioBuffer<float> buffer (numChannels, 12000);
        fillWithNoise (buffer, random);
        engine.process (buffer);

        INFO (numChannels << " channels");

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto rms = buffer.getRMSLevel (channel, 6000, 6000);
            CHECK (std::isfinite (rms));
            CHECK (rms > 0.01f);
            CHECK (rms < 4.0f);
        }
    }
}
