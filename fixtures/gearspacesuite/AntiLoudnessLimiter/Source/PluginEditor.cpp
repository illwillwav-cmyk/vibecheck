#include "PluginProcessor.h"
#include "PluginEditor.h"
static void sk(juce::Slider& s,juce::Colour c){
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow,false,80,16);
    s.setColour(juce::Slider::rotarySliderFillColourId,c);
    s.setColour(juce::Slider::rotarySliderOutlineColourId,juce::Colour(0xFF1A1A2E));
    s.setColour(juce::Slider::thumbColourId,c);
    s.setColour(juce::Slider::textBoxTextColourId,c);
    s.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
}
AntiLoudnessLimiterAudioProcessorEditor::AntiLoudnessLimiterAudioProcessorEditor(AntiLoudnessLimiterAudioProcessor& p)
    :AudioProcessorEditor(&p),audioProcessor(p){
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::bg_jpg, BinaryData::bg_jpgSize);
    setSize(800,600);startTimerHz(30);
    juce::Colour ac(0xFFFF9F43);
    sk(inputSlider,ac);addAndMakeVisible(inputSlider);
    inputAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"INPUT",inputSlider);
    sk(ceilingSlider,ac);addAndMakeVisible(ceilingSlider);
    ceilingAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"CEILING",ceilingSlider);
    sk(releaseSlider,ac);addAndMakeVisible(releaseSlider);
    releaseAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"RELEASE",releaseSlider);
}
AntiLoudnessLimiterAudioProcessorEditor::~AntiLoudnessLimiterAudioProcessorEditor(){}
void AntiLoudnessLimiterAudioProcessorEditor::paint(juce::Graphics& g){
    if (backgroundImage.isValid()) {
        g.drawImage(backgroundImage, getLocalBounds().toFloat());
    } else {
        g.fillAll(juce::Colours::black);
    }

    float gr=audioProcessor.gainReduction.load();
    float grDB=20.0f*std::log10(std::max(gr,0.0001f));
    g.setColour(juce::Colour(0xFFFF9F43).withAlpha(0.7f));
    g.fillRoundedRectangle(100,440,std::max(0.0f,(grDB+30.0f)/30.0f)*600,20,8);
    g.setColour(juce::Colours::white);g.setFont(14.0f);
    g.drawText("GR: "+juce::String(grDB,1)+" dB",100,465,200,20,juce::Justification::left);
    float rL=audioProcessor.rmsLevelLeft.load(),rR=audioProcessor.rmsLevelRight.load();
    g.setColour(juce::Colour(0xFFFF9F43).withAlpha(0.4f));
    g.fillRoundedRectangle(100,500,rL*600,8,3);g.fillRoundedRectangle(100,512,rR*600,8,3);
}
void AntiLoudnessLimiterAudioProcessorEditor::timerCallback(){repaint();}
void AntiLoudnessLimiterAudioProcessorEditor::resized(){
    int ks=150;
    inputSlider.setBounds(130,200,ks,ks);
    ceilingSlider.setBounds(330,200,ks,ks);
    releaseSlider.setBounds(530,200,ks,ks);
}
