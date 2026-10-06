#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

FreeDelayAudioProcessor::FreeDelayAudioProcessor()
     : AudioProcessor (BusesProperties()
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
       apvts(*this, nullptr, "Parameters", createParameterLayout()) {}

FreeDelayAudioProcessor::~FreeDelayAudioProcessor() {}

void FreeDelayAudioProcessor::prepareToPlay (double sampleRate, int) {
    currentSampleRate = sampleRate;
    int maxDelay = (int)(sampleRate * 2.0);
    delayBufferL.resize(maxDelay, 0.0f);
    delayBufferR.resize(maxDelay, 0.0f);
    writePos = 0;
    lastFilterL = lastFilterR = 0.0f;
}
void FreeDelayAudioProcessor::releaseResources() {}

bool FreeDelayAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const {
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet()) return false;
    return true;
}

void FreeDelayAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    auto totalIn = getTotalNumInputChannels();
    auto totalOut = getTotalNumOutputChannels();
    for (auto i = totalIn; i < totalOut; ++i) buffer.clear(i, 0, buffer.getNumSamples());

    float time     = apvts.getRawParameterValue("TIME")->load();
    float feedback = apvts.getRawParameterValue("FEEDBACK")->load();
    float mix      = apvts.getRawParameterValue("MIX")->load();
    float tone     = apvts.getRawParameterValue("TONE")->load();

    int delaySamples = (int)(time * currentSampleRate);
    if (delaySamples < 1) delaySamples = 1;
    if (delaySamples >= (int)delayBufferL.size()) delaySamples = (int)delayBufferL.size() - 1;

    auto* dataL = buffer.getWritePointer(0);
    auto* dataR = buffer.getWritePointer(totalIn > 1 ? 1 : 0);

    for (int s = 0; s < buffer.getNumSamples(); ++s) {
        int readPos = writePos - delaySamples;
        if (readPos < 0) readPos += (int)delayBufferL.size();

        float delL = delayBufferL[readPos];
        float delR = delayBufferR[readPos];

        // Lowpass filter on feedback
        lastFilterL = lastFilterL + tone * (delL - lastFilterL);
        lastFilterR = lastFilterR + tone * (delR - lastFilterR);

        delayBufferL[writePos] = dataL[s] + lastFilterL * feedback;
        delayBufferR[writePos] = dataR[s] + lastFilterR * feedback;

        dataL[s] = dataL[s] * (1.0f - mix) + delL * mix;
        dataR[s] = dataR[s] * (1.0f - mix) + delR * mix;

        writePos++;
        if (writePos >= (int)delayBufferL.size()) writePos = 0;
    }
    rmsLevelLeft.store(buffer.getRMSLevel(0, 0, buffer.getNumSamples()));
    if (totalIn > 1) rmsLevelRight.store(buffer.getRMSLevel(1, 0, buffer.getNumSamples()));
}

juce::AudioProcessorEditor* FreeDelayAudioProcessor::createEditor() { return new FreeDelayAudioProcessorEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FreeDelayAudioProcessor(); }

juce::AudioProcessorValueTreeState::ParameterLayout FreeDelayAudioProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>("TIME",     "Time",     0.01f, 2.0f, 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("FEEDBACK", "Feedback", 0.0f,  0.95f, 0.4f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("MIX",      "Mix",      0.0f,  1.0f, 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("TONE",     "Tone",     0.01f, 1.0f, 0.5f));
    return { params.begin(), params.end() };
}
