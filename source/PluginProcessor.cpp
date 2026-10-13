#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr float minLevelDb = -60.0f;

    using Attributes = juce::AudioParameterFloatAttributes;

    juce::NormalisableRange<float> rangeWithCentre (float start, float end, float centre, float interval = 0.0f)
    {
        juce::NormalisableRange<float> range (start, end, interval);
        range.setSkewForCentre (centre);
        return range;
    }

    Attributes withUnit (const juce::String& unit, int decimalPlaces)
    {
        return Attributes()
            .withLabel (unit)
            .withStringFromValueFunction ([unit, decimalPlaces] (float value, int) { return juce::String (value, decimalPlaces) + " " + unit; });
    }

    Attributes frequencyAttributes()
    {
        return Attributes()
            .withLabel ("Hz")
            .withStringFromValueFunction ([] (float value, int) {
                return value < 1000.0f ? juce::String (juce::roundToInt (value)) + " Hz"
                                       : juce::String (value / 1000.0f, 2) + " kHz";
            })
            .withValueFromStringFunction ([] (const juce::String& text) {
                const auto value = text.getFloatValue();
                return text.containsIgnoreCase ("k") ? value * 1000.0f : value;
            });
    }

    // The bottom of the range switches the group off entirely
    Attributes levelAttributes()
    {
        return Attributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float value, int) {
                return value <= minLevelDb ? juce::String ("Off") : juce::String (value, 1) + " dB";
            })
            .withValueFromStringFunction ([] (const juce::String& text) {
                return text.containsIgnoreCase ("off") || text.containsIgnoreCase ("inf") ? minLevelDb : text.getFloatValue();
            });
    }

    std::unique_ptr<juce::AudioParameterFloat> makeParameter (const char* id, const juce::String& name, juce::NormalisableRange<float> range, float defaultValue, const Attributes& attributes)
    {
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, defaultValue, attributes);
    }

    bool isOneOf (juce::AudioChannelSet::ChannelType type, std::initializer_list<juce::AudioChannelSet::ChannelType> candidates)
    {
        return std::find (candidates.begin(), candidates.end(), type) != candidates.end();
    }
}

//==============================================================================
PluginProcessor::PluginProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::create5point1(), true)
                          .withOutput ("Output", juce::AudioChannelSet::create5point1(), true)),
      state (*this, nullptr, "SurroundReverb", createParameterLayout())
{
    values.mix = state.getRawParameterValue (ParameterIDs::mix);
    values.predelay = state.getRawParameterValue (ParameterIDs::predelay);
    values.size = state.getRawParameterValue (ParameterIDs::size);
    values.decay = state.getRawParameterValue (ParameterIDs::decay);
    values.diffusion = state.getRawParameterValue (ParameterIDs::diffusion);
    values.modulation = state.getRawParameterValue (ParameterIDs::modulation);
    values.lowCut = state.getRawParameterValue (ParameterIDs::lowCut);
    values.highCut = state.getRawParameterValue (ParameterIDs::highCut);
    values.lowDampFrequency = state.getRawParameterValue (ParameterIDs::lowDampFrequency);
    values.lowDampRatio = state.getRawParameterValue (ParameterIDs::lowDampRatio);
    values.highDampFrequency = state.getRawParameterValue (ParameterIDs::highDampFrequency);
    values.highDampRatio = state.getRawParameterValue (ParameterIDs::highDampRatio);
    values.frontLevel = state.getRawParameterValue (ParameterIDs::frontLevel);
    values.centreLevel = state.getRawParameterValue (ParameterIDs::centreLevel);
    values.surroundLevel = state.getRawParameterValue (ParameterIDs::surroundLevel);
    values.heightLevel = state.getRawParameterValue (ParameterIDs::heightLevel);
    values.rearDelay = state.getRawParameterValue (ParameterIDs::rearDelay);
    values.rearTone = state.getRawParameterValue (ParameterIDs::rearTone);
    values.lfeSend = state.getRawParameterValue (ParameterIDs::lfeSend);
    values.lfeReturn = state.getRawParameterValue (ParameterIDs::lfeReturn);
}

