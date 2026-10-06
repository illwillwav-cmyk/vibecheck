#include "PluginProcessor.h"
#include "PluginEditor.h"

GainMatchedSatAudioProcessorEditor::GainMatchedSatAudioProcessorEditor (GainMatchedSatAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::bg_jpg, BinaryData::bg_jpgSize);
    setSize (800, 600);
    startTimerHz(30);
    
    driveSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    driveSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    driveSlider.setName("DRIVE"); driveSlider.setLookAndFeel(&customLaf); addAndMakeVisible(driveSlider);
    driveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "DRIVE", driveSlider);

    mixSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    mixSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    mixSlider.setName("MIX"); mixSlider.setLookAndFeel(&customLaf); addAndMakeVisible(mixSlider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "MIX", mixSlider);

    outputSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    outputSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    outputSlider.setName("OUTPUT"); outputSlider.setLookAndFeel(&customLaf); addAndMakeVisible(outputSlider);
    outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "OUTPUT", outputSlider);

    powerButton.setClickingTogglesState(true);
    powerButton.setName("POWER"); powerButton.setLookAndFeel(&customLaf); addAndMakeVisible(powerButton);
    powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(audioProcessor.apvts, "POWER", powerButton);

    styleSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    styleSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    styleSlider.setName("STYLE"); styleSlider.setLookAndFeel(&customLaf); addAndMakeVisible(styleSlider);
    styleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "STYLE", styleSlider);
}


GainMatchedSatAudioProcessorEditor::~GainMatchedSatAudioProcessorEditor()
{
    driveSlider.setLookAndFeel(nullptr);
    mixSlider.setLookAndFeel(nullptr);
    outputSlider.setLookAndFeel(nullptr);
    powerButton.setLookAndFeel(nullptr);
    styleSlider.setLookAndFeel(nullptr);
}



void GainMatchedSatAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    if (backgroundImage.isValid())
        g.drawImage(backgroundImage, getLocalBounds().toFloat());

    // Draw VU Meters (animated)
    float rmsL = audioProcessor.rmsLevelLeft.load();
    float rmsR = audioProcessor.rmsLevelRight.load();
    
    // Scale RMS to degrees for needle
    float angleL = juce::jmap(rmsL, 0.0f, 1.0f, -0.8f, 0.8f);
    float angleR = juce::jmap(rmsR, 0.0f, 1.0f, -0.8f, 0.8f);

    // Left Meter
    g.setColour(juce::Colours::black.withAlpha(0.8f));
    juce::Point<float> pivotL(265.0f, 430.0f);
    juce::Line<float> needleL(pivotL, pivotL.getPointOnCircumference(70.0f, angleL - juce::MathConstants<float>::halfPi));
    g.drawLine(needleL, 2.0f);

    // Right Meter
    juce::Point<float> pivotR(535.0f, 430.0f);
    juce::Line<float> needleR(pivotR, pivotR.getPointOnCircumference(70.0f, angleR - juce::MathConstants<float>::halfPi));
    g.drawLine(needleR, 2.0f);
}



void GainMatchedSatAudioProcessorEditor::timerCallback()
{
    repaint();
}


void GainMatchedSatAudioProcessorEditor::resized()
{
    int dialSize = 175;
    driveSlider.setBounds(96, 178, dialSize, dialSize);
    mixSlider.setBounds(312, 178, dialSize, dialSize);
    outputSlider.setBounds(525, 178, dialSize, dialSize);
    
    powerButton.setBounds(80, 430, 50, 70);
    styleSlider.setBounds(660, 420, 70, 70);
}


