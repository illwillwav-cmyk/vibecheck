#pragma once
#include <JuceHeader.h>
class AntiLoudnessLimiterAudioProcessor : public juce::AudioProcessor {
public:
    AntiLoudnessLimiterAudioProcessor();
    ~AntiLoudnessLimiterAudioProcessor() override;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void prepareToPlay(double,int) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override{return true;}
    const juce::String getName() const override{return JucePlugin_Name;}
    bool acceptsMidi() const override{return false;}
    bool producesMidi() const override{return false;}
    bool isMidiEffect() const override{return false;}
    double getTailLengthSeconds() const override{return 0;}
    int getNumPrograms() override{return 1;}
    int getCurrentProgram() override{return 0;}
    void setCurrentProgram(int) override{}
    const juce::String getProgramName(int) override{return {};}
    void changeProgramName(int,const juce::String&) override{}
    void getStateInformation(juce::MemoryBlock&) override{}
    void setStateInformation(const void*,int) override{}
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> rmsLevelLeft{0},rmsLevelRight{0},gainReduction{0};
private:
    float env=0;double sr=44100;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AntiLoudnessLimiterAudioProcessor)
};
