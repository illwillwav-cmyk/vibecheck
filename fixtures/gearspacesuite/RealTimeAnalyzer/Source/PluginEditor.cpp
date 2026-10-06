#include "PluginProcessor.h"
#include "PluginEditor.h"
RealTimeAnalyzerAudioProcessorEditor::RealTimeAnalyzerAudioProcessorEditor(RealTimeAnalyzerAudioProcessor& p)
    :AudioProcessorEditor(&p),audioProcessor(p){
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::bg_jpg, BinaryData::bg_jpgSize);
    setSize(800,600);startTimerHz(30);
    gainSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    gainSlider.setTextBoxStyle(juce::Slider::TextBoxRight,false,60,20);
    gainSlider.setColour(juce::Slider::trackColourId,juce::Colour(0xFF00FFB3));
    gainSlider.setColour(juce::Slider::thumbColourId,juce::Colour(0xFF00FFB3));
    gainSlider.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xFF00FFB3));
    gainSlider.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
    addAndMakeVisible(gainSlider);
    gainAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"GAIN",gainSlider);
}
RealTimeAnalyzerAudioProcessorEditor::~RealTimeAnalyzerAudioProcessorEditor(){}
void RealTimeAnalyzerAudioProcessorEditor::paint(juce::Graphics& g){
    if (backgroundImage.isValid()) {
        g.drawImage(backgroundImage, getLocalBounds().toFloat());
    } else {
        g.fillAll(juce::Colours::black);
    }

    float displayGain=audioProcessor.apvts.getRawParameterValue("GAIN")->load();
    int numBands=RealTimeAnalyzerAudioProcessor::NUM_BANDS;
    float barW=700.0f/numBands-2;
    for(int b=0;b<numBands;++b){
        float level=audioProcessor.bandLevels[b].load()*displayGain;
        float h=std::min(level*400.0f,350.0f);
        float x=50+b*(barW+2);
        juce::Colour barCol=juce::Colour::fromHSV((float)b/numBands*0.4f,0.8f,0.9f,1.0f);
        g.setColour(barCol);
        g.fillRoundedRectangle(x,480-h,barW,h,3);
        g.setColour(barCol.withAlpha(0.3f));
        g.fillRoundedRectangle(x,480-h-5,barW,5,2);
    }
    g.setColour(juce::Colour(0xFF00FFB3).withAlpha(0.3f));
    g.drawLine(50,480,750,480,1);
}
void RealTimeAnalyzerAudioProcessorEditor::timerCallback(){repaint();}
void RealTimeAnalyzerAudioProcessorEditor::resized(){
    gainSlider.setBounds(250,540,300,25);
}
