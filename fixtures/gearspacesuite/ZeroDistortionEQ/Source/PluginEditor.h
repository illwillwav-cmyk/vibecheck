#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
class ZeroDistortionEQAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    ZeroDistortionEQAudioProcessorEditor(ZeroDistortionEQAudioProcessor&);
    ~ZeroDistortionEQAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
private:
    ZeroDistortionEQAudioProcessor& audioProcessor;
    juce::Image backgroundImage;
    juce::Slider freqSliders[4], gainSliders[4], qSliders[4];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> freqAtts[4], gainAtts[4], qAtts[4];
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZeroDistortionEQAudioProcessorEditor)
};
