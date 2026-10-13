#include "PluginEditor.h"

namespace
{
    constexpr int margin = 12;
    constexpr int headerHeight = 44;
    constexpr int sectionTitleHeight = 26;
    constexpr int rowHeight = 132;
    constexpr int knobWidth = 98;
    constexpr int knobsPerRow = 9;
    constexpr int meterRefreshHz = 30;
    constexpr float minMeterDb = -60.0f;

    bool hasRole (const juce::AudioChannelSet& layout, ReverbEngine::Role role)
    {
        // A mono layout is all front, see PluginProcessor::makeEngineLayout
        if (layout.size() <= 1)
            return role == ReverbEngine::Role::front;

        for (int channel = 0; channel < layout.size(); ++channel)
            if (PluginProcessor::getRoleForChannel (layout.getTypeOfChannel (channel)) == role)
                return true;

        return false;
    }
}

//==============================================================================
const juce::Colour ReverbLookAndFeel::background { 0xff14181f };
const juce::Colour ReverbLookAndFeel::panel { 0xff1c222c };
const juce::Colour ReverbLookAndFeel::outline { 0xff2e3744 };
const juce::Colour ReverbLookAndFeel::accent { 0xff4fc3c8 };
const juce::Colour ReverbLookAndFeel::text { 0xffe6eaf0 };
const juce::Colour ReverbLookAndFeel::dimmedText { 0xff8b96a5 };

ReverbLookAndFeel::ReverbLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, accent.withAlpha (0.4f));
    setColour (juce::Label::textColourId, dimmedText);
    setColour (juce::TextEditor::backgroundColourId, background);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.4f));
    setColour (juce::TextEditor::outlineColourId, outline);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
}

void ReverbLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (6.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    const auto thickness = 4.0f;
    const auto arcRadius = radius - thickness * 0.5f;
    const auto alpha = slider.isEnabled() ? 1.0f : 0.3f;
    const juce::PathStrokeType stroke (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (outline.withMultipliedAlpha (alpha));
    g.strokePath (track, stroke);

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, angle, true);
    g.setColour (accent.withMultipliedAlpha (alpha));
    g.strokePath (value, stroke);

    const auto capRadius = arcRadius - 7.0f;
    g.setColour (panel.brighter (0.12f));
    g.fillEllipse (juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre));

    const auto pointerTip = centre.getPointOnCircumference (capRadius - 3.0f, angle);
    const auto pointerBase = centre.getPointOnCircumference (capRadius * 0.45f, angle);
    g.setColour (text.withMultipliedAlpha (alpha));
    g.drawLine ({ pointerBase, pointerTip }, 2.0f);
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID, const juce::String& title)
    : slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow),
      attachment (state, parameterID, slider)
{
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    addAndMakeVisible (slider);

    label.setText (title, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (label);
}

void Knob::resized()
{
    auto area = getLocalBounds();
    label.setBounds (area.removeFromTop (18));
    slider.setBounds (area);
}

//==============================================================================
OutputMeters::OutputMeters (PluginProcessor& p)
    : processor (p)
{
    levels.fill (minMeterDb);
    startTimerHz (meterRefreshHz);
}

void OutputMeters::timerCallback()
{
    layout = processor.getBusesLayout().getMainOutputChannelSet();

    // Fall back at a fixed rate between peaks, so short transients stay readable
    const auto fallPerFrame = 36.0f / (float) meterRefreshHz;

    for (size_t channel = 0; channel < levels.size(); ++channel)
    {
        const auto peak = juce::Decibels::gainToDecibels (processor.readOutputPeak ((int) channel), minMeterDb);
        levels[channel] = juce::jmax (peak, levels[channel] - fallPerFrame, minMeterDb);
    }

    repaint();
}

void OutputMeters::paint (juce::Graphics& g)
{
    const auto numChannels = juce::jmin (layout.size(), (int) levels.size());

    if (numChannels == 0)
        return;

    auto area = getLocalBounds().toFloat();
    const auto slot = area.getWidth() / (float) numChannels;
    g.setFont (juce::FontOptions (11.0f));

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto column = area.removeFromLeft (slot);
        const auto name = juce::AudioChannelSet::getAbbreviatedChannelTypeName (layout.getTypeOfChannel (channel));

        g.setColour (ReverbLookAndFeel::dimmedText);
        g.drawText (name, column.removeFromBottom (16.0f), juce::Justification::centred, false);

        const auto bar = column.withSizeKeepingCentre (juce::jmin (14.0f, slot - 6.0f), column.getHeight() - 4.0f);
        g.setColour (ReverbLookAndFeel::background);
        g.fillRoundedRectangle (bar, 2.0f);

        const auto level = juce::jmap (levels[(size_t) channel], minMeterDb, 0.0f, 0.0f, 1.0f);
        const auto isClipping = levels[(size_t) channel] > 0.0f;
        g.setColour (isClipping ? juce::Colours::orangered : ReverbLookAndFeel::accent);
        g.fillRoundedRectangle (bar.withTop (bar.getBottom() - bar.getHeight() * juce::jlimit (0.0f, 1.0f, level)), 2.0f);
    }
}

