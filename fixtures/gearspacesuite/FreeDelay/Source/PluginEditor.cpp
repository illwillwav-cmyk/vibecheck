#include "PluginProcessor.h"
#include "PluginEditor.h"

static void setupKnob(juce::Slider& s) {
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 18);
    s.setColour(juce::Slider::textBoxTextColourId, juce::Colours::cyan);
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xFF00CCCC));
    s.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xFF1A1A2E));
    s.setColour(juce::Slider::thumbColourId, juce::Colours::cyan);
}

FreeDelayAudioProcessorEditor::FreeDelayAudioProcessorEditor (FreeDelayAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::bg_jpg, BinaryData::bg_jpgSize);
    setSize (800, 600); startTimerHz(30);
    setupKnob(timeSlider);     addAndMakeVisible(timeSlider);
    timeAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "TIME", timeSlider);
    setupKnob(feedbackSlider); addAndMakeVisible(feedbackSlider);
    feedbackAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "FEEDBACK", feedbackSlider);
    setupKnob(mixSlider);      addAndMakeVisible(mixSlider);
    mixAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "MIX", mixSlider);
    setupKnob(toneSlider);     addAndMakeVisible(toneSlider);
    toneAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "TONE", toneSlider);
}
FreeDelayAudioProcessorEditor::~FreeDelayAudioProcessorEditor() {}

void FreeDelayAudioProcessorEditor::paint (juce::Graphics& g) {
    g.fillAll(juce::Colour(0xFF0A0A1A));
    if (backgroundImage.isValid())
        g.drawImage(backgroundImage, getLocalBounds().toFloat());

    g.setColour(juce::Colour(0xAA0A0A2E));
    g.fillRoundedRectangle(30, 30, 740, 540, 20);

    g.setColour(juce::Colours::cyan);
    g.setFont(juce::Font(36.0f));
    g.drawText("FREE DELAY", 0, 40, 800, 50, juce::Justification::centred);
    g.setFont(juce::Font(14.0f));
    g.drawText("TIME",     100, 380, 120, 20, juce::Justification::centred);
    g.drawText("FEEDBACK", 260, 380, 120, 20, juce::Justification::centred);
    g.drawText("MIX",      420, 380, 120, 20, juce::Justification::centred);
    g.drawText("TONE",     580, 380, 120, 20, juce::Justification::centred);

    // VU bars
    float rmsL = audioProcessor.rmsLevelLeft.load();
    float rmsR = audioProcessor.rmsLevelRight.load();
    g.setColour(juce::Colours::cyan.withAlpha(0.6f));
    g.fillRoundedRectangle(100, 120, rmsL * 600, 12, 4);
    g.fillRoundedRectangle(100, 140, rmsR * 600, 12, 4);
}
void FreeDelayAudioProcessorEditor::timerCallback() { repaint(); }
void FreeDelayAudioProcessorEditor::resized() {
    int ks = 140;
    timeSlider.setBounds(90, 220, ks, ks);
    feedbackSlider.setBounds(250, 220, ks, ks);
    mixSlider.setBounds(410, 220, ks, ks);
    toneSlider.setBounds(570, 220, ks, ks);
}
