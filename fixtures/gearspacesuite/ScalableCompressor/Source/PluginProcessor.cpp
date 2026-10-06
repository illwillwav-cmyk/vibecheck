#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
ScalableCompressorAudioProcessor::ScalableCompressorAudioProcessor()
 : AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)),
   apvts(*this,nullptr,"Parameters",createParameterLayout()){}
ScalableCompressorAudioProcessor::~ScalableCompressorAudioProcessor(){}
void ScalableCompressorAudioProcessor::prepareToPlay(double s,int){sr=s;envL=envR=0;}
void ScalableCompressorAudioProcessor::releaseResources(){}
bool ScalableCompressorAudioProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    return l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo()&&l.getMainOutputChannelSet()==l.getMainInputChannelSet();
}
void ScalableCompressorAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&){
    juce::ScopedNoDenormals noDenormals;
    float thresh = apvts.getRawParameterValue("THRESHOLD")->load();
    float ratio  = apvts.getRawParameterValue("RATIO")->load();
    float attack = std::exp(-1.0f/(float)(sr * apvts.getRawParameterValue("ATTACK")->load() * 0.001f));
    float release= std::exp(-1.0f/(float)(sr * apvts.getRawParameterValue("RELEASE")->load() * 0.001f));
    float makeup = std::pow(10.0f, apvts.getRawParameterValue("MAKEUP")->load()/20.0f);
    float threshLin = std::pow(10.0f, thresh/20.0f);
    auto* dL = buffer.getWritePointer(0);
    auto* dR = buffer.getWritePointer(getTotalNumInputChannels()>1?1:0);
    float lastGR = 1.0f;
    for(int s=0;s<buffer.getNumSamples();++s){
        float inL=std::fabs(dL[s]),inR=std::fabs(dR[s]);
        float inMax=std::max(inL,inR);
        float env=(inMax>envL)?attack*envL+(1.0f-attack)*inMax:release*envL+(1.0f-release)*inMax;
        envL=env;
        float gr=1.0f;
        if(env>threshLin) gr=threshLin*std::pow(env/threshLin,1.0f/ratio-1.0f);
        if(gr!=gr)gr=1.0f; // NaN guard
        dL[s]*=gr*makeup; dR[s]*=gr*makeup;
        lastGR=gr;
    }
    gainReduction.store(lastGR);
    rmsLevelLeft.store(buffer.getRMSLevel(0,0,buffer.getNumSamples()));
    if(getTotalNumInputChannels()>1)rmsLevelRight.store(buffer.getRMSLevel(1,0,buffer.getNumSamples()));
}
juce::AudioProcessorEditor* ScalableCompressorAudioProcessor::createEditor(){return new ScalableCompressorAudioProcessorEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new ScalableCompressorAudioProcessor();}
juce::AudioProcessorValueTreeState::ParameterLayout ScalableCompressorAudioProcessor::createParameterLayout(){
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterFloat>("THRESHOLD","Threshold",-60.0f,0.0f,-12.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("RATIO","Ratio",1.0f,20.0f,4.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("ATTACK","Attack",0.1f,100.0f,10.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("RELEASE","Release",10.0f,1000.0f,100.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("MAKEUP","Makeup",-12.0f,24.0f,0.0f));
    return {p.begin(),p.end()};
}
