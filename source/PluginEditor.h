#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
class ReverbLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ReverbLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    static const juce::Colour background, panel, outline, accent, text, dimmedText;
};

//==============================================================================
// A rotary slider bound to a parameter, with its name above it
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID, const juce::String& title);

    void resized() override;

private:
    juce::Slider slider;
    juce::Label label;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

//==============================================================================
// One peak meter per output channel, labelled with the speaker it feeds
class OutputMeters : public juce::Component,
                     private juce::Timer
{
public:
    explicit OutputMeters (PluginProcessor&);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    PluginProcessor& processor;
    juce::AudioChannelSet layout;
    std::array<float, ReverbEngine::maxChannels> levels {};
};

//==============================================================================
class PluginEditor : public juce::AudioProcessorEditor,
                     private juce::Timer
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Section
    {
        juce::String title;
        juce::Rectangle<int> bounds;
    };

    void timerCallback() override;
    void updateForLayout();

    // Lays sections out side by side, each as wide as the number of knobs it holds
    void layoutRow (juce::Rectangle<int> area, std::initializer_list<std::pair<Section*, std::vector<juce::Component*>>> row);

    PluginProcessor& processorRef;
    ReverbLookAndFeel lookAndFeel;

    Knob size, decay, predelay, diffusion, modulation;
    Knob lowDampFrequency, lowDampRatio, highDampFrequency, highDampRatio;
    Knob frontLevel, centreLevel, surroundLevel, heightLevel;
    Knob rearDelay, rearTone;
    Knob lfeSend, lfeReturn;
    Knob lowCut, highCut, mix;
    OutputMeters meters;

    Section reverbSection { "Reverb", {} }, dampingSection { "Damping", {} }, levelsSection { "Speaker Levels", {} }, rearSection { "Rear", {} },
        lfeSection { "LFE", {} }, toneSection { "Tone & Mix", {} }, outputSection { "Output", {} };
    juce::AudioChannelSet inputLayout, outputLayout;
    juce::String layoutDescription;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
