#include "PluginProcessor.h"
#include "PluginEditor.h"
CPUFriendlySynthAudioProcessorEditor::CPUFriendlySynthAudioProcessorEditor(CPUFriendlySynthAudioProcessor& p)
    :AudioProcessorEditor(&p),audioProcessor(p){
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::bg_jpg, BinaryData::bg_jpgSize);
    setSize(800,600);startTimerHz(30);
    gainSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    gainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,80,20);
    gainSlider.setColour(juce::Slider::rotarySliderFillColourId,juce::Colour(0xFFB388FF));
    gainSlider.setColour(juce::Slider::rotarySliderOutlineColourId,juce::Colour(0xFF1A1A2E));
    gainSlider.setColour(juce::Slider::thumbColourId,juce::Colour(0xFFB388FF));
    gainSlider.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xFFB388FF));
    gainSlider.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
    addAndMakeVisible(gainSlider);
    gainAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"GAIN",gainSlider);
}
CPUFriendlySynthAudioProcessorEditor::~CPUFriendlySynthAudioProcessorEditor(){}
void CPUFriendlySynthAudioProcessorEditor::paint(juce::Graphics& g){
    if (backgroundImage.isValid()) {
        g.drawImage(backgroundImage, getLocalBounds().toFloat());
    } else {
        g.fillAll(juce::Colours::black);
    }

    float rms=audioProcessor.rmsLevel.load();
    g.setColour(juce::Colour(0xFFB388FF).withAlpha(0.5f));
    g.fillRoundedRectangle(200,500,rms*400,14,5);
}
void CPUFriendlySynthAudioProcessorEditor::timerCallback(){repaint();}
void CPUFriendlySynthAudioProcessorEditor::resized(){
    gainSlider.setBounds(300,250,200,200);
}
