#include "helpers/test_helpers.h"
#include <PluginProcessor.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

TEST_CASE ("Plugin instance", "[instance]")
{
    PluginProcessor testPlugin;

    SECTION ("name")
    {
        CHECK_THAT (testPlugin.getName().toStdString(),
            Catch::Matchers::Equals ("Surround Reverb"));
    }

    SECTION ("program name")
    {
        // Steinberg's VST3 validator fails plugins whose programs have no name
        CHECK (testPlugin.getProgramName (0).isNotEmpty());
    }

    SECTION ("defaults to 5.1")
    {
        CHECK (testPlugin.getBusesLayout().getMainInputChannelSet() == juce::AudioChannelSet::create5point1());
        CHECK (testPlugin.getBusesLayout().getMainOutputChannelSet() == juce::AudioChannelSet::create5point1());
    }
}

TEST_CASE ("Supported layouts", "[layouts]")
{
    using Set = juce::AudioChannelSet;

    SECTION ("matching speaker layouts")
    {
        for (const auto& set : { Set::mono(), Set::stereo(), Set::createLCR(), Set::quadraphonic(), Set::create5point0(), Set::create5point1(), Set::create7point1(), Set::create7point1point2(), Set::create7point1point4(), Set::create9point1point6() })
        {
            INFO (set.getDescription());
            CHECK (PluginProcessor::isLayoutSupported (set, set));
        }
    }

    SECTION ("mono and stereo can feed a wider output")
    {
        CHECK (PluginProcessor::isLayoutSupported (Set::mono(), Set::stereo()));
        CHECK (PluginProcessor::isLayoutSupported (Set::stereo(), Set::create5point1()));
        CHECK (PluginProcessor::isLayoutSupported (Set::stereo(), Set::create7point1point4()));
    }

    SECTION ("rejected layouts")
    {
        CHECK_FALSE (PluginProcessor::isLayoutSupported (Set::create5point1(), Set::stereo()));
        CHECK_FALSE (PluginProcessor::isLayoutSupported (Set::create5point1(), Set::create7point1()));
        CHECK_FALSE (PluginProcessor::isLayoutSupported (Set::stereo(), Set::disabled()));
        CHECK_FALSE (PluginProcessor::isLayoutSupported (Set::ambisonic (1), Set::ambisonic (1)));
        CHECK_FALSE (PluginProcessor::isLayoutSupported (Set::discreteChannels (32), Set::discreteChannels (32)));
    }

    SECTION ("the processor accepts what isLayoutSupported accepts")
    {
        PluginProcessor plugin;
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (Set::stereo());
        layout.outputBuses.add (Set::create7point1());
        CHECK (plugin.setBusesLayout (layout));
        CHECK (plugin.getTotalNumInputChannels() == 2);
        CHECK (plugin.getTotalNumOutputChannels() == 8);
    }
}

TEST_CASE ("Engine layouts", "[layouts]")
{
    using Set = juce::AudioChannelSet;
    using Role = ReverbEngine::Role;

    SECTION ("5.1 speaker roles")
    {
        const auto layout = PluginProcessor::makeEngineLayout (Set::create5point1(), Set::create5point1());

        // JUCE orders 5.1 as L R C LFE Ls Rs
        CHECK (layout.outputRoles[0] == Role::front);
        CHECK (layout.outputRoles[1] == Role::front);
        CHECK (layout.outputRoles[2] == Role::centre);
        CHECK (layout.outputRoles[3] == Role::lfe);
        CHECK (layout.outputRoles[4] == Role::surround);
        CHECK (layout.outputRoles[5] == Role::surround);
        CHECK (layout.inputRoles[3] == Role::lfe);
    }

    SECTION ("7.1.4 has height speakers")
    {
        const auto set = Set::create7point1point4();
        const auto layout = PluginProcessor::makeEngineLayout (set, set);
        auto numHeights = 0;

        for (int channel = 0; channel < set.size(); ++channel)
            numHeights += layout.outputRoles[(size_t) channel] == Role::height ? 1 : 0;

        CHECK (numHeights == 4);
    }

    SECTION ("mono is a front channel, not a centre speaker")
    {
        CHECK (PluginProcessor::makeEngineLayout (Set::mono(), Set::mono()).outputRoles[0] == Role::front);
    }

    SECTION ("mono's dry signal goes to the centre speaker when there is one")
    {
        const auto layout = PluginProcessor::makeEngineLayout (Set::mono(), Set::create5point1());
        CHECK (layout.numInputs == 1);
        CHECK (layout.numOutputs == 6);
        CHECK (layout.dryDestinations[0] == std::array<int, 2> { 2, -1 });
    }

    SECTION ("mono's dry signal is split between left and right otherwise")
    {
        const auto layout = PluginProcessor::makeEngineLayout (Set::mono(), Set::quadraphonic());
        CHECK (layout.dryDestinations[0] == std::array<int, 2> { 0, 1 });
    }
}

