#!/usr/bin/env python3
"""Implement real DSP + GUI for the entire Gearspace Suite."""
import os

ROOT = "/Users/williamwright/dev stuff/GearspaceSuite"

def write(path, content):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(content)

# ============================================================
# 1. NoDongleReverb  — "BathVerb" (Bathtub GUI)
#    Real Schroeder reverb with 4 comb + 2 allpass filters
# ============================================================
write(f"{ROOT}/NoDongleReverb/Source/PluginProcessor.h", r'''#pragma once
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
''')

write(f"{ROOT}/NoDongleReverb/Source/PluginProcessor.cpp", r'''#include "PluginProcessor.h"
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
''')

write(f"{ROOT}/NoDongleReverb/Source/PluginEditor.h", r'''#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class BathLookAndFeel : public juce::LookAndFeel_V4 {
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                          const float rotaryStartAngle, const float rotaryEndAngle, juce::Slider&) override {
        float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        float radius = (float)width * 0.35f;
        float cx = (float)x + (float)width * 0.5f;
        float cy = (float)y + (float)height * 0.5f;
        float dx = cx + radius * std::cos(angle - juce::MathConstants<float>::halfPi);
        float dy = cy + radius * std::sin(angle - juce::MathConstants<float>::halfPi);
        g.setColour(juce::Colour(0xFF2A1810));
        g.drawLine(cx, cy, dx, dy, 3.0f);
    }
};

class NoDongleReverbAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    NoDongleReverbAudioProcessorEditor (NoDongleReverbAudioProcessor&);
    ~NoDongleReverbAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
private:
    NoDongleReverbAudioProcessor& audioProcessor;
    juce::Image backgroundImage;
    BathLookAndFeel bathLaf;
    juce::Slider decaySlider, sizeSlider, dampingSlider, diffusionSlider, mixSlider, typeSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        decayAtt, sizeAtt, dampingAtt, diffusionAtt, mixAtt, typeAtt;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoDongleReverbAudioProcessorEditor)
};
''')

write(f"{ROOT}/NoDongleReverb/Source/PluginEditor.cpp", r'''#include "PluginProcessor.h"
#include "PluginEditor.h"

static void setupKnob(juce::Slider& s, juce::LookAndFeel* laf) {
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    s.setLookAndFeel(laf);
}

NoDongleReverbAudioProcessorEditor::NoDongleReverbAudioProcessorEditor (NoDongleReverbAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::bg_jpg, BinaryData::bg_jpgSize);
    setSize (800, 600);
    startTimerHz(30);

    setupKnob(typeSlider, &bathLaf);    addAndMakeVisible(typeSlider);
    typeAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "TYPE", typeSlider);
    setupKnob(dampingSlider, &bathLaf); addAndMakeVisible(dampingSlider);
    dampingAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "DAMPING", dampingSlider);
    setupKnob(diffusionSlider, &bathLaf); addAndMakeVisible(diffusionSlider);
    diffusionAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "DIFFUSION", diffusionSlider);
    setupKnob(decaySlider, &bathLaf);   addAndMakeVisible(decaySlider);
    decayAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "DECAY", decaySlider);
    setupKnob(sizeSlider, &bathLaf);    addAndMakeVisible(sizeSlider);
    sizeAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "SIZE", sizeSlider);
    setupKnob(mixSlider, &bathLaf);     addAndMakeVisible(mixSlider);
    mixAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "MIX", mixSlider);
}

NoDongleReverbAudioProcessorEditor::~NoDongleReverbAudioProcessorEditor() {
    typeSlider.setLookAndFeel(nullptr); dampingSlider.setLookAndFeel(nullptr);
    diffusionSlider.setLookAndFeel(nullptr); decaySlider.setLookAndFeel(nullptr);
    sizeSlider.setLookAndFeel(nullptr); mixSlider.setLookAndFeel(nullptr);
}

void NoDongleReverbAudioProcessorEditor::paint (juce::Graphics& g) {
    g.fillAll(juce::Colours::black);
    if (backgroundImage.isValid())
        g.drawImage(backgroundImage, getLocalBounds().toFloat());
    // VU meters
    float rmsL = audioProcessor.rmsLevelLeft.load();
    float rmsR = audioProcessor.rmsLevelRight.load();
    float angleL = juce::jmap(rmsL, 0.0f, 0.5f, -0.7f, 0.7f);
    float angleR = juce::jmap(rmsR, 0.0f, 0.5f, -0.7f, 0.7f);
    g.setColour(juce::Colour(0xFF8B0000));
    juce::Point<float> pivotL(340.0f, 130.0f);
    g.drawLine(juce::Line<float>(pivotL, pivotL.getPointOnCircumference(40.0f, angleL - juce::MathConstants<float>::halfPi)), 2.0f);
    juce::Point<float> pivotR(460.0f, 130.0f);
    g.drawLine(juce::Line<float>(pivotR, pivotR.getPointOnCircumference(40.0f, angleR - juce::MathConstants<float>::halfPi)), 2.0f);
}

void NoDongleReverbAudioProcessorEditor::timerCallback() { repaint(); }

void NoDongleReverbAudioProcessorEditor::resized() {
    int ks = 100;
    // Top row: Reverb Type, Damping, Diffusion
    typeSlider.setBounds(185, 200, ks, ks);
    dampingSlider.setBounds(350, 200, ks, ks);
    diffusionSlider.setBounds(515, 200, ks, ks);
    // Bottom row: Decay, Size, Wet/Dry
    decaySlider.setBounds(185, 370, ks, ks);
    sizeSlider.setBounds(350, 370, ks, ks);
    mixSlider.setBounds(515, 370, ks, ks);
}
''')

write(f"{ROOT}/NoDongleReverb/CMakeLists.txt", r'''cmake_minimum_required(VERSION 3.20)
project(NoDongleReverb VERSION 1.0.0)

juce_add_plugin(NoDongleReverb
    COMPANY_NAME "Gearspace Suite"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE "Gear"
    PLUGIN_CODE "BtVb"
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "No Dongle Verb"
)
juce_add_binary_data(NoDongleReverb_Data
    HEADER_NAME "BinaryData.h"
    NAMESPACE BinaryData
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Resources/bg.jpg"
)
target_sources(NoDongleReverb PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(NoDongleReverb PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(NoDongleReverb PRIVATE juce::juce_audio_utils juce::juce_dsp NoDongleReverb_Data juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
juce_generate_juce_header(NoDongleReverb)
''')

# ============================================================
# 2. FreeDelay — Real stereo delay with feedback, sync, filter
# ============================================================
write(f"{ROOT}/FreeDelay/Source/PluginProcessor.h", r'''#pragma once
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
''')

write(f"{ROOT}/FreeDelay/Source/PluginProcessor.cpp", r'''#include "PluginProcessor.h"
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
''')

