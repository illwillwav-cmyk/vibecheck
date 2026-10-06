#include "PluginProcessor.h"
#include "PluginEditor.h"

static void setupKnob(juce::Slider& s, juce::LookAndFeel* laf) {
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    s.setLookAndFeel(laf);
}

NoDongleReverbAudioProcessorEditor::NoDongleReverbAudioProcessorEditor (NoDongleReverbAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::bg_jpg, BinaryData::bg_jpgSize);
    setSize (800, 600);
    startTimerHz(30);

    setupKnob(typeSlider, &bathLaf);    addAndMakeVisible(typeSlider);
    typeAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "TYPE", typeSlider);
    setupKnob(dampingSlider, &bathLaf); addAndMakeVisible(dampingSlider);
    dampingAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "DAMPING", dampingSlider);
    setupKnob(diffusionSlider, &bathLaf); addAndMakeVisible(diffusionSlider);
    diffusionAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "DIFFUSION", diffusionSlider);
    setupKnob(decaySlider, &bathLaf);   addAndMakeVisible(decaySlider);
    decayAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "DECAY", decaySlider);
    setupKnob(sizeSlider, &bathLaf);    addAndMakeVisible(sizeSlider);
    sizeAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "SIZE", sizeSlider);
    setupKnob(mixSlider, &bathLaf);     addAndMakeVisible(mixSlider);
    mixAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "MIX", mixSlider);
}

NoDongleReverbAudioProcessorEditor::~NoDongleReverbAudioProcessorEditor() {
    typeSlider.setLookAndFeel(nullptr); dampingSlider.setLookAndFeel(nullptr);
    diffusionSlider.setLookAndFeel(nullptr); decaySlider.setLookAndFeel(nullptr);
    sizeSlider.setLookAndFeel(nullptr); mixSlider.setLookAndFeel(nullptr);
}

void NoDongleReverbAudioProcessorEditor::paint (juce::Graphics& g) {
    g.fillAll(juce::Colours::black);
    if (backgroundImage.isValid())
        g.drawImage(backgroundImage, getLocalBounds().toFloat());
    // VU meters
    float rmsL = audioProcessor.rmsLevelLeft.load();
    float rmsR = audioProcessor.rmsLevelRight.load();
    float angleL = juce::jmap(rmsL, 0.0f, 0.5f, -0.7f, 0.7f);
    float angleR = juce::jmap(rmsR, 0.0f, 0.5f, -0.7f, 0.7f);
    g.setColour(juce::Colour(0xFF8B0000));
    juce::Point<float> pivotL(340.0f, 130.0f);
    g.drawLine(juce::Line<float>(pivotL, pivotL.getPointOnCircumference(40.0f, angleL - juce::MathConstants<float>::halfPi)), 2.0f);
    juce::Point<float> pivotR(460.0f, 130.0f);
    g.drawLine(juce::Line<float>(pivotR, pivotR.getPointOnCircumference(40.0f, angleR - juce::MathConstants<float>::halfPi)), 2.0f);
}

void NoDongleReverbAudioProcessorEditor::timerCallback() { repaint(); }

void NoDongleReverbAudioProcessorEditor::resized() {
    int ks = 100;
    // Top row: Reverb Type, Damping, Diffusion
    typeSlider.setBounds(185, 200, ks, ks);
    dampingSlider.setBounds(350, 200, ks, ks);
    diffusionSlider.setBounds(515, 200, ks, ks);
    // Bottom row: Decay, Size, Wet/Dry
    decaySlider.setBounds(185, 370, ks, ks);
    sizeSlider.setBounds(350, 370, ks, ks);
    mixSlider.setBounds(515, 370, ks, ks);
}
