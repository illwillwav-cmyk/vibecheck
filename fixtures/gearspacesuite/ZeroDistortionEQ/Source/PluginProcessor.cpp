#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

ZeroDistortionEQAudioProcessor::ZeroDistortionEQAudioProcessor()
     : AudioProcessor (BusesProperties()
                       .withInput("Input", juce::AudioChannelSet::stereo(), true)
                       .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
       apvts(*this, nullptr, "Parameters", createParameterLayout()) {}
ZeroDistortionEQAudioProcessor::~ZeroDistortionEQAudioProcessor() {}

void ZeroDistortionEQAudioProcessor::prepareToPlay(double sr, int) {
    sampleRate = sr;
    for (auto& s : stateL) s = {};
    for (auto& s : stateR) s = {};
}
void ZeroDistortionEQAudioProcessor::releaseResources() {}
bool ZeroDistortionEQAudioProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && l.getMainOutputChannelSet() == l.getMainInputChannelSet();
}

void ZeroDistortionEQAudioProcessor::updateCoeffs() {
    const char* freqIds[] = {"FREQ1","FREQ2","FREQ3","FREQ4"};
    const char* gainIds[] = {"GAIN1","GAIN2","GAIN3","GAIN4"};
    const char* qIds[]    = {"Q1","Q2","Q3","Q4"};
    for (int i = 0; i < NUM_BANDS; ++i) {
        float freq = apvts.getRawParameterValue(freqIds[i])->load();
        float gain = apvts.getRawParameterValue(gainIds[i])->load();
        float q    = apvts.getRawParameterValue(qIds[i])->load();
        float A    = std::pow(10.0f, gain / 40.0f);
        float w0   = 2.0f * juce::MathConstants<float>::pi * freq / (float)sampleRate;
        float alpha = std::sin(w0) / (2.0f * q);
        float a0 = 1.0f + alpha / A;
        coeffs[i].a0 = (1.0f + alpha * A) / a0;
        coeffs[i].a1 = (-2.0f * std::cos(w0)) / a0;
        coeffs[i].a2 = (1.0f - alpha * A) / a0;
        coeffs[i].b1 = (-2.0f * std::cos(w0)) / a0;
        coeffs[i].b2 = (1.0f - alpha / A) / a0;
    }
}

void ZeroDistortionEQAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    updateCoeffs();
    auto* dL = buffer.getWritePointer(0);
    auto* dR = buffer.getWritePointer(getTotalNumInputChannels() > 1 ? 1 : 0);
    for (int s = 0; s < buffer.getNumSamples(); ++s) {
        float sL = dL[s], sR = dR[s];
        for (int b = 0; b < NUM_BANDS; ++b) {
            auto& c = coeffs[b];
            auto& sl = stateL[b]; auto& sr = stateR[b];
            float yL = c.a0*sL + c.a1*sl.x1 + c.a2*sl.x2 - c.b1*sl.y1 - c.b2*sl.y2;
            sl.x2=sl.x1; sl.x1=sL; sl.y2=sl.y1; sl.y1=yL; sL=yL;
            float yR = c.a0*sR + c.a1*sr.x1 + c.a2*sr.x2 - c.b1*sr.y1 - c.b2*sr.y2;
            sr.x2=sr.x1; sr.x1=sR; sr.y2=sr.y1; sr.y1=yR; sR=yR;
        }
        dL[s] = sL; dR[s] = sR;
    }
    rmsLevelLeft.store(buffer.getRMSLevel(0,0,buffer.getNumSamples()));
    if (getTotalNumInputChannels()>1) rmsLevelRight.store(buffer.getRMSLevel(1,0,buffer.getNumSamples()));
}
juce::AudioProcessorEditor* ZeroDistortionEQAudioProcessor::createEditor() { return new ZeroDistortionEQAudioProcessorEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ZeroDistortionEQAudioProcessor(); }

juce::AudioProcessorValueTreeState::ParameterLayout ZeroDistortionEQAudioProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    float defaultFreqs[] = {100,500,2000,8000};
    for (int i = 0; i < 4; ++i) {
        auto si = juce::String(i+1);
        params.push_back(std::make_unique<juce::AudioParameterFloat>("FREQ"+si,"Freq "+si,20.0f,20000.0f,defaultFreqs[i]));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("GAIN"+si,"Gain "+si,-24.0f,24.0f,0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("Q"+si,"Q "+si,0.1f,10.0f,1.0f));
    }
    return { params.begin(), params.end() };
}
