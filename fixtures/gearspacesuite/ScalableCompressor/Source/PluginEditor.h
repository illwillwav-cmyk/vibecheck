#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
class ScalableCompressorAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    ScalableCompressorAudioProcessorEditor(ScalableCompressorAudioProcessor&);
    ~ScalableCompressorAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
private:
    ScalableCompressorAudioProcessor& audioProcessor;
    juce::Image backgroundImage;
    juce::Slider threshSlider, ratioSlider, attackSlider, releaseSlider, makeupSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> threshAtt,ratioAtt,attackAtt,releaseAtt,makeupAtt;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScalableCompressorAudioProcessorEditor)
};
