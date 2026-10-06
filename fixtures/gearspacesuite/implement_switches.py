import os, re

root_dir = "/Users/williamwright/dev stuff/GearspaceSuite"
src_dir = os.path.join(root_dir, "GainMatchedSat/Source")

# 1. PluginProcessor.cpp
cpp_path = os.path.join(src_dir, "PluginProcessor.cpp")
with open(cpp_path, "r") as f:
    cpp = f.read()

# Replace parameter layout
cpp = re.sub(r'juce::AudioProcessorValueTreeState::ParameterLayout GainMatchedSatAudioProcessor::createParameterLayout\(\)\s*\{[\s\S]*?return \{ params\.begin\(\), params\.end\(\) \};\s*\}', 
"""juce::AudioProcessorValueTreeState::ParameterLayout GainMatchedSatAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>("DRIVE", "Drive", 1.0f, 10.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("MIX", "Mix", 0.0f, 1.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("OUTPUT", "Output", 0.0f, 2.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("POWER", "Power", true));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("STYLE", "Style", juce::StringArray{"Tube", "Tape", "Germ"}, 0));
    return { params.begin(), params.end() };
}""", cpp)

# Replace DSP loop
dsp_loop = """
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
"""
cpp = re.sub(r'float drive = apvts\.getRawParameterValue\("DRIVE"\)->load\(\);[\s\S]*?rmsLevelRight\.store\(rms\);\s*\}', dsp_loop, cpp)
with open(cpp_path, "w") as f:
    f.write(cpp)

# 2. PluginEditor.h
ed_h_path = os.path.join(src_dir, "PluginEditor.h")
with open(ed_h_path, "r") as f:
    ed_h = f.read()

ed_h = re.sub(r'juce::Slider driveSlider, mixSlider, outputSlider;', 
"""juce::Slider driveSlider, mixSlider, outputSlider;
    juce::TextButton powerButton;
    juce::Slider styleSlider;""", ed_h)

ed_h = re.sub(r'std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driveAttachment, mixAttachment, outputAttachment;', 
"""std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driveAttachment, mixAttachment, outputAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> styleAttachment;""", ed_h)

with open(ed_h_path, "w") as f:
    f.write(ed_h)

# 3. PluginEditor.cpp
ed_cpp_path = os.path.join(src_dir, "PluginEditor.cpp")
with open(ed_cpp_path, "r") as f:
    ed_cpp = f.read()

constructor = """
    setupSlider(driveSlider, "DRIVE", driveAttachment);
    setupSlider(mixSlider, "MIX", mixAttachment);
    setupSlider(outputSlider, "OUTPUT", outputAttachment);
    
    powerButton.setClickingTogglesState(true);
    powerButton.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    powerButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    powerButton.setColour(juce::TextButton::textColourOffId, juce::Colours::transparentBlack);
    powerButton.setColour(juce::TextButton::textColourOnId, juce::Colours::transparentBlack);
    addAndMakeVisible(powerButton);
    powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(audioProcessor.apvts, "POWER", powerButton);

    styleSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    styleSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    styleSlider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colours::transparentBlack);
    styleSlider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colours::transparentBlack);
    styleSlider.setColour(juce::Slider::thumbColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(styleSlider);
    styleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "STYLE", styleSlider);
"""
ed_cpp = re.sub(r'setupSlider\(driveSlider, "DRIVE", driveAttachment\);\s*setupSlider\(mixSlider, "MIX", mixAttachment\);\s*setupSlider\(outputSlider, "OUTPUT", outputAttachment\);', constructor, ed_cpp)

resized = """
void GainMatchedSatAudioProcessorEditor::resized()
{
    int dialSize = 175;
    driveSlider.setBounds(96, 178, dialSize, dialSize);
    mixSlider.setBounds(312, 178, dialSize, dialSize);
    outputSlider.setBounds(525, 178, dialSize, dialSize);
    
    powerButton.setBounds(80, 430, 50, 70);
    styleSlider.setBounds(660, 420, 70, 70);
}
"""
ed_cpp = re.sub(r'void GainMatchedSatAudioProcessorEditor::resized\(\)[\s\S]*?\}', resized, ed_cpp)

with open(ed_cpp_path, "w") as f:
    f.write(ed_cpp)
