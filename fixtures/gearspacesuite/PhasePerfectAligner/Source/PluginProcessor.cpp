#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
PhasePerfectAlignerAudioProcessor::PhasePerfectAlignerAudioProcessor()
 :AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)),
  apvts(*this,nullptr,"Parameters",createParameterLayout()){}
PhasePerfectAlignerAudioProcessor::~PhasePerfectAlignerAudioProcessor(){}
void PhasePerfectAlignerAudioProcessor::prepareToPlay(double s,int){
    sr=s;int mx=(int)(s*0.05);delayBufL.resize(mx,0);delayBufR.resize(mx,0);writePos=0;}
void PhasePerfectAlignerAudioProcessor::releaseResources(){}
bool PhasePerfectAlignerAudioProcessor::isBusesLayoutSupported(const BusesLayout& l) const{
    return l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo()&&l.getMainOutputChannelSet()==l.getMainInputChannelSet();}
void PhasePerfectAlignerAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&){
    juce::ScopedNoDenormals nd;
    bool flipL=apvts.getRawParameterValue("FLIPL")->load()>0.5f;
    bool flipR=apvts.getRawParameterValue("FLIPR")->load()>0.5f;
    int delaySamples=(int)(apvts.getRawParameterValue("DELAY")->load()*sr*0.001f);
    if(delaySamples>=(int)delayBufL.size())delaySamples=(int)delayBufL.size()-1;
    auto*dL=buffer.getWritePointer(0);auto*dR=buffer.getWritePointer(1);
    float corrSum=0;
    for(int s=0;s<buffer.getNumSamples();++s){
        if(flipL)dL[s]=-dL[s];if(flipR)dR[s]=-dR[s];
        delayBufL[writePos]=dL[s];delayBufR[writePos]=dR[s];
        int rp=writePos-delaySamples;if(rp<0)rp+=(int)delayBufL.size();
        dR[s]=delayBufR[rp];
        corrSum+=dL[s]*dR[s];
        writePos++;if(writePos>=(int)delayBufL.size())writePos=0;
    }
    correlation.store(corrSum/(float)buffer.getNumSamples());
    rmsL.store(buffer.getRMSLevel(0,0,buffer.getNumSamples()));
    rmsR.store(buffer.getRMSLevel(1,0,buffer.getNumSamples()));
}
juce::AudioProcessorEditor* PhasePerfectAlignerAudioProcessor::createEditor(){return new PhasePerfectAlignerAudioProcessorEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new PhasePerfectAlignerAudioProcessor();}
juce::AudioProcessorValueTreeState::ParameterLayout PhasePerfectAlignerAudioProcessor::createParameterLayout(){
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterBool>("FLIPL","Flip L",false));
    p.push_back(std::make_unique<juce::AudioParameterBool>("FLIPR","Flip R",false));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("DELAY","Delay ms",0.0f,50.0f,0.0f));
    return {p.begin(),p.end()};
}
