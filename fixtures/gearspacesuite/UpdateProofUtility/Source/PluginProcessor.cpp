#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
UpdateProofUtilityAudioProcessor::UpdateProofUtilityAudioProcessor()
 :AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)),
  apvts(*this,nullptr,"Parameters",createParameterLayout()){}
UpdateProofUtilityAudioProcessor::~UpdateProofUtilityAudioProcessor(){}
void UpdateProofUtilityAudioProcessor::prepareToPlay(double,int){}
void UpdateProofUtilityAudioProcessor::releaseResources(){}
bool UpdateProofUtilityAudioProcessor::isBusesLayoutSupported(const BusesLayout& l) const{
    return l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo()&&l.getMainOutputChannelSet()==l.getMainInputChannelSet();}
void UpdateProofUtilityAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&){
    juce::ScopedNoDenormals nd;
    float gain=std::pow(10.0f,apvts.getRawParameterValue("GAIN")->load()/20.0f);
    float pan=apvts.getRawParameterValue("PAN")->load();
    bool mono=apvts.getRawParameterValue("MONO")->load()>0.5f;
    bool swap=apvts.getRawParameterValue("SWAP")->load()>0.5f;
    auto*dL=buffer.getWritePointer(0);auto*dR=buffer.getWritePointer(1);
    for(int s=0;s<buffer.getNumSamples();++s){
        float l=dL[s]*gain,r=dR[s]*gain;
        if(mono){float m=(l+r)*0.5f;l=r=m;}
        if(swap){float t=l;l=r;r=t;}
        float panL=std::cos((pan+1.0f)*0.25f*juce::MathConstants<float>::pi);
        float panR=std::sin((pan+1.0f)*0.25f*juce::MathConstants<float>::pi);
        dL[s]=l*panL;dR[s]=r*panR;
    }
    rmsL.store(buffer.getRMSLevel(0,0,buffer.getNumSamples()));
    rmsR.store(buffer.getRMSLevel(1,0,buffer.getNumSamples()));
}
juce::AudioProcessorEditor* UpdateProofUtilityAudioProcessor::createEditor(){return new UpdateProofUtilityAudioProcessorEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new UpdateProofUtilityAudioProcessor();}
juce::AudioProcessorValueTreeState::ParameterLayout UpdateProofUtilityAudioProcessor::createParameterLayout(){
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterFloat>("GAIN","Gain dB",-24.0f,24.0f,0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("PAN","Pan",-1.0f,1.0f,0.0f));
    p.push_back(std::make_unique<juce::AudioParameterBool>("MONO","Mono",false));
    p.push_back(std::make_unique<juce::AudioParameterBool>("SWAP","Swap L/R",false));
    return {p.begin(),p.end()};
}
