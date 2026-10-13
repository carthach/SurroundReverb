#pragma once

#include "ReverbEngine.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace ParameterIDs
{
    inline constexpr auto mix = "mix";
    inline constexpr auto predelay = "predelay";
    inline constexpr auto size = "size";
    inline constexpr auto decay = "decay";
    inline constexpr auto diffusion = "diffusion";
    inline constexpr auto modulation = "modulation";
    inline constexpr auto lowCut = "lowCut";
    inline constexpr auto highCut = "highCut";
    inline constexpr auto lowDampFrequency = "lowDampFrequency";
    inline constexpr auto lowDampRatio = "lowDampRatio";
    inline constexpr auto highDampFrequency = "highDampFrequency";
    inline constexpr auto highDampRatio = "highDampRatio";
    inline constexpr auto frontLevel = "frontLevel";
    inline constexpr auto centreLevel = "centreLevel";
    inline constexpr auto surroundLevel = "surroundLevel";
    inline constexpr auto heightLevel = "heightLevel";
    inline constexpr auto rearDelay = "rearDelay";
    inline constexpr auto rearTone = "rearTone";
    inline constexpr auto lfeSend = "lfeSend";
    inline constexpr auto lfeReturn = "lfeReturn";
}

class PluginProcessor : public juce::AudioProcessor
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Any speaker layout of up to 16 channels in and out, or a mono/stereo input into a wider output
    static bool isLayoutSupported (const juce::AudioChannelSet& input, const juce::AudioChannelSet& output);

    static ReverbEngine::Role getRoleForChannel (juce::AudioChannelSet::ChannelType);
    static ReverbEngine::Layout makeEngineLayout (const juce::AudioChannelSet& input, const juce::AudioChannelSet& output);

    // Returns the peak output level of a channel since this was last called. For metering on the message thread.
    float readOutputPeak (int channel);

    juce::AudioProcessorValueTreeState state;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    ReverbEngine::Parameters readParameters() const;

    ReverbEngine engine;

    struct ParameterValues
    {
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* predelay = nullptr;
        std::atomic<float>* size = nullptr;
        std::atomic<float>* decay = nullptr;
        std::atomic<float>* diffusion = nullptr;
        std::atomic<float>* modulation = nullptr;
        std::atomic<float>* lowCut = nullptr;
        std::atomic<float>* highCut = nullptr;
        std::atomic<float>* lowDampFrequency = nullptr;
        std::atomic<float>* lowDampRatio = nullptr;
        std::atomic<float>* highDampFrequency = nullptr;
        std::atomic<float>* highDampRatio = nullptr;
        std::atomic<float>* frontLevel = nullptr;
        std::atomic<float>* centreLevel = nullptr;
        std::atomic<float>* surroundLevel = nullptr;
        std::atomic<float>* heightLevel = nullptr;
        std::atomic<float>* rearDelay = nullptr;
        std::atomic<float>* rearTone = nullptr;
        std::atomic<float>* lfeSend = nullptr;
        std::atomic<float>* lfeReturn = nullptr;
    };

    ParameterValues values;
    std::array<std::atomic<float>, ReverbEngine::maxChannels> outputPeaks {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
