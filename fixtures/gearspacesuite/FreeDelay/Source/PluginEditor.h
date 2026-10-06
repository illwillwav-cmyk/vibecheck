#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class FreeDelayAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    FreeDelayAudioProcessorEditor (FreeDelayAudioProcessor&);
    ~FreeDelayAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
private:
    FreeDelayAudioProcessor& audioProcessor;
    juce::Image backgroundImage;
    juce::Slider timeSlider, feedbackSlider, mixSlider, toneSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> timeAtt, feedbackAtt, mixAtt, toneAtt;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FreeDelayAudioProcessorEditor)
};