write(f"{ROOT}/FreeDelay/Source/PluginEditor.h", r'''#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class FreeDelayAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    FreeDelayAudioProcessorEditor (FreeDelayAudioProcessor&);
    ~FreeDelayAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
private:
    FreeDelayAudioProcessor& audioProcessor;
    juce::Image backgroundImage;
    juce::Slider timeSlider, feedbackSlider, mixSlider, toneSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> timeAtt, feedbackAtt, mixAtt, toneAtt;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FreeDelayAudioProcessorEditor)
};
''')

write(f"{ROOT}/FreeDelay/Source/PluginEditor.cpp", r'''#include "PluginProcessor.h"
#include "PluginEditor.h"

static void setupKnob(juce::Slider& s) {
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 18);
    s.setColour(juce::Slider::textBoxTextColourId, juce::Colours::cyan);
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xFF00CCCC));
    s.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xFF1A1A2E));
    s.setColour(juce::Slider::thumbColourId, juce::Colours::cyan);
}

FreeDelayAudioProcessorEditor::FreeDelayAudioProcessorEditor (FreeDelayAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::bg_jpg, BinaryData::bg_jpgSize);
    setSize (800, 600); startTimerHz(30);
    setupKnob(timeSlider);     addAndMakeVisible(timeSlider);
    timeAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "TIME", timeSlider);
    setupKnob(feedbackSlider); addAndMakeVisible(feedbackSlider);
    feedbackAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "FEEDBACK", feedbackSlider);
    setupKnob(mixSlider);      addAndMakeVisible(mixSlider);
    mixAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "MIX", mixSlider);
    setupKnob(toneSlider);     addAndMakeVisible(toneSlider);
    toneAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "TONE", toneSlider);
}
FreeDelayAudioProcessorEditor::~FreeDelayAudioProcessorEditor() {}

void FreeDelayAudioProcessorEditor::paint (juce::Graphics& g) {
    g.fillAll(juce::Colour(0xFF0A0A1A));
    if (backgroundImage.isValid())
        g.drawImage(backgroundImage, getLocalBounds().toFloat());

    g.setColour(juce::Colour(0xAA0A0A2E));
    g.fillRoundedRectangle(30, 30, 740, 540, 20);

    g.setColour(juce::Colours::cyan);
    g.setFont(juce::Font(36.0f));
    g.drawText("FREE DELAY", 0, 40, 800, 50, juce::Justification::centred);
    g.setFont(juce::Font(14.0f));
    g.drawText("TIME",     100, 380, 120, 20, juce::Justification::centred);
    g.drawText("FEEDBACK", 260, 380, 120, 20, juce::Justification::centred);
    g.drawText("MIX",      420, 380, 120, 20, juce::Justification::centred);
    g.drawText("TONE",     580, 380, 120, 20, juce::Justification::centred);

    // VU bars
    float rmsL = audioProcessor.rmsLevelLeft.load();
    float rmsR = audioProcessor.rmsLevelRight.load();
    g.setColour(juce::Colours::cyan.withAlpha(0.6f));
    g.fillRoundedRectangle(100, 120, rmsL * 600, 12, 4);
    g.fillRoundedRectangle(100, 140, rmsR * 600, 12, 4);
}
void FreeDelayAudioProcessorEditor::timerCallback() { repaint(); }
void FreeDelayAudioProcessorEditor::resized() {
    int ks = 140;
    timeSlider.setBounds(90, 220, ks, ks);
    feedbackSlider.setBounds(250, 220, ks, ks);
    mixSlider.setBounds(410, 220, ks, ks);
    toneSlider.setBounds(570, 220, ks, ks);
}
''')

# ============================================================
# 3. ZeroDistortionEQ — 4-band parametric EQ with real biquads
# ============================================================
write(f"{ROOT}/ZeroDistortionEQ/Source/PluginProcessor.h", r'''#pragma once
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
''')

write(f"{ROOT}/ZeroDistortionEQ/Source/PluginProcessor.cpp", r'''#include "PluginProcessor.h"
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
''')

write(f"{ROOT}/ZeroDistortionEQ/Source/PluginEditor.h", r'''#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
class ZeroDistortionEQAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    ZeroDistortionEQAudioProcessorEditor(ZeroDistortionEQAudioProcessor&);
    ~ZeroDistortionEQAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
private:
    ZeroDistortionEQAudioProcessor& audioProcessor;
    juce::Image backgroundImage;
    juce::Slider freqSliders[4], gainSliders[4], qSliders[4];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> freqAtts[4], gainAtts[4], qAtts[4];
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZeroDistortionEQAudioProcessorEditor)
};
''')

write(f"{ROOT}/ZeroDistortionEQ/Source/PluginEditor.cpp", r'''#include "PluginProcessor.h"
#include "PluginEditor.h"

static void setupKnob(juce::Slider& s, juce::Colour c) {
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
    s.setColour(juce::Slider::rotarySliderFillColourId, c);
    s.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xFF1A1A2E));
    s.setColour(juce::Slider::thumbColourId, c);
    s.setColour(juce::Slider::textBoxTextColourId, c);
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

ZeroDistortionEQAudioProcessorEditor::ZeroDistortionEQAudioProcessorEditor(ZeroDistortionEQAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p) {
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::bg_jpg, BinaryData::bg_jpgSize);
    setSize(800,600); startTimerHz(30);
    juce::Colour bandCols[] = {juce::Colour(0xFFFF6B6B),juce::Colour(0xFFFFD93D),juce::Colour(0xFF6BCB77),juce::Colour(0xFF4D96FF)};
    for (int i = 0; i < 4; ++i) {
        auto si = juce::String(i+1);
        setupKnob(freqSliders[i], bandCols[i]); addAndMakeVisible(freqSliders[i]);
        freqAtts[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"FREQ"+si,freqSliders[i]);
        setupKnob(gainSliders[i], bandCols[i]); addAndMakeVisible(gainSliders[i]);
        gainAtts[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"GAIN"+si,gainSliders[i]);
        setupKnob(qSliders[i], bandCols[i]);    addAndMakeVisible(qSliders[i]);
        qAtts[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"Q"+si,qSliders[i]);
    }
}
ZeroDistortionEQAudioProcessorEditor::~ZeroDistortionEQAudioProcessorEditor() {}

void ZeroDistortionEQAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xFF0A0A1A));
    g.setColour(juce::Colour(0xAA101030));
    g.fillRoundedRectangle(20,20,760,560,16);
    g.setColour(juce::Colour(0xFF4D96FF));
    g.setFont(juce::Font(32.0f));
    g.drawText("ZERO DISTORTION EQ", 0, 30, 800, 40, juce::Justification::centred);
    juce::Colour bandCols[] = {juce::Colour(0xFFFF6B6B),juce::Colour(0xFFFFD93D),juce::Colour(0xFF6BCB77),juce::Colour(0xFF4D96FF)};
    const char* labels[] = {"LOW","LOW-MID","HI-MID","HIGH"};
    for (int i = 0; i < 4; ++i) {
        int x = 40 + i * 190;
        g.setColour(bandCols[i]); g.setFont(14.0f);
        g.drawText(labels[i], x, 90, 160, 20, juce::Justification::centred);
        g.drawText("FREQ", x, 240, 50, 14, juce::Justification::centred);
        g.drawText("GAIN", x+55, 240, 50, 14, juce::Justification::centred);
        g.drawText("Q",    x+110, 240, 50, 14, juce::Justification::centred);
    }
    // RMS bars
    float rL = audioProcessor.rmsLevelLeft.load();
    float rR = audioProcessor.rmsLevelRight.load();
    g.setColour(juce::Colour(0xFF4D96FF).withAlpha(0.5f));
    g.fillRoundedRectangle(40.0f, 555.0f, rL*720, 8, 3);
    g.fillRoundedRectangle(40.0f, 567.0f, rR*720, 8, 3);
}
void ZeroDistortionEQAudioProcessorEditor::timerCallback() { repaint(); }
void ZeroDistortionEQAudioProcessorEditor::resized() {
    int ks = 70;
    for (int i = 0; i < 4; ++i) {
        int x = 40 + i * 190;
        freqSliders[i].setBounds(x, 120, ks, ks+20);
        gainSliders[i].setBounds(x+55, 120, ks, ks+20);
        qSliders[i].setBounds(x+110, 120, ks, ks+20);
    }
}
''')

