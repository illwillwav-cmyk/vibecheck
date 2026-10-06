#pragma once
#include <JuceHeader.h>

class FreeDelayAudioProcessor : public juce::AudioProcessor {
public:
    FreeDelayAudioProcessor();
    ~FreeDelayAudioProcessor() override;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> rmsLevelLeft{0.0f}, rmsLevelRight{0.0f};
private:
    std::vector<float> delayBufferL, delayBufferR;
    int writePos = 0;
    double currentSampleRate = 44100.0;
    float lastFilterL = 0.0f, lastFilterR = 0.0f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FreeDelayAudioProcessor)
};
