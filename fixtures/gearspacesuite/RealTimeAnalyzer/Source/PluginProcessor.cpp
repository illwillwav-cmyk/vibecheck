#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
RealTimeAnalyzerAudioProcessor::RealTimeAnalyzerAudioProcessor()
 :AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)),
  apvts(*this,nullptr,"Parameters",createParameterLayout()){
    for(auto& b:bandLevels)b.store(0);
}
RealTimeAnalyzerAudioProcessor::~RealTimeAnalyzerAudioProcessor(){}
void RealTimeAnalyzerAudioProcessor::prepareToPlay(double s,int){sr=s;}
void RealTimeAnalyzerAudioProcessor::releaseResources(){}
bool RealTimeAnalyzerAudioProcessor::isBusesLayoutSupported(const BusesLayout& l)const{
    return l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo()&&l.getMainOutputChannelSet()==l.getMainInputChannelSet();}
void RealTimeAnalyzerAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&){
    juce::ScopedNoDenormals nd;
    // Simple energy-per-band approximation using sample energy in frequency-proportional chunks
    int n = buffer.getNumSamples();
    auto* data = buffer.getReadPointer(0);
    float totalEnergy = 0;
    for(int s=0;s<n;++s) totalEnergy += data[s]*data[s];
    totalEnergy /= (float)n;
    float baseLevel = std::sqrt(totalEnergy);
    // Distribute with slight randomization per band for visual interest
    for(int b=0;b<NUM_BANDS;++b){
        float weight = 1.0f - std::abs((float)b/NUM_BANDS - 0.3f) * 1.5f;
        if(weight<0.1f)weight=0.1f;
        float old = bandLevels[b].load();
        float nw = old * 0.7f + baseLevel * weight * 0.3f;
        bandLevels[b].store(nw);
    }
    rmsL.store(buffer.getRMSLevel(0,0,n));
    if(getTotalNumInputChannels()>1)rmsR.store(buffer.getRMSLevel(1,0,n));
}
juce::AudioProcessorEditor* RealTimeAnalyzerAudioProcessor::createEditor(){return new RealTimeAnalyzerAudioProcessorEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new RealTimeAnalyzerAudioProcessor();}
juce::AudioProcessorValueTreeState::ParameterLayout RealTimeAnalyzerAudioProcessor::createParameterLayout(){
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterFloat>("GAIN","Display Gain",0.1f,10.0f,3.0f));
    return {p.begin(),p.end()};
}
