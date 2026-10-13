#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

/** A multichannel feedback delay network reverb.

    Every input channel excites one shared 16 line network, and every speaker reads its own
    disjoint set of delay lines, so the tail is decorrelated across all of the outputs.

    LFE channels are kept out of the reverb unless asked for: the LFE send and return both default
    to off, which leaves the LFE as just its dry signal.

    Nothing in here allocates after prepare(), so setParameters() and process() are safe to call
    on the audio thread.
*/
class ReverbEngine
{
public:
    static constexpr int maxChannels = 16;
    static constexpr int numLines = 16;

    // Which level control an output channel answers to
    enum class Role
    {
        front,
        centre,
        surround,
        height,
        lfe
    };

    struct Layout
    {
        int numInputs = 2;
        int numOutputs = 2;
        std::array<Role, maxChannels> inputRoles {};
        std::array<Role, maxChannels> outputRoles {};

        // The output(s) each input's dry signal is sent to.
        // The second entry is -1 unless the input is split equally (-3 dB) across two outputs.
        std::array<std::array<int, 2>, maxChannels> dryDestinations {};

        // Same channels in and out, with every channel treated as a front speaker
        static Layout matched (int numChannels);
    };

    struct Parameters
    {
        float mix = 0.3f; // 0..1
        float predelayMs = 20.0f;
        float size = 0.5f; // 0..1
        float decaySeconds = 2.5f; // RT60
        float diffusion = 0.75f; // 0..1
        float modulation = 0.25f; // 0..1
        float lowCutHz = 20.0f;
        float highCutHz = 12000.0f;

        // The decay time below lowDampHz and above highDampHz, as a multiple of decaySeconds
        float lowDampHz = 200.0f;
        float lowDampRatio = 1.1f;
        float highDampHz = 4000.0f;
        float highDampRatio = 0.5f;

        float rearDelayMs = 0.0f;
        float rearToneDb = 0.0f; // high shelf on the surround speakers' reverb
        float frontGain = 1.0f; // linear wet gains
        float centreGain = 1.0f;
        float surroundGain = 1.0f;
        float heightGain = 1.0f;
        float lfeSendGain = 0.0f; // how much the LFE input drives the reverb
        float lfeReturnGain = 0.0f; // how much (lowpassed) reverb comes back in the LFE
    };

    static constexpr float maxPredelayMs = 250.0f;
    static constexpr float maxRearDelayMs = 100.0f;
    static constexpr float minDampRatio = 0.1f;
    static constexpr float maxDampRatio = 2.0f;

    // Allocates. Call from prepareToPlay, never while rendering.
    void prepare (double sampleRate, const Layout& newLayout);

    // Clears the tail and snaps all smoothed parameters to their targets
    void reset();

    void setParameters (const Parameters& newParameters);

    // The buffer needs at least max (numInputs, numOutputs) channels. Inputs are read from the
    // first numInputs channels, and outputs are written to the first numOutputs channels.
    void process (juce::AudioBuffer<float>& buffer);

private:
    struct DelayLine
    {
        void resize (int maxDelaySamples);
        void clear();
        void push (float sample);

        // A delay of 1 reads the most recently pushed sample
        float read (int delay) const;
        float readLinear (float delay) const;
        float readCubic (float delay) const;

        std::vector<float> data;
        int mask = 0;
        int writePosition = 0;
    };

    struct Allpass
    {
        float process (float input, float coefficient);

        DelayLine delay;
        int length = 1;
    };

    struct FilterCoefficients
    {
        void set (float frequency, float sampleRate);

        float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 0.0f;
    };

    // Two pole state variable filter, we only need its lowpass and highpass outputs
    struct Filter
    {
        float lowpass (float input, const FilterCoefficients&);
        float highpass (float input, const FilterCoefficients&);
        void reset();

        float tick (float input, const FilterCoefficients&, float& band);

        float ic1 = 0.0f, ic2 = 0.0f;
    };

    // One pole lowpass, the building block of the first order shelves
    struct OnePole
    {
        static float coefficientFor (float frequency, float sampleRate);
        float lowpass (float input, float coefficient);

        float state = 0.0f;
    };

    static constexpr int numDiffusers = 3;

    struct InputChannel
    {
        DelayLine predelay;
        Filter lowCut, highCut;
        std::array<Allpass, numDiffusers> diffusers;
        float dryGain = 1.0f;
    };

    struct Line
    {
        DelayLine delay;
        float baseSamples = 0.0f; // length at a size scale of 1
        float baseSeconds = 0.0f;
        float feedback = 0.0f;

        // Low and high shelves that make the decay time depend on frequency
        OnePole lowBand, highBand;
        float lowCoefficient = 0.0f, lowAmount = 0.0f;
        float highCoefficient = 0.0f, highAmount = 0.0f;

        float lfoSin = 0.0f, lfoCos = 1.0f, lfoRate = 0.0f;
        float tapGain = 0.0f;
        float lfeTapGain = 0.0f;
        int output = 0;
    };

    void updateControlRate (int numSamples);
    void updateRouting();

    template <typename Function>
    void forEachSmoother (Function&&);

    static constexpr int controlInterval = 32;

    double sampleRate = 44100.0;
    Layout layout;
    Parameters parameters;
    int numSpeakers = 0;

    std::array<InputChannel, maxChannels> inputs;
    std::array<Line, numLines> lines;
    std::array<DelayLine, maxChannels> rearDelays;
    std::array<OnePole, maxChannels> rearTones;
    std::array<Filter, maxChannels> lfeFilters;
    std::array<std::array<float, numLines>, maxChannels> injection {};

    FilterCoefficients lowCutCoefficients, highCutCoefficients, lfeCoefficients;
    float rearToneCoefficient = 0.0f, rearToneAmount = 0.0f;
    float diffusionCoefficient = 0.0f;
    float decayCompensation = 1.0f;
    float maxLineDelay = 0.0f;

    // Smoothed per sample
    juce::SmoothedValue<float> dryGain, wetGain, sizeScale, modulationDepth, predelaySamples, rearDelaySamples, lfeSend, lfeReturn;
    std::array<juce::SmoothedValue<float>, 4> roleGains;

    // Smoothed at the control rate
    juce::SmoothedValue<float> decay, diffusion, lowCut, highCut, lowDampFrequency, lowDampRatio, highDampFrequency, highDampRatio, rearTone;
};