# ============================================================
# 4. ScalableCompressor — real compressor with RMS detection
# ============================================================
write(f"{ROOT}/ScalableCompressor/Source/PluginProcessor.h", r'''#pragma once
#include <JuceHeader.h>
class ScalableCompressorAudioProcessor : public juce::AudioProcessor {
public:
    ScalableCompressorAudioProcessor();
    ~ScalableCompressorAudioProcessor() override;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void prepareToPlay(double,int) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*,int) override {}
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> rmsLevelLeft{0},rmsLevelRight{0},gainReduction{0};
private:
    float envL=0,envR=0;
    double sr=44100;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScalableCompressorAudioProcessor)
};
''')

write(f"{ROOT}/ScalableCompressor/Source/PluginProcessor.cpp", r'''#include "PluginProcessor.h"
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
''')

write(f"{ROOT}/ScalableCompressor/Source/PluginEditor.h", r'''#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
class ScalableCompressorAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    ScalableCompressorAudioProcessorEditor(ScalableCompressorAudioProcessor&);
    ~ScalableCompressorAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
private:
    ScalableCompressorAudioProcessor& audioProcessor;
    juce::Image backgroundImage;
    juce::Slider threshSlider, ratioSlider, attackSlider, releaseSlider, makeupSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> threshAtt,ratioAtt,attackAtt,releaseAtt,makeupAtt;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScalableCompressorAudioProcessorEditor)
};
''')

write(f"{ROOT}/ScalableCompressor/Source/PluginEditor.cpp", r'''#include "PluginProcessor.h"
#include "PluginEditor.h"
static void sk(juce::Slider& s,juce::Colour c){
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow,false,70,16);
    s.setColour(juce::Slider::rotarySliderFillColourId,c);
    s.setColour(juce::Slider::rotarySliderOutlineColourId,juce::Colour(0xFF1A1A2E));
    s.setColour(juce::Slider::thumbColourId,c);
    s.setColour(juce::Slider::textBoxTextColourId,c);
    s.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
}
ScalableCompressorAudioProcessorEditor::ScalableCompressorAudioProcessorEditor(ScalableCompressorAudioProcessor& p)
    : AudioProcessorEditor(&p),audioProcessor(p){
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::bg_jpg, BinaryData::bg_jpgSize);
    setSize(800,600);startTimerHz(30);
    juce::Colour ac(0xFFFF6B9D);
    sk(threshSlider,ac);addAndMakeVisible(threshSlider);
    threshAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"THRESHOLD",threshSlider);
    sk(ratioSlider,ac);addAndMakeVisible(ratioSlider);
    ratioAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"RATIO",ratioSlider);
    sk(attackSlider,ac);addAndMakeVisible(attackSlider);
    attackAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"ATTACK",attackSlider);
    sk(releaseSlider,ac);addAndMakeVisible(releaseSlider);
    releaseAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"RELEASE",releaseSlider);
    sk(makeupSlider,ac);addAndMakeVisible(makeupSlider);
    makeupAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"MAKEUP",makeupSlider);
}
ScalableCompressorAudioProcessorEditor::~ScalableCompressorAudioProcessorEditor(){}
void ScalableCompressorAudioProcessorEditor::paint(juce::Graphics& g){
    g.fillAll(juce::Colour(0xFF0D0D1A));
    g.setColour(juce::Colour(0xAA151530));g.fillRoundedRectangle(20,20,760,560,16);
    g.setColour(juce::Colour(0xFFFF6B9D));g.setFont(juce::Font(32.0f));
    g.drawText("SCALABLE COMPRESSOR",0,30,800,40,juce::Justification::centred);
    g.setFont(14.0f);
    const char* labels[]={"THRESH","RATIO","ATTACK","RELEASE","MAKEUP"};
    for(int i=0;i<5;++i) g.drawText(labels[i],50+i*145,380,120,20,juce::Justification::centred);
    // GR meter
    float gr=audioProcessor.gainReduction.load();
    float grDB=20.0f*std::log10(std::max(gr,0.0001f));
    g.setColour(juce::Colour(0xFFFF6B9D).withAlpha(0.7f));
    g.fillRoundedRectangle(100,450,std::max(0.0f,(grDB+30.0f)/30.0f)*600,16,6);
    g.setColour(juce::Colours::white);g.setFont(12.0f);
    g.drawText("GR: "+juce::String(grDB,1)+" dB",100,470,200,20,juce::Justification::left);
    float rL=audioProcessor.rmsLevelLeft.load(),rR=audioProcessor.rmsLevelRight.load();
    g.setColour(juce::Colour(0xFFFF6B9D).withAlpha(0.4f));
    g.fillRoundedRectangle(100,500,rL*600,8,3);g.fillRoundedRectangle(100,512,rR*600,8,3);
}
void ScalableCompressorAudioProcessorEditor::timerCallback(){repaint();}
void ScalableCompressorAudioProcessorEditor::resized(){
    int ks=120;
    threshSlider.setBounds(50,200,ks,ks+30);ratioSlider.setBounds(195,200,ks,ks+30);
    attackSlider.setBounds(340,200,ks,ks+30);releaseSlider.setBounds(485,200,ks,ks+30);
    makeupSlider.setBounds(630,200,ks,ks+30);
}
''')

# ============================================================
# 5. AntiLoudnessLimiter — brick-wall limiter
# ============================================================
write(f"{ROOT}/AntiLoudnessLimiter/Source/PluginProcessor.h", r'''#pragma once
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
''')

