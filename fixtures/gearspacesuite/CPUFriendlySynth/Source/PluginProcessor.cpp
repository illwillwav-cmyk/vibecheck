#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

class SimpleSound : public juce::SynthesiserSound {
public:
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

class SimpleVoice : public juce::SynthesiserVoice {
public:
    bool canPlaySound(juce::SynthesiserSound* s) override { return dynamic_cast<SimpleSound*>(s)!=nullptr; }
    void startNote(int midiNote, float velocity, juce::SynthesiserSound*, int) override {
        phase = 0.0; freq = juce::MidiMessage::getMidiNoteInHertz(midiNote);
        level = velocity * 0.3f; tailOff = 0.0;
    }
    void stopNote(float, bool allowTailOff) override {
        if (allowTailOff) { if (tailOff == 0.0) tailOff = 1.0; }
        else { clearCurrentNote(); level = 0; }
    }
    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}
    void renderNextBlock(juce::AudioBuffer<float>& buf, int startSample, int numSamples) override {
        if (level == 0) return;
        double sr = getSampleRate();
        for (int s = startSample; s < startSample + numSamples; ++s) {
            float val = (float)std::sin(phase * 2.0 * juce::MathConstants<double>::pi) * level;
            // Add saw harmonic
            val += (float)(std::fmod(phase, 1.0) * 2.0 - 1.0) * level * 0.3f;
            if (tailOff > 0.0) { val *= (float)tailOff; tailOff *= 0.9995;
                if (tailOff < 0.005) { clearCurrentNote(); level = 0; break; } }
            for (int ch = 0; ch < buf.getNumChannels(); ++ch) buf.addSample(ch, s, val);
            phase += freq / sr; if (phase >= 1.0) phase -= 1.0;
        }
    }
private:
    double phase = 0, freq = 440, tailOff = 0;
    float level = 0;
};

CPUFriendlySynthAudioProcessor::CPUFriendlySynthAudioProcessor()
 :AudioProcessor(BusesProperties().withOutput("Output",juce::AudioChannelSet::stereo(),true)),
  apvts(*this,nullptr,"Parameters",createParameterLayout()){
    synth.addSound(new SimpleSound());
    for(int i=0;i<8;++i)synth.addVoice(new SimpleVoice());
}
CPUFriendlySynthAudioProcessor::~CPUFriendlySynthAudioProcessor(){}
void CPUFriendlySynthAudioProcessor::prepareToPlay(double sr,int bs){synth.setCurrentPlaybackSampleRate(sr);}
void CPUFriendlySynthAudioProcessor::releaseResources(){}
bool CPUFriendlySynthAudioProcessor::isBusesLayoutSupported(const BusesLayout& l) const{
    return l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();}
void CPUFriendlySynthAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer& midi){
    buffer.clear();
    float gain=apvts.getRawParameterValue("GAIN")->load();
    synth.renderNextBlock(buffer,midi,0,buffer.getNumSamples());
    buffer.applyGain(gain);
    rmsLevel.store(buffer.getRMSLevel(0,0,buffer.getNumSamples()));
}
juce::AudioProcessorEditor* CPUFriendlySynthAudioProcessor::createEditor(){return new CPUFriendlySynthAudioProcessorEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new CPUFriendlySynthAudioProcessor();}
juce::AudioProcessorValueTreeState::ParameterLayout CPUFriendlySynthAudioProcessor::createParameterLayout(){
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterFloat>("GAIN","Gain",0.0f,2.0f,0.8f));
    return {p.begin(),p.end()};
}