//==============================================================================
PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      size (p.state, ParameterIDs::size, "Size"),
      decay (p.state, ParameterIDs::decay, "Decay"),
      predelay (p.state, ParameterIDs::predelay, "Pre-Delay"),
      diffusion (p.state, ParameterIDs::diffusion, "Diffusion"),
      modulation (p.state, ParameterIDs::modulation, "Modulation"),
      lowDampFrequency (p.state, ParameterIDs::lowDampFrequency, "Low Freq"),
      lowDampRatio (p.state, ParameterIDs::lowDampRatio, "Low Decay"),
      highDampFrequency (p.state, ParameterIDs::highDampFrequency, "High Freq"),
      highDampRatio (p.state, ParameterIDs::highDampRatio, "High Decay"),
      frontLevel (p.state, ParameterIDs::frontLevel, "Front"),
      centreLevel (p.state, ParameterIDs::centreLevel, "Center"),
      surroundLevel (p.state, ParameterIDs::surroundLevel, "Surround"),
      heightLevel (p.state, ParameterIDs::heightLevel, "Height"),
      rearDelay (p.state, ParameterIDs::rearDelay, "Delay"),
      rearTone (p.state, ParameterIDs::rearTone, "Tone"),
      lfeSend (p.state, ParameterIDs::lfeSend, "Send"),
      lfeReturn (p.state, ParameterIDs::lfeReturn, "Return"),
      lowCut (p.state, ParameterIDs::lowCut, "Low Cut"),
      highCut (p.state, ParameterIDs::highCut, "High Cut"),
      mix (p.state, ParameterIDs::mix, "Mix"),
      meters (p)
{
    setLookAndFeel (&lookAndFeel);

    for (auto* knob : { &size, &decay, &predelay, &diffusion, &modulation, &lowDampFrequency, &lowDampRatio, &highDampFrequency, &highDampRatio, &frontLevel, &centreLevel, &surroundLevel, &heightLevel, &rearDelay, &rearTone, &lfeSend, &lfeReturn, &lowCut, &highCut, &mix })
        addAndMakeVisible (knob);

    addAndMakeVisible (meters);

    updateForLayout();
    startTimerHz (4);

    setSize (knobsPerRow * knobWidth + 4 * margin, headerHeight + 3 * (rowHeight + sectionTitleHeight + margin));
}

PluginEditor::~PluginEditor()
{
    setLookAndFeel (nullptr);
}

void PluginEditor::timerCallback()
{
    // Hosts can change the bus layout while the editor is open
    const auto layout = processorRef.getBusesLayout();

    if (layout.getMainInputChannelSet() != inputLayout || layout.getMainOutputChannelSet() != outputLayout)
        updateForLayout();
}