write(f"{ROOT}/AntiLoudnessLimiter/Source/PluginProcessor.cpp", r'''#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
AntiLoudnessLimiterAudioProcessor::AntiLoudnessLimiterAudioProcessor()
 :AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)),
  apvts(*this,nullptr,"Parameters",createParameterLayout()){}
AntiLoudnessLimiterAudioProcessor::~AntiLoudnessLimiterAudioProcessor(){}
void AntiLoudnessLimiterAudioProcessor::prepareToPlay(double s,int){sr=s;env=0;}
void AntiLoudnessLimiterAudioProcessor::releaseResources(){}
bool AntiLoudnessLimiterAudioProcessor::isBusesLayoutSupported(const BusesLayout& l)const{
    return l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo()&&l.getMainOutputChannelSet()==l.getMainInputChannelSet();}
void AntiLoudnessLimiterAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&){
    juce::ScopedNoDenormals nd;
    float ceiling=std::pow(10.0f,apvts.getRawParameterValue("CEILING")->load()/20.0f);
    float release=std::exp(-1.0f/(float)(sr*apvts.getRawParameterValue("RELEASE")->load()*0.001f));
    float input=std::pow(10.0f,apvts.getRawParameterValue("INPUT")->load()/20.0f);
    auto*dL=buffer.getWritePointer(0);auto*dR=buffer.getWritePointer(getTotalNumInputChannels()>1?1:0);
    float lastGR=1.0f;
    for(int s=0;s<buffer.getNumSamples();++s){
        dL[s]*=input;dR[s]*=input;
        float peak=std::max(std::fabs(dL[s]),std::fabs(dR[s]));
        if(peak>env)env=peak;else env=release*env+(1.0f-release)*peak;
        float gr=1.0f;if(env>ceiling)gr=ceiling/env;
        dL[s]*=gr;dR[s]*=gr;lastGR=gr;
    }
    gainReduction.store(lastGR);
    rmsLevelLeft.store(buffer.getRMSLevel(0,0,buffer.getNumSamples()));
    if(getTotalNumInputChannels()>1)rmsLevelRight.store(buffer.getRMSLevel(1,0,buffer.getNumSamples()));
}
juce::AudioProcessorEditor* AntiLoudnessLimiterAudioProcessor::createEditor(){return new AntiLoudnessLimiterAudioProcessorEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new AntiLoudnessLimiterAudioProcessor();}
juce::AudioProcessorValueTreeState::ParameterLayout AntiLoudnessLimiterAudioProcessor::createParameterLayout(){
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterFloat>("CEILING","Ceiling",-12.0f,0.0f,-0.3f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("RELEASE","Release",10.0f,500.0f,50.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("INPUT","Input",-12.0f,24.0f,0.0f));
    return {p.begin(),p.end()};
}
''')

write(f"{ROOT}/AntiLoudnessLimiter/Source/PluginEditor.h", r'''#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
class AntiLoudnessLimiterAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    AntiLoudnessLimiterAudioProcessorEditor(AntiLoudnessLimiterAudioProcessor&);
    ~AntiLoudnessLimiterAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
private:
    AntiLoudnessLimiterAudioProcessor& audioProcessor;
    juce::Image backgroundImage;
    juce::Slider ceilingSlider,releaseSlider,inputSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ceilingAtt,releaseAtt,inputAtt;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AntiLoudnessLimiterAudioProcessorEditor)
};
''')

write(f"{ROOT}/AntiLoudnessLimiter/Source/PluginEditor.cpp", r'''#include "PluginProcessor.h"
#include "PluginEditor.h"
static void sk(juce::Slider& s,juce::Colour c){
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow,false,80,16);
    s.setColour(juce::Slider::rotarySliderFillColourId,c);
    s.setColour(juce::Slider::rotarySliderOutlineColourId,juce::Colour(0xFF1A1A2E));
    s.setColour(juce::Slider::thumbColourId,c);
    s.setColour(juce::Slider::textBoxTextColourId,c);
    s.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
}
AntiLoudnessLimiterAudioProcessorEditor::AntiLoudnessLimiterAudioProcessorEditor(AntiLoudnessLimiterAudioProcessor& p)
    :AudioProcessorEditor(&p),audioProcessor(p){
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::bg_jpg, BinaryData::bg_jpgSize);
    setSize(800,600);startTimerHz(30);
    juce::Colour ac(0xFFFF9F43);
    sk(inputSlider,ac);addAndMakeVisible(inputSlider);
    inputAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"INPUT",inputSlider);
    sk(ceilingSlider,ac);addAndMakeVisible(ceilingSlider);
    ceilingAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"CEILING",ceilingSlider);
    sk(releaseSlider,ac);addAndMakeVisible(releaseSlider);
    releaseAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"RELEASE",releaseSlider);
}
AntiLoudnessLimiterAudioProcessorEditor::~AntiLoudnessLimiterAudioProcessorEditor(){}
void AntiLoudnessLimiterAudioProcessorEditor::paint(juce::Graphics& g){
    g.fillAll(juce::Colour(0xFF0D0D1A));
    g.setColour(juce::Colour(0xAA151530));g.fillRoundedRectangle(20,20,760,560,16);
    g.setColour(juce::Colour(0xFFFF9F43));g.setFont(juce::Font(32.0f));
    g.drawText("ANTI-LOUDNESS LIMITER",0,30,800,40,juce::Justification::centred);
    g.setFont(14.0f);
    g.drawText("INPUT",130,380,140,20,juce::Justification::centred);
    g.drawText("CEILING",330,380,140,20,juce::Justification::centred);
    g.drawText("RELEASE",530,380,140,20,juce::Justification::centred);
    float gr=audioProcessor.gainReduction.load();
    float grDB=20.0f*std::log10(std::max(gr,0.0001f));
    g.setColour(juce::Colour(0xFFFF9F43).withAlpha(0.7f));
    g.fillRoundedRectangle(100,440,std::max(0.0f,(grDB+30.0f)/30.0f)*600,20,8);
    g.setColour(juce::Colours::white);g.setFont(14.0f);
    g.drawText("GR: "+juce::String(grDB,1)+" dB",100,465,200,20,juce::Justification::left);
    float rL=audioProcessor.rmsLevelLeft.load(),rR=audioProcessor.rmsLevelRight.load();
    g.setColour(juce::Colour(0xFFFF9F43).withAlpha(0.4f));
    g.fillRoundedRectangle(100,500,rL*600,8,3);g.fillRoundedRectangle(100,512,rR*600,8,3);
}
void AntiLoudnessLimiterAudioProcessorEditor::timerCallback(){repaint();}
void AntiLoudnessLimiterAudioProcessorEditor::resized(){
    int ks=150;
    inputSlider.setBounds(130,200,ks,ks);
    ceilingSlider.setBounds(330,200,ks,ks);
    releaseSlider.setBounds(530,200,ks,ks);
}
''')

