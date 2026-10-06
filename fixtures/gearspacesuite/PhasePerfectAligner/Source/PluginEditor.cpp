#include "PluginProcessor.h"
#include "PluginEditor.h"
PhasePerfectAlignerAudioProcessorEditor::PhasePerfectAlignerAudioProcessorEditor(PhasePerfectAlignerAudioProcessor& p)
    :AudioProcessorEditor(&p),audioProcessor(p){
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::bg_jpg, BinaryData::bg_jpgSize);
    setSize(800,600);startTimerHz(30);
    flipLBtn.setClickingTogglesState(true);flipLBtn.setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xFF6BCB77));
    addAndMakeVisible(flipLBtn);flipLAtt=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"FLIPL",flipLBtn);
    flipRBtn.setClickingTogglesState(true);flipRBtn.setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xFF6BCB77));
    addAndMakeVisible(flipRBtn);flipRAtt=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"FLIPR",flipRBtn);
    delaySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    delaySlider.setTextBoxStyle(juce::Slider::TextBoxRight,false,80,20);
    delaySlider.setColour(juce::Slider::trackColourId,juce::Colour(0xFF6BCB77));
    delaySlider.setColour(juce::Slider::thumbColourId,juce::Colour(0xFF6BCB77));
    delaySlider.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xFF6BCB77));
    delaySlider.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
    addAndMakeVisible(delaySlider);
    delayAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"DELAY",delaySlider);
}
PhasePerfectAlignerAudioProcessorEditor::~PhasePerfectAlignerAudioProcessorEditor(){}
void PhasePerfectAlignerAudioProcessorEditor::paint(juce::Graphics& g){
    if (backgroundImage.isValid()) {
        g.drawImage(backgroundImage, getLocalBounds().toFloat());
    } else {
        g.fillAll(juce::Colours::black);
    }
// Correlation meter
    float corr=audioProcessor.correlation.load();
    g.setColour(corr>0?juce::Colour(0xFF6BCB77):juce::Colour(0xFFFF6B6B));
    g.fillRoundedRectangle(400.0f-std::abs(corr)*200,420.0f,std::abs(corr)*400,20,6);
    g.setColour(juce::Colours::white);g.setFont(12.0f);
    g.drawText("CORRELATION: "+juce::String(corr,3),250,445,300,20,juce::Justification::centred);
    float rL=audioProcessor.rmsL.load(),rR=audioProcessor.rmsR.load();
    g.setColour(juce::Colour(0xFF6BCB77).withAlpha(0.4f));
    g.fillRoundedRectangle(100,510,rL*600,8,3);g.fillRoundedRectangle(100,522,rR*600,8,3);
}
void PhasePerfectAlignerAudioProcessorEditor::timerCallback(){repaint();}
void PhasePerfectAlignerAudioProcessorEditor::resized(){
    flipLBtn.setBounds(200,200,120,40);flipRBtn.setBounds(480,200,120,40);
    delaySlider.setBounds(150,340,500,30);
}
