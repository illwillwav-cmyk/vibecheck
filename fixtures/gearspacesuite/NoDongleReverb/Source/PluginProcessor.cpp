#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

static const int combTuningsL[] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
static const int combTuningsR[] = { 1139, 1211, 1300, 1379, 1445, 1514, 1580, 1640 };
static const int allpassTunings[] = { 556, 441, 341, 225 };

NoDongleReverbAudioProcessor::NoDongleReverbAudioProcessor()
     : AudioProcessor (BusesProperties()
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
       apvts(*this, nullptr, "Parameters", createParameterLayout())
{}

NoDongleReverbAudioProcessor::~NoDongleReverbAudioProcessor() {}

void NoDongleReverbAudioProcessor::prepareToPlay (double sampleRate, int) {
    currentSampleRate = sampleRate;
    float scale = (float)(sampleRate / 44100.0);
    for (int i = 0; i < NUM_COMBS; ++i) {
        combL[i].setSize((int)(combTuningsL[i] * scale));
        combR[i].setSize((int)(combTuningsR[i] * scale));
        combL[i].clear(); combR[i].clear();
    }
    for (int i = 0; i < NUM_ALLPASSES; ++i) {
        allpassL[i].setSize((int)(allpassTunings[i] * scale));
        allpassR[i].setSize((int)(allpassTunings[i] * scale));
        allpassL[i].clear(); allpassR[i].clear();
    }
}

void NoDongleReverbAudioProcessor::releaseResources() {}

bool NoDongleReverbAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const {
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
    return true;
}

void NoDongleReverbAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    float decay   = apvts.getRawParameterValue("DECAY")->load();
    float size    = apvts.getRawParameterValue("SIZE")->load();
    float damping = apvts.getRawParameterValue("DAMPING")->load();
    float mix     = apvts.getRawParameterValue("MIX")->load();

    float feedback = decay * 0.9f + 0.1f;
    feedback *= size;

    auto* dataL = buffer.getWritePointer(0);
    auto* dataR = buffer.getWritePointer(totalNumInputChannels > 1 ? 1 : 0);

    for (int s = 0; s < buffer.getNumSamples(); ++s) {
        float inL = dataL[s];
        float inR = dataR[s];
        float input = (inL + inR) * 0.5f * 0.015f;

        float outL = 0.0f, outR = 0.0f;
        for (int c = 0; c < NUM_COMBS; ++c) {
            outL += combL[c].process(input, feedback, damping);
            outR += combR[c].process(input, feedback, damping);
        }
        for (int a = 0; a < NUM_ALLPASSES; ++a) {
            outL = allpassL[a].process(outL);
            outR = allpassR[a].process(outR);
        }

        dataL[s] = inL * (1.0f - mix) + outL * mix;
        dataR[s] = inR * (1.0f - mix) + outR * mix;
    }

    rmsLevelLeft.store(buffer.getRMSLevel(0, 0, buffer.getNumSamples()));
    if (totalNumInputChannels > 1)
        rmsLevelRight.store(buffer.getRMSLevel(1, 0, buffer.getNumSamples()));
}

juce::AudioProcessorEditor* NoDongleReverbAudioProcessor::createEditor() {
    return new NoDongleReverbAudioProcessorEditor(*this);
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NoDongleReverbAudioProcessor(); }

juce::AudioProcessorValueTreeState::ParameterLayout NoDongleReverbAudioProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>("DECAY",    "Decay",     0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("SIZE",     "Size",      0.1f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("DAMPING",  "Damping",   0.0f, 1.0f, 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("DIFFUSION","Diffusion", 0.0f, 1.0f, 0.7f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("MIX",      "Wet/Dry",   0.0f, 1.0f, 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("TYPE",    "Reverb Type", juce::StringArray{"Hall","Room","Plate"}, 0));
    return { params.begin(), params.end() };
}
