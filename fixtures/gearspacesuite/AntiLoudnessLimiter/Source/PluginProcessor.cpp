#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
AntiLoudnessLimiterAudioProcessor::AntiLoudnessLimiterAudioProcessor()
 :AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)),
  apvts(*this,nullptr,"Parameters",createParameterLayout()){}
AntiLoudnessLimiterAudioProcessor::~AntiLoudnessLimiterAudioProcessor(){}
void AntiLoudnessLimiterAudioProcessor::prepareToPlay(double s,int){sr=s;env=0;}
void AntiLoudnessLimiterAudioProcessor::releaseResources(){}
bool AntiLoudnessLimiterAudioProcessor::isBusesLayoutSupported(const BusesLayout& l)const{
    return l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo()&&l.getMainOutputChannelSet()==l.getMainInputChannelSet();}
void AntiLoudnessLimiterAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&){
    juce::ScopedNoDenormals nd;
    float ceiling=std::pow(10.0f,apvts.getRawParameterValue("CEILING")->load()/20.0f);
    float release=std::exp(-1.0f/(float)(sr*apvts.getRawParameterValue("RELEASE")->load()*0.001f));
    float input=std::pow(10.0f,apvts.getRawParameterValue("INPUT")->load()/20.0f);
    auto*dL=buffer.getWritePointer(0);auto*dR=buffer.getWritePointer(getTotalNumInputChannels()>1?1:0);
    float lastGR=1.0f;
    for(int s=0;s<buffer.getNumSamples();++s){
        dL[s]*=input;dR[s]*=input;
        float peak=std::max(std::fabs(dL[s]),std::fabs(dR[s]));
        if(peak>env)env=peak;else env=release*env+(1.0f-release)*peak;
        float gr=1.0f;if(env>ceiling)gr=ceiling/env;
        dL[s]*=gr;dR[s]*=gr;lastGR=gr;
    }
    gainReduction.store(lastGR);
    rmsLevelLeft.store(buffer.getRMSLevel(0,0,buffer.getNumSamples()));
    if(getTotalNumInputChannels()>1)rmsLevelRight.store(buffer.getRMSLevel(1,0,buffer.getNumSamples()));
}
juce::AudioProcessorEditor* AntiLoudnessLimiterAudioProcessor::createEditor(){return new AntiLoudnessLimiterAudioProcessorEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new AntiLoudnessLimiterAudioProcessor();}
juce::AudioProcessorValueTreeState::ParameterLayout AntiLoudnessLimiterAudioProcessor::createParameterLayout(){
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterFloat>("CEILING","Ceiling",-12.0f,0.0f,-0.3f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("RELEASE","Release",10.0f,500.0f,50.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("INPUT","Input",-12.0f,24.0f,0.0f));
    return {p.begin(),p.end()};
}