PluginProcessor::~PluginProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    const juce::NormalisableRange<float> percent (0.0f, 100.0f);
    const auto level = rangeWithCentre (minLevelDb, 6.0f, -12.0f, 0.1f);
    const auto dampRatio = rangeWithCentre (ReverbEngine::minDampRatio, ReverbEngine::maxDampRatio, 1.0f, 0.01f);

    return {
        makeParameter (ParameterIDs::mix, "Mix", percent, 30.0f, withUnit ("%", 0)),
        makeParameter (ParameterIDs::predelay, "Pre-Delay", rangeWithCentre (0.0f, ReverbEngine::maxPredelayMs, 50.0f), 20.0f, withUnit ("ms", 1)),
        makeParameter (ParameterIDs::size, "Size", percent, 50.0f, withUnit ("%", 0)),
        makeParameter (ParameterIDs::decay, "Decay", rangeWithCentre (0.2f, 20.0f, 2.5f), 2.5f, withUnit ("s", 2)),
        makeParameter (ParameterIDs::diffusion, "Diffusion", percent, 75.0f, withUnit ("%", 0)),
        makeParameter (ParameterIDs::modulation, "Modulation", percent, 25.0f, withUnit ("%", 0)),
        makeParameter (ParameterIDs::lowCut, "Low Cut", rangeWithCentre (20.0f, 1000.0f, 150.0f), 20.0f, frequencyAttributes()),
        makeParameter (ParameterIDs::highCut, "High Cut", rangeWithCentre (1000.0f, 20000.0f, 6000.0f), 12000.0f, frequencyAttributes()),
        makeParameter (ParameterIDs::lowDampFrequency, "Low Damp Frequency", rangeWithCentre (20.0f, 1600.0f, 200.0f), 200.0f, frequencyAttributes()),
        makeParameter (ParameterIDs::lowDampRatio, "Low Decay Ratio", dampRatio, 1.1f, withUnit ("x", 2)),
        makeParameter (ParameterIDs::highDampFrequency, "High Damp Frequency", rangeWithCentre (1000.0f, 20000.0f, 5000.0f), 4000.0f, frequencyAttributes()),
        makeParameter (ParameterIDs::highDampRatio, "High Decay Ratio", dampRatio, 0.5f, withUnit ("x", 2)),
        makeParameter (ParameterIDs::frontLevel, "Front Level", level, 0.0f, levelAttributes()),
        makeParameter (ParameterIDs::centreLevel, "Center Level", level, 0.0f, levelAttributes()),
        makeParameter (ParameterIDs::surroundLevel, "Surround Level", level, 0.0f, levelAttributes()),
        makeParameter (ParameterIDs::heightLevel, "Height Level", level, 0.0f, levelAttributes()),
        makeParameter (ParameterIDs::rearDelay, "Rear Delay", juce::NormalisableRange<float> (0.0f, ReverbEngine::maxRearDelayMs), 0.0f, withUnit ("ms", 1)),
        makeParameter (ParameterIDs::rearTone, "Rear Tone", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, withUnit ("dB", 1)),
        makeParameter (ParameterIDs::lfeSend, "LFE Send", level, minLevelDb, levelAttributes()),
        makeParameter (ParameterIDs::lfeReturn, "LFE Return", level, minLevelDb, levelAttributes()),
    };
}

ReverbEngine::Parameters PluginProcessor::readParameters() const
{
    const auto percent = [] (const std::atomic<float>* value) { return value->load (std::memory_order_relaxed) * 0.01f; };
    const auto plain = [] (const std::atomic<float>* value) { return value->load (std::memory_order_relaxed); };
    const auto gain = [] (const std::atomic<float>* value) { return juce::Decibels::decibelsToGain (value->load (std::memory_order_relaxed), minLevelDb); };

    ReverbEngine::Parameters parameters;
    parameters.mix = percent (values.mix);
    parameters.predelayMs = plain (values.predelay);
    parameters.size = percent (values.size);
    parameters.decaySeconds = plain (values.decay);
    parameters.diffusion = percent (values.diffusion);
    parameters.modulation = percent (values.modulation);
    parameters.lowCutHz = plain (values.lowCut);
    parameters.highCutHz = plain (values.highCut);
    parameters.lowDampHz = plain (values.lowDampFrequency);
    parameters.lowDampRatio = plain (values.lowDampRatio);
    parameters.highDampHz = plain (values.highDampFrequency);
    parameters.highDampRatio = plain (values.highDampRatio);
    parameters.rearDelayMs = plain (values.rearDelay);
    parameters.rearToneDb = plain (values.rearTone);
    parameters.lfeSendGain = gain (values.lfeSend);
    parameters.lfeReturnGain = gain (values.lfeReturn);
    parameters.frontGain = gain (values.frontLevel);
    parameters.centreGain = gain (values.centreLevel);
    parameters.surroundGain = gain (values.surroundLevel);
    parameters.heightGain = gain (values.heightLevel);
    return parameters;
}

//==============================================================================
const juce::String PluginProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PluginProcessor::acceptsMidi() const
{
    return false;
}

bool PluginProcessor::producesMidi() const
{
    return false;
}

bool PluginProcessor::isMidiEffect() const
{
    return false;
}

double PluginProcessor::getTailLengthSeconds() const
{
    // The lows can be set to ring for longer than the decay time itself
    const auto longestRatio = juce::jmax (1.0f, values.lowDampRatio->load (std::memory_order_relaxed), values.highDampRatio->load (std::memory_order_relaxed));
    const auto delaysMs = values.predelay->load (std::memory_order_relaxed) + values.rearDelay->load (std::memory_order_relaxed);
    return (double) (values.decay->load (std::memory_order_relaxed) * longestRatio) + (double) delaysMs * 0.001;
}

