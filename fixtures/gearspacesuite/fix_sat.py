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
    return { params.begin(), params.end() };
}""", cpp)

# Replace DSP loop
dsp_loop = """
    float drive = apvts.getRawParameterValue("DRIVE")->load();
    float mix = apvts.getRawParameterValue("MIX")->load();
    float output = apvts.getRawParameterValue("OUTPUT")->load();

    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            float clean = channelData[sample];
            float driven = std::tanh(clean * drive) * (1.0f / drive);
            channelData[sample] = ((clean * (1.0f - mix)) + (driven * mix)) * output;
        }
    }
"""
cpp = re.sub(r'for \(int channel = 0; channel < totalNumInputChannels; \+\+channel\)\s*\{[\s\S]*?\}\s*\}', dsp_loop, cpp)

with open(cpp_path, "w") as f:
    f.write(cpp)

# 2. PluginEditor.h
ed_h_path = os.path.join(src_dir, "PluginEditor.h")
with open(ed_h_path, "r") as f:
    ed_h = f.read()

ed_h = re.sub(r'juce::Slider mainSlider;\s*std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;', 
"""juce::Slider driveSlider, mixSlider, outputSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driveAttachment, mixAttachment, outputAttachment;""", ed_h)

with open(ed_h_path, "w") as f:
    f.write(ed_h)

# 3. PluginEditor.cpp
ed_cpp_path = os.path.join(src_dir, "PluginEditor.cpp")
with open(ed_cpp_path, "r") as f:
    ed_cpp = f.read()

constructor_body = """
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::bg_jpg, BinaryData::bg_jpgSize);
    setSize (800, 600);
    
    driveSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    driveSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 100, 20);
    addAndMakeVisible(driveSlider);
    driveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "DRIVE", driveSlider);

    mixSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    mixSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 100, 20);
    addAndMakeVisible(mixSlider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "MIX", mixSlider);

    outputSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    outputSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 100, 20);
    addAndMakeVisible(outputSlider);
    outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "OUTPUT", outputSlider);
"""
ed_cpp = re.sub(r'backgroundImage = juce::ImageCache::getFromMemory[\s\S]*?attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>[^;]*;', constructor_body, ed_cpp)

ed_cpp = ed_cpp.replace("mainSlider.setBounds(300, 200, 200, 200);", 
"""
    int dialSize = 150;
    driveSlider.setBounds(100, 250, dialSize, dialSize);
    mixSlider.setBounds(325, 250, dialSize, dialSize);
    outputSlider.setBounds(550, 250, dialSize, dialSize);
""")

with open(ed_cpp_path, "w") as f:
    f.write(ed_cpp)

