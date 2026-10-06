#include "PluginProcessor.h"
#include "PluginEditor.h"
UpdateProofUtilityAudioProcessorEditor::UpdateProofUtilityAudioProcessorEditor(UpdateProofUtilityAudioProcessor& p)
    :AudioProcessorEditor(&p),audioProcessor(p){
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::bg_jpg, BinaryData::bg_jpgSize);
    setSize(800,600);startTimerHz(30);
    gainSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    gainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,80,20);
    gainSlider.setColour(juce::Slider::rotarySliderFillColourId,juce::Colour(0xFF54A0FF));
    gainSlider.setColour(juce::Slider::rotarySliderOutlineColourId,juce::Colour(0xFF1A1A2E));
    gainSlider.setColour(juce::Slider::thumbColourId,juce::Colour(0xFF54A0FF));
    gainSlider.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xFF54A0FF));
    gainSlider.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
    addAndMakeVisible(gainSlider);
    gainAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"GAIN",gainSlider);
    panSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    panSlider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,80,20);
    panSlider.setColour(juce::Slider::rotarySliderFillColourId,juce::Colour(0xFF54A0FF));
    panSlider.setColour(juce::Slider::rotarySliderOutlineColourId,juce::Colour(0xFF1A1A2E));
    panSlider.setColour(juce::Slider::thumbColourId,juce::Colour(0xFF54A0FF));
    panSlider.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xFF54A0FF));
    panSlider.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
    addAndMakeVisible(panSlider);
    panAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"PAN",panSlider);
    monoBtn.setClickingTogglesState(true);monoBtn.setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xFF54A0FF));
    addAndMakeVisible(monoBtn);monoAtt=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"MONO",monoBtn);
    swapBtn.setClickingTogglesState(true);swapBtn.setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xFF54A0FF));
    addAndMakeVisible(swapBtn);swapAtt=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"SWAP",swapBtn);
}
UpdateProofUtilityAudioProcessorEditor::~UpdateProofUtilityAudioProcessorEditor(){}
void UpdateProofUtilityAudioProcessorEditor::paint(juce::Graphics& g){
    if (backgroundImage.isValid()) {
        g.drawImage(backgroundImage, getLocalBounds().toFloat());
    } else {
        g.fillAll(juce::Colours::black);
    }

    float rL=audioProcessor.rmsL.load(),rR=audioProcessor.rmsR.load();
    g.setColour(juce::Colour(0xFF54A0FF).withAlpha(0.4f));
    g.fillRoundedRectangle(100,520,rL*600,8,3);g.fillRoundedRectangle(100,534,rR*600,8,3);
    g.setColour(juce::Colours::white);g.setFont(11.0f);
    g.drawText("L",85,517,15,14,juce::Justification::centred);
    g.drawText("R",85,531,15,14,juce::Justification::centred);
}
void UpdateProofUtilityAudioProcessorEditor::timerCallback(){repaint();}
void UpdateProofUtilityAudioProcessorEditor::resized(){
    gainSlider.setBounds(200,220,150,150);
    panSlider.setBounds(450,220,150,150);
    monoBtn.setBounds(250,460,100,35);
    swapBtn.setBounds(450,460,100,35);
}