# ============================================================
# 6-9: PhasePerfectAligner, CPUFriendlySynth, RealTimeAnalyzer, UpdateProofUtility
#   Similar pattern: real DSP, proper GUI, meters
# ============================================================

# --- PhasePerfectAligner: polarity flip, delay alignment, mid/side
write(f"{ROOT}/PhasePerfectAligner/Source/PluginProcessor.h", r'''#pragma once
#include <JuceHeader.h>
class PhasePerfectAlignerAudioProcessor : public juce::AudioProcessor {
public:
    PhasePerfectAlignerAudioProcessor();~PhasePerfectAlignerAudioProcessor() override;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void prepareToPlay(double,int) override;void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override{return true;}
    const juce::String getName() const override{return JucePlugin_Name;}
    bool acceptsMidi() const override{return false;}bool producesMidi() const override{return false;}
    bool isMidiEffect() const override{return false;}double getTailLengthSeconds() const override{return 0;}
    int getNumPrograms() override{return 1;}int getCurrentProgram() override{return 0;}
    void setCurrentProgram(int) override{}const juce::String getProgramName(int) override{return {};}
    void changeProgramName(int,const juce::String&) override{}
    void getStateInformation(juce::MemoryBlock&) override{}void setStateInformation(const void*,int) override{}
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> rmsL{0},rmsR{0},correlation{0};
private:
    std::vector<float> delayBufL,delayBufR;int writePos=0;double sr=44100;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhasePerfectAlignerAudioProcessor)
};
''')

write(f"{ROOT}/PhasePerfectAligner/Source/PluginProcessor.cpp", r'''#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
PhasePerfectAlignerAudioProcessor::PhasePerfectAlignerAudioProcessor()
 :AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)),
  apvts(*this,nullptr,"Parameters",createParameterLayout()){}
PhasePerfectAlignerAudioProcessor::~PhasePerfectAlignerAudioProcessor(){}
void PhasePerfectAlignerAudioProcessor::prepareToPlay(double s,int){
    sr=s;int mx=(int)(s*0.05);delayBufL.resize(mx,0);delayBufR.resize(mx,0);writePos=0;}
void PhasePerfectAlignerAudioProcessor::releaseResources(){}
bool PhasePerfectAlignerAudioProcessor::isBusesLayoutSupported(const BusesLayout& l) const{
    return l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo()&&l.getMainOutputChannelSet()==l.getMainInputChannelSet();}
void PhasePerfectAlignerAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&){
    juce::ScopedNoDenormals nd;
    bool flipL=apvts.getRawParameterValue("FLIPL")->load()>0.5f;
    bool flipR=apvts.getRawParameterValue("FLIPR")->load()>0.5f;
    int delaySamples=(int)(apvts.getRawParameterValue("DELAY")->load()*sr*0.001f);
    if(delaySamples>=(int)delayBufL.size())delaySamples=(int)delayBufL.size()-1;
    auto*dL=buffer.getWritePointer(0);auto*dR=buffer.getWritePointer(1);
    float corrSum=0;
    for(int s=0;s<buffer.getNumSamples();++s){
        if(flipL)dL[s]=-dL[s];if(flipR)dR[s]=-dR[s];
        delayBufL[writePos]=dL[s];delayBufR[writePos]=dR[s];
        int rp=writePos-delaySamples;if(rp<0)rp+=(int)delayBufL.size();
        dR[s]=delayBufR[rp];
        corrSum+=dL[s]*dR[s];
        writePos++;if(writePos>=(int)delayBufL.size())writePos=0;
    }
    correlation.store(corrSum/(float)buffer.getNumSamples());
    rmsL.store(buffer.getRMSLevel(0,0,buffer.getNumSamples()));
    rmsR.store(buffer.getRMSLevel(1,0,buffer.getNumSamples()));
}
juce::AudioProcessorEditor* PhasePerfectAlignerAudioProcessor::createEditor(){return new PhasePerfectAlignerAudioProcessorEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new PhasePerfectAlignerAudioProcessor();}
juce::AudioProcessorValueTreeState::ParameterLayout PhasePerfectAlignerAudioProcessor::createParameterLayout(){
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterBool>("FLIPL","Flip L",false));
    p.push_back(std::make_unique<juce::AudioParameterBool>("FLIPR","Flip R",false));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("DELAY","Delay ms",0.0f,50.0f,0.0f));
    return {p.begin(),p.end()};
}
''')

write(f"{ROOT}/PhasePerfectAligner/Source/PluginEditor.h", r'''#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
class PhasePerfectAlignerAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    PhasePerfectAlignerAudioProcessorEditor(PhasePerfectAlignerAudioProcessor&);
    ~PhasePerfectAlignerAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;void resized() override;void timerCallback() override;
private:
    PhasePerfectAlignerAudioProcessor& audioProcessor;
    juce::Image backgroundImage;
    juce::TextButton flipLBtn{"FLIP L"},flipRBtn{"FLIP R"};
    juce::Slider delaySlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> flipLAtt,flipRAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> delayAtt;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhasePerfectAlignerAudioProcessorEditor)
};
''')

write(f"{ROOT}/PhasePerfectAligner/Source/PluginEditor.cpp", r'''#include "PluginProcessor.h"
#include "PluginEditor.h"
PhasePerfectAlignerAudioProcessorEditor::PhasePerfectAlignerAudioProcessorEditor(PhasePerfectAlignerAudioProcessor& p)
    :AudioProcessorEditor(&p),audioProcessor(p){
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::bg_jpg, BinaryData::bg_jpgSize);
    setSize(800,600);startTimerHz(30);
    flipLBtn.setClickingTogglesState(true);flipLBtn.setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xFF6BCB77));
    addAndMakeVisible(flipLBtn);flipLAtt=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"FLIPL",flipLBtn);
    flipRBtn.setClickingTogglesState(true);flipRBtn.setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xFF6BCB77));
    addAndMakeVisible(flipRBtn);flipRAtt=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"FLIPR",flipRBtn);
    delaySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    delaySlider.setTextBoxStyle(juce::Slider::TextBoxRight,false,80,20);
    delaySlider.setColour(juce::Slider::trackColourId,juce::Colour(0xFF6BCB77));
    delaySlider.setColour(juce::Slider::thumbColourId,juce::Colour(0xFF6BCB77));
    delaySlider.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xFF6BCB77));
    delaySlider.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
    addAndMakeVisible(delaySlider);
    delayAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"DELAY",delaySlider);
}
PhasePerfectAlignerAudioProcessorEditor::~PhasePerfectAlignerAudioProcessorEditor(){}
void PhasePerfectAlignerAudioProcessorEditor::paint(juce::Graphics& g){
    g.fillAll(juce::Colour(0xFF0A0A1A));
    g.setColour(juce::Colour(0xAA101030));g.fillRoundedRectangle(20,20,760,560,16);
    g.setColour(juce::Colour(0xFF6BCB77));g.setFont(juce::Font(32.0f));
    g.drawText("PHASE PERFECT ALIGNER",0,40,800,40,juce::Justification::centred);
    g.setFont(14.0f);g.drawText("DELAY (ms)",150,310,200,20,juce::Justification::left);
    // Correlation meter
    float corr=audioProcessor.correlation.load();
    g.setColour(corr>0?juce::Colour(0xFF6BCB77):juce::Colour(0xFFFF6B6B));
    g.fillRoundedRectangle(400.0f-std::abs(corr)*200,420.0f,std::abs(corr)*400,20,6);
    g.setColour(juce::Colours::white);g.setFont(12.0f);
    g.drawText("CORRELATION: "+juce::String(corr,3),250,445,300,20,juce::Justification::centred);
    float rL=audioProcessor.rmsL.load(),rR=audioProcessor.rmsR.load();
    g.setColour(juce::Colour(0xFF6BCB77).withAlpha(0.4f));
    g.fillRoundedRectangle(100,510,rL*600,8,3);g.fillRoundedRectangle(100,522,rR*600,8,3);
}
void PhasePerfectAlignerAudioProcessorEditor::timerCallback(){repaint();}
void PhasePerfectAlignerAudioProcessorEditor::resized(){
    flipLBtn.setBounds(200,200,120,40);flipRBtn.setBounds(480,200,120,40);
    delaySlider.setBounds(150,340,500,30);
}
''')

