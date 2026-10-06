#include "PluginProcessor.h"
#include "PluginEditor.h"
static void sk(juce::Slider& s,juce::Colour c){
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow,false,70,16);
    s.setColour(juce::Slider::rotarySliderFillColourId,c);
    s.setColour(juce::Slider::rotarySliderOutlineColourId,juce::Colour(0xFF1A1A2E));
    s.setColour(juce::Slider::thumbColourId,c);
    s.setColour(juce::Slider::textBoxTextColourId,c);
    s.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
}
ScalableCompressorAudioProcessorEditor::ScalableCompressorAudioProcessorEditor(ScalableCompressorAudioProcessor& p)
    : AudioProcessorEditor(&p),audioProcessor(p){
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::bg_jpg, BinaryData::bg_jpgSize);
    setSize(800,600);startTimerHz(30);
    juce::Colour ac(0xFFFF6B9D);
    sk(threshSlider,ac);addAndMakeVisible(threshSlider);
    threshAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"THRESHOLD",threshSlider);
    sk(ratioSlider,ac);addAndMakeVisible(ratioSlider);
    ratioAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"RATIO",ratioSlider);
    sk(attackSlider,ac);addAndMakeVisible(attackSlider);
    attackAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"ATTACK",attackSlider);
    sk(releaseSlider,ac);addAndMakeVisible(releaseSlider);
    releaseAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"RELEASE",releaseSlider);
    sk(makeupSlider,ac);addAndMakeVisible(makeupSlider);
    makeupAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"MAKEUP",makeupSlider);
}
ScalableCompressorAudioProcessorEditor::~ScalableCompressorAudioProcessorEditor(){}
void ScalableCompressorAudioProcessorEditor::paint(juce::Graphics& g){
    if (backgroundImage.isValid()) {
        g.drawImage(backgroundImage, getLocalBounds().toFloat());
    } else {
        g.fillAll(juce::Colours::black);
    }
// GR meter
    float gr=audioProcessor.gainReduction.load();
    float grDB=20.0f*std::log10(std::max(gr,0.0001f));
    g.setColour(juce::Colour(0xFFFF6B9D).withAlpha(0.7f));
    g.fillRoundedRectangle(100,450,std::max(0.0f,(grDB+30.0f)/30.0f)*600,16,6);
    g.setColour(juce::Colours::white);g.setFont(12.0f);
    g.drawText("GR: "+juce::String(grDB,1)+" dB",100,470,200,20,juce::Justification::left);
    float rL=audioProcessor.rmsLevelLeft.load(),rR=audioProcessor.rmsLevelRight.load();
    g.setColour(juce::Colour(0xFFFF6B9D).withAlpha(0.4f));
    g.fillRoundedRectangle(100,500,rL*600,8,3);g.fillRoundedRectangle(100,512,rR*600,8,3);
}
void ScalableCompressorAudioProcessorEditor::timerCallback(){repaint();}
void ScalableCompressorAudioProcessorEditor::resized(){
    int ks=120;
    threshSlider.setBounds(50,200,ks,ks+30);ratioSlider.setBounds(195,200,ks,ks+30);
    attackSlider.setBounds(340,200,ks,ks+30);releaseSlider.setBounds(485,200,ks,ks+30);
    makeupSlider.setBounds(630,200,ks,ks+30);
}
