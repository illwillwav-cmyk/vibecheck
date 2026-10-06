#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

GainMatchedSatAudioProcessor::GainMatchedSatAudioProcessor()
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ), apvts(*this, nullptr, "Parameters", createParameterLayout())
{
}

GainMatchedSatAudioProcessor::~GainMatchedSatAudioProcessor() {}

void GainMatchedSatAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock) {}
void GainMatchedSatAudioProcessor::releaseResources() {}

bool GainMatchedSatAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif
    return true;
  #endif
}

void GainMatchedSatAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i) buffer.clear (i, 0, buffer.getNumSamples());
    
    
    float drive = apvts.getRawParameterValue("DRIVE")->load();
    float mix = apvts.getRawParameterValue("MIX")->load();
    float output = apvts.getRawParameterValue("OUTPUT")->load();
    float power = apvts.getRawParameterValue("POWER")->load();
    float style = apvts.getRawParameterValue("STYLE")->load();

    if (power < 0.5f) {
        for (int channel = 0; channel < totalNumInputChannels; ++channel) {
            float rms = buffer.getRMSLevel(channel, 0, buffer.getNumSamples());
            if (channel == 0) rmsLevelLeft.store(rms);
            if (channel == 1) rmsLevelRight.store(rms);
        }
        return;
    }

    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            float clean = channelData[sample];
            float driven = clean;
            
            if (style < 0.5f) {
                driven = std::tanh(clean * drive) * (1.0f / drive);
            } else if (style < 1.5f) {
                driven = std::atan(clean * drive * 1.5f) * 0.6366f * (1.0f / drive);
            } else {
                float d = clean * drive;
                if (d > 0) driven = std::tanh(d);
                else driven = std::tanh(d * 0.5f) * 2.0f;
                driven *= (1.0f / drive);
            }

            channelData[sample] = ((clean * (1.0f - mix)) + (driven * mix)) * output;
        }
        float rms = buffer.getRMSLevel(channel, 0, buffer.getNumSamples());
        if (channel == 0) rmsLevelLeft.store(rms);
        if (channel == 1) rmsLevelRight.store(rms);
    }





    
}

juce::AudioProcessorEditor* GainMatchedSatAudioProcessor::createEditor()
{
    return new GainMatchedSatAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GainMatchedSatAudioProcessor();
}

juce::AudioProcessorValueTreeState::ParameterLayout GainMatchedSatAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>("DRIVE", "Drive", 1.0f, 10.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("MIX", "Mix", 0.0f, 1.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("OUTPUT", "Output", 0.0f, 2.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("POWER", "Power", true));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("STYLE", "Style", juce::StringArray{"Tube", "Tape", "Germ"}, 0));
    return { params.begin(), params.end() };
}
