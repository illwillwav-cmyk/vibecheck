#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
class PhasePerfectAlignerAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    PhasePerfectAlignerAudioProcessorEditor(PhasePerfectAlignerAudioProcessor&);
    ~PhasePerfectAlignerAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;void resized() override;void timerCallback() override;
private:
    PhasePerfectAlignerAudioProcessor& audioProcessor;
    juce::Image backgroundImage;
    juce::TextButton flipLBtn{"FLIP L"},flipRBtn{"FLIP R"};
    juce::Slider delaySlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> flipLAtt,flipRAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> delayAtt;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhasePerfectAlignerAudioProcessorEditor)
};
