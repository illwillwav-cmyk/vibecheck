#pragma once
#include <JuceHeader.h>
#include <array>

class CombFilter {
public:
    void setSize(int size) { buffer.resize(size, 0.0f); pos = 0; }
    float process(float input, float feedback, float damp) {
        float out = buffer[pos];
        filterStore = (out * (1.0f - damp)) + (filterStore * damp);
        buffer[pos] = input + (filterStore * feedback);
        if (++pos >= (int)buffer.size()) pos = 0;
        return out;
    }
    void clear() { std::fill(buffer.begin(), buffer.end(), 0.0f); filterStore = 0.0f; }
private:
    std::vector<float> buffer;
    int pos = 0;
    float filterStore = 0.0f;
};

class AllpassFilter {
public:
    void setSize(int size) { buffer.resize(size, 0.0f); pos = 0; }
    float process(float input) {
        float buffered = buffer[pos];
        buffer[pos] = input + (buffered * 0.5f);
        if (++pos >= (int)buffer.size()) pos = 0;
        return buffered - input;
    }
    void clear() { std::fill(buffer.begin(), buffer.end(), 0.0f); }
private:
    std::vector<float> buffer;
    int pos = 0;
};

class NoDongleReverbAudioProcessor : public juce::AudioProcessor
{
public:
    NoDongleReverbAudioProcessor();
    ~NoDongleReverbAudioProcessor() override;
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
    static constexpr int NUM_COMBS = 8;
    static constexpr int NUM_ALLPASSES = 4;
    std::array<CombFilter, NUM_COMBS> combL, combR;
    std::array<AllpassFilter, NUM_ALLPASSES> allpassL, allpassR;
    double currentSampleRate = 44100.0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoDongleReverbAudioProcessor)
};
