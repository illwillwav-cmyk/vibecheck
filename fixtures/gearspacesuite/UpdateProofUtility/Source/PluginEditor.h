#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
class UpdateProofUtilityAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    UpdateProofUtilityAudioProcessorEditor(UpdateProofUtilityAudioProcessor&);
    ~UpdateProofUtilityAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;void resized() override;void timerCallback() override;
private:
    UpdateProofUtilityAudioProcessor& audioProcessor;
    juce::Image backgroundImage;
    juce::Slider gainSlider,panSlider;
    juce::TextButton monoBtn{"MONO"},swapBtn{"SWAP L/R"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAtt,panAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monoAtt,swapAtt;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UpdateProofUtilityAudioProcessorEditor)
};