void PluginEditor::updateForLayout()
{
    const auto layout = processorRef.getBusesLayout();
    inputLayout = layout.getMainInputChannelSet();
    outputLayout = layout.getMainOutputChannelSet();

    layoutDescription = inputLayout == outputLayout ? outputLayout.getDescription()
                                                    : inputLayout.getDescription() + juce::String::fromUTF8 (" \xe2\x86\x92 ") + outputLayout.getDescription();

    // Controls for speakers this layout doesn't have are greyed out
    const auto hasSurrounds = hasRole (outputLayout, ReverbEngine::Role::surround);
    frontLevel.setEnabled (hasRole (outputLayout, ReverbEngine::Role::front));
    centreLevel.setEnabled (hasRole (outputLayout, ReverbEngine::Role::centre));
    surroundLevel.setEnabled (hasSurrounds);
    heightLevel.setEnabled (hasRole (outputLayout, ReverbEngine::Role::height));
    rearDelay.setEnabled (hasSurrounds);
    rearTone.setEnabled (hasSurrounds);
    lfeSend.setEnabled (hasRole (inputLayout, ReverbEngine::Role::lfe));
    lfeReturn.setEnabled (hasRole (outputLayout, ReverbEngine::Role::lfe));

    repaint();
}

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (ReverbLookAndFeel::background);

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (margin + 4, 0);
    g.setColour (ReverbLookAndFeel::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText (PRODUCT_NAME_WITHOUT_VERSION, header, juce::Justification::centredLeft, false);

    g.setColour (ReverbLookAndFeel::accent);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (layoutDescription, header, juce::Justification::centredRight, false);

    for (const auto* section : { &reverbSection, &dampingSection, &levelsSection, &rearSection, &lfeSection, &toneSection, &outputSection })
    {
        g.setColour (ReverbLookAndFeel::panel);
        g.fillRoundedRectangle (section->bounds.toFloat(), 6.0f);

        g.setColour (ReverbLookAndFeel::dimmedText);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (section->title.toUpperCase(), section->bounds.withHeight (sectionTitleHeight).reduced (12, 0), juce::Justification::centredLeft, false);
    }
}

void PluginEditor::layoutRow (juce::Rectangle<int> area, std::initializer_list<std::pair<Section*, std::vector<juce::Component*>>> row)
{
    auto remaining = (int) row.size();

    for (const auto& [section, components] : row)
    {
        // The last section takes whatever is left, so rounding never leaves a gap at the edge
        const auto isLast = --remaining == 0;
        const auto width = isLast ? area.getWidth() : (int) components.size() * knobWidth + margin;
        section->bounds = area.removeFromLeft (width);
        area.removeFromLeft (margin);

        auto content = section->bounds.withTrimmedTop (sectionTitleHeight).reduced (6, 6);
        const auto slot = content.getWidth() / juce::jmax (1, (int) components.size());

        for (auto* component : components)
            component->setBounds (content.removeFromLeft (slot).reduced (2, 0));
    }
}

void PluginEditor::resized()
{
    auto area = getLocalBounds().reduced (margin);
    area.removeFromTop (headerHeight - margin);

    const auto nextRow = [&area] {
        const auto row = area.removeFromTop (rowHeight + sectionTitleHeight);
        area.removeFromTop (margin);
        return row;
    };

    layoutRow (nextRow(), { { &reverbSection, { &size, &decay, &predelay, &diffusion, &modulation } }, { &dampingSection, { &lowDampFrequency, &lowDampRatio, &highDampFrequency, &highDampRatio } } });
    layoutRow (nextRow(), { { &levelsSection, { &frontLevel, &centreLevel, &surroundLevel, &heightLevel } }, { &rearSection, { &rearDelay, &rearTone } }, { &lfeSection, { &lfeSend, &lfeReturn } } });
    layoutRow (nextRow(), { { &toneSection, { &lowCut, &highCut, &mix } }, { &outputSection, { &meters } } });
}
