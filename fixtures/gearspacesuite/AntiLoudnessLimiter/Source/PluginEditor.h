#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
class AntiLoudnessLimiterAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    AntiLoudnessLimiterAudioProcessorEditor(AntiLoudnessLimiterAudioProcessor&);
    ~AntiLoudnessLimiterAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
private:
    AntiLoudnessLimiterAudioProcessor& audioProcessor;
    juce::Image backgroundImage;
    juce::Slider ceilingSlider,releaseSlider,inputSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ceilingAtt,releaseAtt,inputAtt;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AntiLoudnessLimiterAudioProcessorEditor)
};