int PluginProcessor::getNumPrograms()
{
    return 1; // NB: some hosts don't cope very well if you tell them there are 0 programs,
        // so this should be at least 1, even if you're not really implementing programs.
}

int PluginProcessor::getCurrentProgram()
{
    return 0;
}

void PluginProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String PluginProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    // Steinberg's VST3 validator fails plugins whose single default program has no name
    return "Default";
}

void PluginProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
bool PluginProcessor::isLayoutSupported (const juce::AudioChannelSet& input, const juce::AudioChannelSet& output)
{
    // Ambisonic channels aren't speaker feeds, and decorrelating them would wreck the encoding
    if (output.isDisabled() || output.size() > ReverbEngine::maxChannels || output.getAmbisonicOrder() >= 0)
        return false;

    if (input == output)
        return true;

    const auto isNarrowInput = input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo();
    return isNarrowInput && output.size() > input.size() && ! output.isDiscreteLayout();
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return isLayoutSupported (layouts.getMainInputChannelSet(), layouts.getMainOutputChannelSet());
}

ReverbEngine::Role PluginProcessor::getRoleForChannel (juce::AudioChannelSet::ChannelType type)
{
    using Set = juce::AudioChannelSet;

    if (type == Set::centre)
        return ReverbEngine::Role::centre;

    if (isOneOf (type, { Set::LFE, Set::LFE2 }))
        return ReverbEngine::Role::lfe;

    if (isOneOf (type, { Set::leftSurround, Set::rightSurround, Set::centreSurround, Set::leftSurroundSide, Set::rightSurroundSide, Set::leftSurroundRear, Set::rightSurroundRear }))
        return ReverbEngine::Role::surround;

    if (isOneOf (type, { Set::topMiddle, Set::topFrontLeft, Set::topFrontCentre, Set::topFrontRight, Set::topRearLeft, Set::topRearCentre, Set::topRearRight, Set::topSideLeft, Set::topSideRight }))
        return ReverbEngine::Role::height;

    return ReverbEngine::Role::front;
}

ReverbEngine::Layout PluginProcessor::makeEngineLayout (const juce::AudioChannelSet& input, const juce::AudioChannelSet& output)
{
    auto layout = ReverbEngine::Layout::matched (output.size());
    layout.numInputs = input.size();

    // A lone centre channel is just mono, so it shouldn't answer to the center level control
    if (output.size() > 1)
        for (int channel = 0; channel < output.size(); ++channel)
            layout.outputRoles[(size_t) channel] = getRoleForChannel (output.getTypeOfChannel (channel));

    if (input.size() > 1)
        for (int channel = 0; channel < input.size(); ++channel)
            layout.inputRoles[(size_t) channel] = getRoleForChannel (input.getTypeOfChannel (channel));

    if (input == output)
        return layout;

    // The dry signal goes to the matching speaker in the wider output layout
    for (int channel = 0; channel < input.size(); ++channel)
    {
        const auto type = input.getTypeOfChannel (channel);
        const auto left = output.getChannelIndexForType (juce::AudioChannelSet::left);
        const auto right = output.getChannelIndexForType (juce::AudioChannelSet::right);
        auto& destinations = layout.dryDestinations[(size_t) channel];

        if (const auto match = output.getChannelIndexForType (type); match >= 0)
            destinations = { match, -1 };
        else if (type == juce::AudioChannelSet::centre && left >= 0 && right >= 0)
            destinations = { left, right }; // mono into a layout with no centre speaker
        else
            destinations = { channel, -1 };
    }

    return layout;
}

void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);

    const auto layout = getBusesLayout();
    engine.setParameters (readParameters());
    engine.prepare (sampleRate, makeEngineLayout (layout.getMainInputChannelSet(), layout.getMainOutputChannelSet()));
}

void PluginProcessor::releaseResources()
{
}

void PluginProcessor::reset()
{
    engine.reset();
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    engine.setParameters (readParameters());
    engine.process (buffer);

    const auto numOutputs = juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels(), (int) outputPeaks.size());

    for (int channel = 0; channel < numOutputs; ++channel)
    {
        auto& peak = outputPeaks[(size_t) channel];
        const auto magnitude = buffer.getMagnitude (channel, 0, buffer.getNumSamples());
        peak.store (juce::jmax (peak.load (std::memory_order_relaxed), magnitude), std::memory_order_relaxed);
    }
}

float PluginProcessor::readOutputPeak (int channel)
{
    if (! juce::isPositiveAndBelow (channel, (int) outputPeaks.size()))
        return 0.0f;

    return outputPeaks[(size_t) channel].exchange (0.0f, std::memory_order_relaxed);
}

//==============================================================================
bool PluginProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

//==============================================================================
void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (const auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