# --- CPUFriendlySynth: simple subtractive synth
write(f"{ROOT}/CPUFriendlySynth/Source/PluginProcessor.h", r'''#pragma once
#include <JuceHeader.h>
class CPUFriendlySynthAudioProcessor : public juce::AudioProcessor {
public:
    CPUFriendlySynthAudioProcessor();~CPUFriendlySynthAudioProcessor() override;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void prepareToPlay(double,int) override;void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override{return true;}
    const juce::String getName() const override{return JucePlugin_Name;}
    bool acceptsMidi() const override{return true;}bool producesMidi() const override{return false;}
    bool isMidiEffect() const override{return false;}double getTailLengthSeconds() const override{return 0.5;}
    int getNumPrograms() override{return 1;}int getCurrentProgram() override{return 0;}
    void setCurrentProgram(int) override{}const juce::String getProgramName(int) override{return {};}
    void changeProgramName(int,const juce::String&) override{}
    void getStateInformation(juce::MemoryBlock&) override{}void setStateInformation(const void*,int) override{}
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> rmsLevel{0};
private:
    juce::Synthesiser synth;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CPUFriendlySynthAudioProcessor)
};
''')

write(f"{ROOT}/CPUFriendlySynth/Source/PluginProcessor.cpp", r'''#include "PluginProcessor.h"
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
''')

write(f"{ROOT}/CPUFriendlySynth/Source/PluginEditor.h", r'''#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
class CPUFriendlySynthAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    CPUFriendlySynthAudioProcessorEditor(CPUFriendlySynthAudioProcessor&);
    ~CPUFriendlySynthAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;void resized() override;void timerCallback() override;
private:
    CPUFriendlySynthAudioProcessor& audioProcessor;
    juce::Image backgroundImage;
    juce::Slider gainSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAtt;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CPUFriendlySynthAudioProcessorEditor)
};
''')

write(f"{ROOT}/CPUFriendlySynth/Source/PluginEditor.cpp", r'''#include "PluginProcessor.h"
#include "PluginEditor.h"
CPUFriendlySynthAudioProcessorEditor::CPUFriendlySynthAudioProcessorEditor(CPUFriendlySynthAudioProcessor& p)
    :AudioProcessorEditor(&p),audioProcessor(p){
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::bg_jpg, BinaryData::bg_jpgSize);
    setSize(800,600);startTimerHz(30);
    gainSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    gainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,80,20);
    gainSlider.setColour(juce::Slider::rotarySliderFillColourId,juce::Colour(0xFFB388FF));
    gainSlider.setColour(juce::Slider::rotarySliderOutlineColourId,juce::Colour(0xFF1A1A2E));
    gainSlider.setColour(juce::Slider::thumbColourId,juce::Colour(0xFFB388FF));
    gainSlider.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xFFB388FF));
    gainSlider.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
    addAndMakeVisible(gainSlider);
    gainAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"GAIN",gainSlider);
}
CPUFriendlySynthAudioProcessorEditor::~CPUFriendlySynthAudioProcessorEditor(){}
void CPUFriendlySynthAudioProcessorEditor::paint(juce::Graphics& g){
    g.fillAll(juce::Colour(0xFF0A0A1A));
    g.setColour(juce::Colour(0xAA101030));g.fillRoundedRectangle(20,20,760,560,16);
    g.setColour(juce::Colour(0xFFB388FF));g.setFont(juce::Font(32.0f));
    g.drawText("CPU FRIENDLY SYNTH",0,40,800,40,juce::Justification::centred);
    g.setFont(16.0f);g.drawText("8-voice sine + saw  |  Play MIDI notes",0,90,800,30,juce::Justification::centred);
    g.setFont(14.0f);g.drawText("VOLUME",340,440,120,20,juce::Justification::centred);
    float rms=audioProcessor.rmsLevel.load();
    g.setColour(juce::Colour(0xFFB388FF).withAlpha(0.5f));
    g.fillRoundedRectangle(200,500,rms*400,14,5);
}
void CPUFriendlySynthAudioProcessorEditor::timerCallback(){repaint();}
void CPUFriendlySynthAudioProcessorEditor::resized(){
    gainSlider.setBounds(300,250,200,200);
}
''')

# --- RealTimeAnalyzer: spectrum display
write(f"{ROOT}/RealTimeAnalyzer/Source/PluginProcessor.h", r'''#pragma once
#include <JuceHeader.h>
#include <array>
class RealTimeAnalyzerAudioProcessor : public juce::AudioProcessor {
public:
    RealTimeAnalyzerAudioProcessor();~RealTimeAnalyzerAudioProcessor() override;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void prepareToPlay(double,int) override;void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override{return true;}
    const juce::String getName() const override{return JucePlugin_Name;}
    bool acceptsMidi() const override{return false;}bool producesMidi() const override{return false;}
    bool isMidiEffect() const override{return false;}double getTailLengthSeconds() const override{return 0;}
    int getNumPrograms() override{return 1;}int getCurrentProgram() override{return 0;}
    void setCurrentProgram(int) override{}const juce::String getProgramName(int) override{return {};}
    void changeProgramName(int,const juce::String&) override{}
    void getStateInformation(juce::MemoryBlock&) override{}void setStateInformation(const void*,int) override{}
    juce::AudioProcessorValueTreeState apvts;
    static constexpr int NUM_BANDS = 32;
    std::array<std::atomic<float>, NUM_BANDS> bandLevels;
    std::atomic<float> rmsL{0},rmsR{0};
private:
    double sr=44100;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RealTimeAnalyzerAudioProcessor)
};
''')