TEST_CASE ("Processing", "[processing]")
{
    PluginProcessor plugin;
    juce::MidiBuffer midi;
    juce::Random random (1234);

    const auto fillWithNoise = [&random] (juce::AudioBuffer<float>& buffer) {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                buffer.setSample (channel, sample, random.nextFloat() - 0.5f);
    };

    SECTION ("5.1 with awkward block sizes stays finite and bounded")
    {
        plugin.prepareToPlay (48000.0, 512);

        for (const auto blockSize : { 512, 1, 7, 33, 512, 64 })
        {
            juce::AudioBuffer<float> buffer (6, blockSize);
            fillWithNoise (buffer);
            plugin.processBlock (buffer, midi);

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                const auto magnitude = buffer.getMagnitude (channel, 0, blockSize);
                CHECK (std::isfinite (magnitude));
                CHECK (magnitude < 4.0f);
            }
        }
    }

    SECTION ("stereo into 5.1 fills the surrounds")
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::stereo());
        layout.outputBuses.add (juce::AudioChannelSet::create5point1());
        REQUIRE (plugin.setBusesLayout (layout));
        plugin.prepareToPlay (48000.0, 512);

        auto surroundEnergy = 0.0f;

        for (int block = 0; block < 40; ++block)
        {
            juce::AudioBuffer<float> buffer (6, 512);
            buffer.clear();

            // Only the two input channels carry signal
            for (int sample = 0; sample < 512; ++sample)
            {
                buffer.setSample (0, sample, random.nextFloat() - 0.5f);
                buffer.setSample (1, sample, random.nextFloat() - 0.5f);
            }

            plugin.processBlock (buffer, midi);
            surroundEnergy += buffer.getRMSLevel (4, 0, 512) + buffer.getRMSLevel (5, 0, 512);

            // Nothing was sent to the LFE, so nothing should come out of it
            CHECK (buffer.getMagnitude (3, 0, 512) == 0.0f);
        }

        CHECK (surroundEnergy > 0.1f);
    }

    SECTION ("output meters report and then clear")
    {
        plugin.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> buffer (6, 512);
        fillWithNoise (buffer);
        plugin.processBlock (buffer, midi);

        CHECK (plugin.readOutputPeak (0) > 0.0f);
        CHECK (plugin.readOutputPeak (0) == 0.0f);
        CHECK (plugin.readOutputPeak (99) == 0.0f);
    }
}

TEST_CASE ("State", "[state]")
{
    PluginProcessor saved;
    auto* decay = saved.state.getParameter (ParameterIDs::decay);
    auto* centre = saved.state.getParameter (ParameterIDs::centreLevel);
    REQUIRE (decay != nullptr);
    REQUIRE (centre != nullptr);

    decay->setValueNotifyingHost (decay->convertTo0to1 (7.5f));
    centre->setValueNotifyingHost (centre->convertTo0to1 (-18.0f));

    juce::MemoryBlock block;
    saved.getStateInformation (block);

    PluginProcessor restored;
    restored.setStateInformation (block.getData(), (int) block.getSize());

    CHECK_THAT (restored.state.getRawParameterValue (ParameterIDs::decay)->load(), Catch::Matchers::WithinAbs (7.5, 0.01));
    CHECK_THAT (restored.state.getRawParameterValue (ParameterIDs::centreLevel)->load(), Catch::Matchers::WithinAbs (-18.0, 0.11));

    SECTION ("garbage is ignored")
    {
        const char garbage[] = "not a preset";
        restored.setStateInformation (garbage, (int) sizeof (garbage));
        CHECK_THAT (restored.state.getRawParameterValue (ParameterIDs::decay)->load(), Catch::Matchers::WithinAbs (7.5, 0.01));
    }
}

TEST_CASE ("Editor", "[editor]")
{
    runWithinPluginEditor ([] (PluginProcessor& plugin) {
        auto* editor = plugin.getActiveEditor();
        REQUIRE (editor != nullptr);
        CHECK (editor->getWidth() > 0);
        CHECK (editor->getHeight() > 0);
    });
}
