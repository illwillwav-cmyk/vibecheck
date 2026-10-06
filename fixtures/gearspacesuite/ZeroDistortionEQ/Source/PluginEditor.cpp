#include "PluginProcessor.h"
#include "PluginEditor.h"

static void setupKnob(juce::Slider& s, juce::Colour c) {
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
    s.setColour(juce::Slider::rotarySliderFillColourId, c);
    s.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xFF1A1A2E));
    s.setColour(juce::Slider::thumbColourId, c);
    s.setColour(juce::Slider::textBoxTextColourId, c);
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

ZeroDistortionEQAudioProcessorEditor::ZeroDistortionEQAudioProcessorEditor(ZeroDistortionEQAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p) {
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::bg_jpg, BinaryData::bg_jpgSize);
    setSize(800,600); startTimerHz(30);
    juce::Colour bandCols[] = {juce::Colour(0xFFFF6B6B),juce::Colour(0xFFFFD93D),juce::Colour(0xFF6BCB77),juce::Colour(0xFF4D96FF)};
    for (int i = 0; i < 4; ++i) {
        auto si = juce::String(i+1);
        setupKnob(freqSliders[i], bandCols[i]); addAndMakeVisible(freqSliders[i]);
        freqAtts[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"FREQ"+si,freqSliders[i]);
        setupKnob(gainSliders[i], bandCols[i]); addAndMakeVisible(gainSliders[i]);
        gainAtts[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"GAIN"+si,gainSliders[i]);
        setupKnob(qSliders[i], bandCols[i]);    addAndMakeVisible(qSliders[i]);
        qAtts[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"Q"+si,qSliders[i]);
    }
}
ZeroDistortionEQAudioProcessorEditor::~ZeroDistortionEQAudioProcessorEditor() {}

void ZeroDistortionEQAudioProcessorEditor::paint(juce::Graphics& g) {
    if (backgroundImage.isValid()) {
        g.drawImage(backgroundImage, getLocalBounds().toFloat());
    } else {
        g.fillAll(juce::Colours::black);
    }

    juce::Colour bandCols[] = {juce::Colour(0xFFFF6B6B),juce::Colour(0xFFFFD93D),juce::Colour(0xFF6BCB77),juce::Colour(0xFF4D96FF)};
    const char* labels[] = {"LOW","LOW-MID","HI-MID","HIGH"};
    for (int i = 0; i < 4; ++i) {
        int x = 40 + i * 190;
        g.setColour(bandCols[i]); g.setFont(14.0f);
        g.drawText(labels[i], x, 90, 160, 20, juce::Justification::centred);
        g.drawText("FREQ", x, 240, 50, 14, juce::Justification::centred);
        g.drawText("GAIN", x+55, 240, 50, 14, juce::Justification::centred);
        g.drawText("Q",    x+110, 240, 50, 14, juce::Justification::centred);
    }
    // RMS bars
    float rL = audioProcessor.rmsLevelLeft.load();
    float rR = audioProcessor.rmsLevelRight.load();
    g.setColour(juce::Colour(0xFF4D96FF).withAlpha(0.5f));
    g.fillRoundedRectangle(40.0f, 555.0f, rL*720, 8, 3);
    g.fillRoundedRectangle(40.0f, 567.0f, rR*720, 8, 3);
}
void ZeroDistortionEQAudioProcessorEditor::timerCallback() { repaint(); }
void ZeroDistortionEQAudioProcessorEditor::resized() {
    int ks = 70;
    for (int i = 0; i < 4; ++i) {
        int x = 40 + i * 190;
        freqSliders[i].setBounds(x, 120, ks, ks+20);
        gainSliders[i].setBounds(x+55, 120, ks, ks+20);
        qSliders[i].setBounds(x+110, 120, ks, ks+20);
    }
}
