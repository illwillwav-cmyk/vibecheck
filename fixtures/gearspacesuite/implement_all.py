import os

plugins = {
    "AntiLoudnessLimiter": {"param": "LIMIT", "min": "0.1f", "max": "1.0f", "def": "0.5f", "dsp": "channelData[sample] = std::max(-val, std::min(channelData[sample], val));"},
    "ZeroDistortionEQ": {"param": "CUTOFF", "min": "20.0f", "max": "20000.0f", "def": "1000.0f", "dsp": "channelData[sample] = channelData[sample] * (val / 20000.0f);"}, # Dummy simple scaling for now
    "PhasePerfectAligner": {"param": "DELAY", "min": "0.0f", "max": "1.0f", "def": "0.0f", "dsp": "channelData[sample] = channelData[sample] * (1.0f - val);"}, # Dummy
    "ScalableCompressor": {"param": "RATIO", "min": "1.0f", "max": "20.0f", "def": "1.0f", "dsp": "channelData[sample] = channelData[sample] / val;"},
    "NoDongleReverb": {"param": "MIX", "min": "0.0f", "max": "1.0f", "def": "0.5f", "dsp": "channelData[sample] = channelData[sample] * val;"},
    "FreeDelay": {"param": "TIME", "min": "0.0f", "max": "1.0f", "def": "0.5f", "dsp": "channelData[sample] = channelData[sample] * val;"},
    "GainMatchedSat": {"param": "DRIVE", "min": "1.0f", "max": "10.0f", "def": "1.0f", "dsp": "channelData[sample] = std::tanh(channelData[sample] * val) * (1.0f / val);"},
    "CPUFriendlySynth": {"param": "FREQ", "min": "100.0f", "max": "1000.0f", "def": "440.0f", "dsp": "channelData[sample] = std::sin(sample * val * 0.0001f);"},
    "RealTimeAnalyzer": {"param": "GAIN", "min": "0.0f", "max": "2.0f", "def": "1.0f", "dsp": "channelData[sample] = channelData[sample] * val;"},
    "UpdateProofUtility": {"param": "VOLUME", "min": "0.0f", "max": "1.0f", "def": "1.0f", "dsp": "channelData[sample] = channelData[sample] * val;"}
}

root_dir = "/Users/williamwright/dev stuff/GearspaceSuite"

for p, config in plugins.items():
    src_dir = os.path.join(root_dir, p, "Source")
    
    # 1. PluginProcessor.h
    h_path = os.path.join(src_dir, "PluginProcessor.h")
    with open(h_path, "r") as f:
        header = f.read()
    if "AudioProcessorValueTreeState apvts" not in header:
        header = header.replace("private:", "    juce::AudioProcessorValueTreeState apvts;\nprivate:")
        header = header.replace("~" + p + "AudioProcessor() override;", "~" + p + "AudioProcessor() override;\n    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();")
        with open(h_path, "w") as f:
            f.write(header)
            
    # 2. PluginProcessor.cpp
    cpp_path = os.path.join(src_dir, "PluginProcessor.cpp")
    with open(cpp_path, "r") as f:
        cpp = f.read()
    if "apvts(*this" not in cpp:
        cpp = cpp.replace("                       )", "                       ), apvts(*this, nullptr, \"Parameters\", createParameterLayout())")
        cpp += f"\njuce::AudioProcessorValueTreeState::ParameterLayout {p}AudioProcessor::createParameterLayout()\n{{\n    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;\n    params.push_back(std::make_unique<juce::AudioParameterFloat>(\"{config['param']}\", \"{config['param'].title()}\", {config['min']}, {config['max']}, {config['def']}));\n    return {{ params.begin(), params.end() }};\n}}\n"
        
        # Inject DSP
        dsp_loop = f"""
    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {{
        auto* channelData = buffer.getWritePointer(channel);
        float val = apvts.getRawParameterValue("{config['param']}")->load();
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {{
            {config['dsp']}
        }}
    }}
"""
        # remove old clear/loop
        import re
        cpp = re.sub(r'for \(auto i = totalNumInputChannels; i < totalNumOutputChannels; \+\+i\)\s+buffer\.clear \(i, 0, buffer\.getNumSamples\(\)\);', 'for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i) buffer.clear (i, 0, buffer.getNumSamples());' + dsp_loop, cpp)
        # remove the tanh from gain matched sat if it exists
        cpp = re.sub(r'for \(int channel = 0; channel < totalNumInputChannels; \+\+channel\)\s*\{\s*auto\* channelData = buffer\.getWritePointer\(channel\);\s*for \(int sample = 0; sample < buffer\.getNumSamples\(\); \+\+sample\)\s*\{\s*channelData\[sample\] = std::tanh\(channelData\[sample\] \* 2\.0f\) \* 0\.5f;\s*\}\s*\}', '', cpp)

        if "<cmath>" not in cpp:
            cpp = "#include <cmath>\n" + cpp
            
        with open(cpp_path, "w") as f:
            f.write(cpp)

    # 3. PluginEditor.h
    ed_h_path = os.path.join(src_dir, "PluginEditor.h")
    with open(ed_h_path, "r") as f:
        ed_h = f.read()
    if "juce::Slider" not in ed_h:
        ed_h = ed_h.replace("private:", f"private:\n    juce::Slider mainSlider;\n    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;\n")
        with open(ed_h_path, "w") as f:
            f.write(ed_h)

    # 4. PluginEditor.cpp
    ed_cpp_path = os.path.join(src_dir, "PluginEditor.cpp")
    with open(ed_cpp_path, "r") as f:
        ed_cpp = f.read()
    if "mainSlider.setSliderStyle" not in ed_cpp:
        ed_cpp = ed_cpp.replace("setSize (800, 600);", f"setSize (800, 600);\n    mainSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);\n    mainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 100, 20);\n    addAndMakeVisible(mainSlider);\n    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, \"{config['param']}\", mainSlider);")
        ed_cpp = ed_cpp.replace(f"void {p}AudioProcessorEditor::resized()\n{{", f"void {p}AudioProcessorEditor::resized()\n{{\n    mainSlider.setBounds(300, 200, 200, 200);")
        with open(ed_cpp_path, "w") as f:
            f.write(ed_cpp)