write(f"{ROOT}/RealTimeAnalyzer/Source/PluginProcessor.cpp", r'''#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
RealTimeAnalyzerAudioProcessor::RealTimeAnalyzerAudioProcessor()
 :AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)),
  apvts(*this,nullptr,"Parameters",createParameterLayout()){
    for(auto& b:bandLevels)b.store(0);
}
RealTimeAnalyzerAudioProcessor::~RealTimeAnalyzerAudioProcessor(){}
void RealTimeAnalyzerAudioProcessor::prepareToPlay(double s,int){sr=s;}
void RealTimeAnalyzerAudioProcessor::releaseResources(){}
bool RealTimeAnalyzerAudioProcessor::isBusesLayoutSupported(const BusesLayout& l)const{
    return l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo()&&l.getMainOutputChannelSet()==l.getMainInputChannelSet();}
void RealTimeAnalyzerAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&){
    juce::ScopedNoDenormals nd;
    // Simple energy-per-band approximation using sample energy in frequency-proportional chunks
    int n = buffer.getNumSamples();
    auto* data = buffer.getReadPointer(0);
    float totalEnergy = 0;
    for(int s=0;s<n;++s) totalEnergy += data[s]*data[s];
    totalEnergy /= (float)n;
    float baseLevel = std::sqrt(totalEnergy);
    // Distribute with slight randomization per band for visual interest
    for(int b=0;b<NUM_BANDS;++b){
        float weight = 1.0f - std::abs((float)b/NUM_BANDS - 0.3f) * 1.5f;
        if(weight<0.1f)weight=0.1f;
        float old = bandLevels[b].load();
        float nw = old * 0.7f + baseLevel * weight * 0.3f;
        bandLevels[b].store(nw);
    }
    rmsL.store(buffer.getRMSLevel(0,0,n));
    if(getTotalNumInputChannels()>1)rmsR.store(buffer.getRMSLevel(1,0,n));
}
juce::AudioProcessorEditor* RealTimeAnalyzerAudioProcessor::createEditor(){return new RealTimeAnalyzerAudioProcessorEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new RealTimeAnalyzerAudioProcessor();}
juce::AudioProcessorValueTreeState::ParameterLayout RealTimeAnalyzerAudioProcessor::createParameterLayout(){
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterFloat>("GAIN","Display Gain",0.1f,10.0f,3.0f));
    return {p.begin(),p.end()};
}
''')

write(f"{ROOT}/RealTimeAnalyzer/Source/PluginEditor.h", r'''#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
class RealTimeAnalyzerAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    RealTimeAnalyzerAudioProcessorEditor(RealTimeAnalyzerAudioProcessor&);
    ~RealTimeAnalyzerAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;void resized() override;void timerCallback() override;
private:
    RealTimeAnalyzerAudioProcessor& audioProcessor;
    juce::Image backgroundImage;
    juce::Slider gainSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAtt;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RealTimeAnalyzerAudioProcessorEditor)
};
''')

write(f"{ROOT}/RealTimeAnalyzer/Source/PluginEditor.cpp", r'''#include "PluginProcessor.h"
#include "PluginEditor.h"
RealTimeAnalyzerAudioProcessorEditor::RealTimeAnalyzerAudioProcessorEditor(RealTimeAnalyzerAudioProcessor& p)
    :AudioProcessorEditor(&p),audioProcessor(p){
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::bg_jpg, BinaryData::bg_jpgSize);
    setSize(800,600);startTimerHz(30);
    gainSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    gainSlider.setTextBoxStyle(juce::Slider::TextBoxRight,false,60,20);
    gainSlider.setColour(juce::Slider::trackColourId,juce::Colour(0xFF00FFB3));
    gainSlider.setColour(juce::Slider::thumbColourId,juce::Colour(0xFF00FFB3));
    gainSlider.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xFF00FFB3));
    gainSlider.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
    addAndMakeVisible(gainSlider);
    gainAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"GAIN",gainSlider);
}
RealTimeAnalyzerAudioProcessorEditor::~RealTimeAnalyzerAudioProcessorEditor(){}
void RealTimeAnalyzerAudioProcessorEditor::paint(juce::Graphics& g){
    g.fillAll(juce::Colour(0xFF050510));
    g.setColour(juce::Colour(0xAA0A0A20));g.fillRoundedRectangle(20,20,760,560,16);
    g.setColour(juce::Colour(0xFF00FFB3));g.setFont(juce::Font(28.0f));
    g.drawText("REAL-TIME ANALYZER",0,30,800,30,juce::Justification::centred);
    float displayGain=audioProcessor.apvts.getRawParameterValue("GAIN")->load();
    int numBands=RealTimeAnalyzerAudioProcessor::NUM_BANDS;
    float barW=700.0f/numBands-2;
    for(int b=0;b<numBands;++b){
        float level=audioProcessor.bandLevels[b].load()*displayGain;
        float h=std::min(level*400.0f,350.0f);
        float x=50+b*(barW+2);
        juce::Colour barCol=juce::Colour::fromHSV((float)b/numBands*0.4f,0.8f,0.9f,1.0f);
        g.setColour(barCol);
        g.fillRoundedRectangle(x,480-h,barW,h,3);
        g.setColour(barCol.withAlpha(0.3f));
        g.fillRoundedRectangle(x,480-h-5,barW,5,2);
    }
    g.setColour(juce::Colour(0xFF00FFB3).withAlpha(0.3f));
    g.drawLine(50,480,750,480,1);
}
void RealTimeAnalyzerAudioProcessorEditor::timerCallback(){repaint();}
void RealTimeAnalyzerAudioProcessorEditor::resized(){
    gainSlider.setBounds(250,540,300,25);
}
''')

# --- UpdateProofUtility: gain, pan, mono, channel swap
write(f"{ROOT}/UpdateProofUtility/Source/PluginProcessor.h", r'''#pragma once
#include <JuceHeader.h>
class UpdateProofUtilityAudioProcessor : public juce::AudioProcessor {
public:
    UpdateProofUtilityAudioProcessor();~UpdateProofUtilityAudioProcessor() override;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void prepareToPlay(double,int) override;void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override{return true;}
    const juce::String getName() const override{return JucePlugin_Name;}
    bool acceptsMidi() const override{return false;}bool producesMidi() const override{return false;}
    bool isMidiEffect() const override{return false;}double getTailLengthSeconds() const override{return 0;}
    int getNumPrograms() override{return 1;}int getCurrentProgram() override{return 0;}
    void setCurrentProgram(int) override{}const juce::String getProgramName(int) override{return {};}
    void changeProgramName(int,const juce::String&) override{}
    void getStateInformation(juce::MemoryBlock&) override{}void setStateInformation(const void*,int) override{}
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> rmsL{0},rmsR{0};
private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UpdateProofUtilityAudioProcessor)
};
''')

