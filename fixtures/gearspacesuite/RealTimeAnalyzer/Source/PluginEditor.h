#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
class RealTimeAnalyzerAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    RealTimeAnalyzerAudioProcessorEditor(RealTimeAnalyzerAudioProcessor&);
    ~RealTimeAnalyzerAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;void resized() override;void timerCallback() override;
private:
    RealTimeAnalyzerAudioProcessor& audioProcessor;
    juce::Image backgroundImage;
    juce::Slider gainSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAtt;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RealTimeAnalyzerAudioProcessorEditor)
};
