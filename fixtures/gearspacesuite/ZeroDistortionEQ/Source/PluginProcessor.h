#pragma once
#include <JuceHeader.h>
#include <array>

struct BiquadCoeffs { float a0,a1,a2,b1,b2; };
struct BiquadState { float x1=0,x2=0,y1=0,y2=0; };

class ZeroDistortionEQAudioProcessor : public juce::AudioProcessor {
public:
    ZeroDistortionEQAudioProcessor();
    ~ZeroDistortionEQAudioProcessor() override;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void prepareToPlay (double, int) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> rmsLevelLeft{0}, rmsLevelRight{0};
private:
    double sampleRate = 44100.0;
    static constexpr int NUM_BANDS = 4;
    std::array<BiquadCoeffs, NUM_BANDS> coeffs;
    std::array<BiquadState, NUM_BANDS> stateL, stateR;
    void updateCoeffs();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZeroDistortionEQAudioProcessor)
};