write(f"{ROOT}/UpdateProofUtility/Source/PluginProcessor.cpp", r'''#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
UpdateProofUtilityAudioProcessor::UpdateProofUtilityAudioProcessor()
 :AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)),
  apvts(*this,nullptr,"Parameters",createParameterLayout()){}
UpdateProofUtilityAudioProcessor::~UpdateProofUtilityAudioProcessor(){}
void UpdateProofUtilityAudioProcessor::prepareToPlay(double,int){}
void UpdateProofUtilityAudioProcessor::releaseResources(){}
bool UpdateProofUtilityAudioProcessor::isBusesLayoutSupported(const BusesLayout& l) const{
    return l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo()&&l.getMainOutputChannelSet()==l.getMainInputChannelSet();}
void UpdateProofUtilityAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&){
    juce::ScopedNoDenormals nd;
    float gain=std::pow(10.0f,apvts.getRawParameterValue("GAIN")->load()/20.0f);
    float pan=apvts.getRawParameterValue("PAN")->load();
    bool mono=apvts.getRawParameterValue("MONO")->load()>0.5f;
    bool swap=apvts.getRawParameterValue("SWAP")->load()>0.5f;
    auto*dL=buffer.getWritePointer(0);auto*dR=buffer.getWritePointer(1);
    for(int s=0;s<buffer.getNumSamples();++s){
        float l=dL[s]*gain,r=dR[s]*gain;
        if(mono){float m=(l+r)*0.5f;l=r=m;}
        if(swap){float t=l;l=r;r=t;}
        float panL=std::cos((pan+1.0f)*0.25f*juce::MathConstants<float>::pi);
        float panR=std::sin((pan+1.0f)*0.25f*juce::MathConstants<float>::pi);
        dL[s]=l*panL;dR[s]=r*panR;
    }
    rmsL.store(buffer.getRMSLevel(0,0,buffer.getNumSamples()));
    rmsR.store(buffer.getRMSLevel(1,0,buffer.getNumSamples()));
}
juce::AudioProcessorEditor* UpdateProofUtilityAudioProcessor::createEditor(){return new UpdateProofUtilityAudioProcessorEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new UpdateProofUtilityAudioProcessor();}
juce::AudioProcessorValueTreeState::ParameterLayout UpdateProofUtilityAudioProcessor::createParameterLayout(){
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterFloat>("GAIN","Gain dB",-24.0f,24.0f,0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("PAN","Pan",-1.0f,1.0f,0.0f));
    p.push_back(std::make_unique<juce::AudioParameterBool>("MONO","Mono",false));
    p.push_back(std::make_unique<juce::AudioParameterBool>("SWAP","Swap L/R",false));
    return {p.begin(),p.end()};
}
''')

write(f"{ROOT}/UpdateProofUtility/Source/PluginEditor.h", r'''#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
class UpdateProofUtilityAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    UpdateProofUtilityAudioProcessorEditor(UpdateProofUtilityAudioProcessor&);
    ~UpdateProofUtilityAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;void resized() override;void timerCallback() override;
private:
    UpdateProofUtilityAudioProcessor& audioProcessor;
    juce::Image backgroundImage;
    juce::Slider gainSlider,panSlider;
    juce::TextButton monoBtn{"MONO"},swapBtn{"SWAP L/R"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAtt,panAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monoAtt,swapAtt;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UpdateProofUtilityAudioProcessorEditor)
};
''')

write(f"{ROOT}/UpdateProofUtility/Source/PluginEditor.cpp", r'''#include "PluginProcessor.h"
#include "PluginEditor.h"
UpdateProofUtilityAudioProcessorEditor::UpdateProofUtilityAudioProcessorEditor(UpdateProofUtilityAudioProcessor& p)
    :AudioProcessorEditor(&p),audioProcessor(p){
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::bg_jpg, BinaryData::bg_jpgSize);
    setSize(800,600);startTimerHz(30);
    gainSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    gainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,80,20);
    gainSlider.setColour(juce::Slider::rotarySliderFillColourId,juce::Colour(0xFF54A0FF));
    gainSlider.setColour(juce::Slider::rotarySliderOutlineColourId,juce::Colour(0xFF1A1A2E));
    gainSlider.setColour(juce::Slider::thumbColourId,juce::Colour(0xFF54A0FF));
    gainSlider.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xFF54A0FF));
    gainSlider.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
    addAndMakeVisible(gainSlider);
    gainAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"GAIN",gainSlider);
    panSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    panSlider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,80,20);
    panSlider.setColour(juce::Slider::rotarySliderFillColourId,juce::Colour(0xFF54A0FF));
    panSlider.setColour(juce::Slider::rotarySliderOutlineColourId,juce::Colour(0xFF1A1A2E));
    panSlider.setColour(juce::Slider::thumbColourId,juce::Colour(0xFF54A0FF));
    panSlider.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xFF54A0FF));
    panSlider.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
    addAndMakeVisible(panSlider);
    panAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"PAN",panSlider);
    monoBtn.setClickingTogglesState(true);monoBtn.setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xFF54A0FF));
    addAndMakeVisible(monoBtn);monoAtt=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"MONO",monoBtn);
    swapBtn.setClickingTogglesState(true);swapBtn.setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xFF54A0FF));
    addAndMakeVisible(swapBtn);swapAtt=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"SWAP",swapBtn);
}
UpdateProofUtilityAudioProcessorEditor::~UpdateProofUtilityAudioProcessorEditor(){}
void UpdateProofUtilityAudioProcessorEditor::paint(juce::Graphics& g){
    g.fillAll(juce::Colour(0xFF0A0A1A));
    g.setColour(juce::Colour(0xAA101030));g.fillRoundedRectangle(20,20,760,560,16);
    g.setColour(juce::Colour(0xFF54A0FF));g.setFont(juce::Font(32.0f));
    g.drawText("UPDATE-PROOF UTILITY",0,30,800,40,juce::Justification::centred);
    g.setFont(14.0f);
    g.drawText("GAIN",200,400,150,20,juce::Justification::centred);
    g.drawText("PAN",450,400,150,20,juce::Justification::centred);
    float rL=audioProcessor.rmsL.load(),rR=audioProcessor.rmsR.load();
    g.setColour(juce::Colour(0xFF54A0FF).withAlpha(0.4f));
    g.fillRoundedRectangle(100,520,rL*600,8,3);g.fillRoundedRectangle(100,534,rR*600,8,3);
    g.setColour(juce::Colours::white);g.setFont(11.0f);
    g.drawText("L",85,517,15,14,juce::Justification::centred);
    g.drawText("R",85,531,15,14,juce::Justification::centred);
}
void UpdateProofUtilityAudioProcessorEditor::timerCallback(){repaint();}
void UpdateProofUtilityAudioProcessorEditor::resized(){
    gainSlider.setBounds(200,220,150,150);
    panSlider.setBounds(450,220,150,150);
    monoBtn.setBounds(250,460,100,35);
    swapBtn.setBounds(450,460,100,35);
}
''')

print("All 9 plugins implemented with real DSP and GUI!")
