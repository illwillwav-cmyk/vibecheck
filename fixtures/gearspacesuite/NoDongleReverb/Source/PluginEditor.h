#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class BathLookAndFeel : public juce::LookAndFeel_V4 {
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                          const float rotaryStartAngle, const float rotaryEndAngle, juce::Slider&) override {
        float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        float radius = (float)width * 0.35f;
        float cx = (float)x + (float)width * 0.5f;
        float cy = (float)y + (float)height * 0.5f;
        float dx = cx + radius * std::cos(angle - juce::MathConstants<float>::halfPi);
        float dy = cy + radius * std::sin(angle - juce::MathConstants<float>::halfPi);
        g.setColour(juce::Colour(0xFF2A1810));
        g.drawLine(cx, cy, dx, dy, 3.0f);
    }
};

class NoDongleReverbAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    NoDongleReverbAudioProcessorEditor (NoDongleReverbAudioProcessor&);
    ~NoDongleReverbAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
private:
    NoDongleReverbAudioProcessor& audioProcessor;
    juce::Image backgroundImage;
    BathLookAndFeel bathLaf;
    juce::Slider decaySlider, sizeSlider, dampingSlider, diffusionSlider, mixSlider, typeSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        decayAtt, sizeAtt, dampingAtt, diffusionAtt, mixAtt, typeAtt;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoDongleReverbAudioProcessorEditor)
};
