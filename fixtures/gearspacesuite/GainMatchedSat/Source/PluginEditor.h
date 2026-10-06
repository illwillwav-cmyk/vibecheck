#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"


class RetroLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                          const float rotaryStartAngle, const float rotaryEndAngle, juce::Slider& slider) override
    {
        // For the main knobs, we draw nothing (they are invisible over the image)
        // Wait, the styleSlider is ALSO a rotary slider. How do we distinguish?
        // We can check slider.getName()
        if (slider.getName() == "STYLE") {
            // Draw a red glowing dot indicating the position
            float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
            float radius = width * 0.4f;
            float cx = x + width * 0.5f;
            float cy = y + height * 0.5f;
            float dotX = cx + radius * std::cos(angle - juce::MathConstants<float>::halfPi);
            float dotY = cy + radius * std::sin(angle - juce::MathConstants<float>::halfPi);
            
            g.setColour(juce::Colours::red);
            g.fillEllipse(dotX - 5, dotY - 5, 10, 10);
            
            // Draw a subtle glow
            g.setColour(juce::Colours::red.withAlpha(0.3f));
            g.fillEllipse(dotX - 10, dotY - 10, 20, 20);
        } else {
            // Main knobs: Draw an indicator line
            float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
            float radius = width * 0.35f;
            float cx = x + width * 0.5f;
            float cy = y + height * 0.5f;
            float dotX = cx + radius * std::cos(angle - juce::MathConstants<float>::halfPi);
            float dotY = cy + radius * std::sin(angle - juce::MathConstants<float>::halfPi);
            
            g.setColour(juce::Colours::orange);
            g.drawLine(cx, cy, dotX, dotY, 4.0f);
        }
    }
    
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        if (button.getName() == "POWER") {
            // Draw a LED
            float cx = button.getWidth() * 0.5f;
            float cy = button.getHeight() * 0.5f - 40; // Draw it above the switch
            if (button.getToggleState()) {
                g.setColour(juce::Colours::limegreen);
                g.fillEllipse(cx - 8, cy - 8, 16, 16);
                g.setColour(juce::Colours::limegreen.withAlpha(0.4f));
                g.fillEllipse(cx - 15, cy - 15, 30, 30);
            } else {
                g.setColour(juce::Colours::red);
                g.fillEllipse(cx - 8, cy - 8, 16, 16);
                g.setColour(juce::Colours::red.withAlpha(0.4f));
                g.fillEllipse(cx - 15, cy - 15, 30, 30);
            }
        }
    }
};

class GainMatchedSatAudioProcessorEditor : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    GainMatchedSatAudioProcessorEditor (GainMatchedSatAudioProcessor&);
    ~GainMatchedSatAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    juce::Slider driveSlider, mixSlider, outputSlider;
    juce::TextButton powerButton;
    juce::Slider styleSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driveAttachment, mixAttachment, outputAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> styleAttachment;

    GainMatchedSatAudioProcessor& audioProcessor;
    juce::Image backgroundImage;
    RetroLookAndFeel customLaf;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GainMatchedSatAudioProcessorEditor)
};
