/*
  ==============================================================================

    SQUEEZE COMP - plugin editor

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <memory>
#include <vector>

//==============================================================================
/** Builds a Font in a way that compiles on both JUCE 7 and JUCE 8. */
juce::Font makePedalFont (float height, bool bold);

//==============================================================================
/** Flat pedal-panel styling: thin value arcs, plain knob caps, and a round
    footswitch. */
class PedalLookAndFeel  : public juce::LookAndFeel_V4
{
public:
    PedalLookAndFeel();

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override;

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;
};

//==============================================================================
class NewProjectAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    NewProjectAudioProcessorEditor (NewProjectAudioProcessor&);
    ~NewProjectAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    NewProjectAudioProcessor& audioProcessor;

    // Declared before the controls so it outlives them during destruction
    PedalLookAndFeel pedalLookAndFeel;

    struct ChoiceControl
    {
        juce::Label    caption;
        juce::ComboBox box;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    struct KnobControl
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    std::vector<std::unique_ptr<ChoiceControl>> choices;
    std::vector<std::unique_ptr<KnobControl>>   knobs;

    juce::ToggleButton footswitch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    juce::Rectangle<int> ledBounds;
    bool lastBypassState = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NewProjectAudioProcessorEditor)
};
