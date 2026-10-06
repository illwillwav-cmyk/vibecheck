#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
class CPUFriendlySynthAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    CPUFriendlySynthAudioProcessorEditor(CPUFriendlySynthAudioProcessor&);
    ~CPUFriendlySynthAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;void resized() override;void timerCallback() override;
private:
    CPUFriendlySynthAudioProcessor& audioProcessor;
    juce::Image backgroundImage;
    juce::Slider gainSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAtt;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CPUFriendlySynthAudioProcessorEditor)
};
