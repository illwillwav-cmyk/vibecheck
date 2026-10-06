import os, re, sys

root_dir = "/Users/williamwright/dev stuff/GearspaceSuite"
src_dir = os.path.join(root_dir, "GainMatchedSat/Source")
img_path = "/Users/williamwright/.gemini/antigravity-ide/brain/1fc55cdb-c03d-4ebc-870a-5588e526fb17/toaster_gui_1790808790462.jpg"

# 1. Update BinaryData
with open(img_path, "rb") as f:
    img_data = f.read()

hex_data = ','.join([str(b) for b in img_data])

with open(os.path.join(src_dir, "BinaryData.cpp"), "w") as f:
    f.write(f"""
#include "BinaryData.h"
namespace BinaryData
{{
    const unsigned char bg_jpg[] = {{{hex_data}}};
    const int bg_jpgSize = {len(img_data)};
}}
""")

# 2. PluginProcessor.h (Add RMS atomics)
h_path = os.path.join(src_dir, "PluginProcessor.h")
with open(h_path, "r") as f:
    header = f.read()
if "std::atomic<float> rmsLevelLeft" not in header:
    header = header.replace("private:", "public:\n    std::atomic<float> rmsLevelLeft { 0.0f };\n    std::atomic<float> rmsLevelRight { 0.0f };\nprivate:")
    with open(h_path, "w") as f:
        f.write(header)

# 3. PluginProcessor.cpp (Compute RMS)
cpp_path = os.path.join(src_dir, "PluginProcessor.cpp")
with open(cpp_path, "r") as f:
    cpp = f.read()

rms_logic = """
            channelData[sample] = ((clean * (1.0f - mix)) + (driven * mix)) * output;
        }
        float rms = buffer.getRMSLevel(channel, 0, buffer.getNumSamples());
        if (channel == 0) rmsLevelLeft.store(rms);
        if (channel == 1) rmsLevelRight.store(rms);
    }
"""
cpp = re.sub(r'channelData\[sample\] = \(\(clean \* \(1\.0f - mix\)\) \+ \(driven \* mix\)\) \* output;\s*\}\s*\}', rms_logic, cpp)
with open(cpp_path, "w") as f:
    f.write(cpp)

# 4. PluginEditor.h (Add Timer)
ed_h_path = os.path.join(src_dir, "PluginEditor.h")
with open(ed_h_path, "r") as f:
    ed_h = f.read()
if "public juce::Timer" not in ed_h:
    ed_h = ed_h.replace("public juce::AudioProcessorEditor", "public juce::AudioProcessorEditor, public juce::Timer")
    ed_h = ed_h.replace("void resized() override;", "void resized() override;\n    void timerCallback() override;")
    with open(ed_h_path, "w") as f:
        f.write(ed_h)

# 5. PluginEditor.cpp (UI logic)
ed_cpp_path = os.path.join(src_dir, "PluginEditor.cpp")
with open(ed_cpp_path, "r") as f:
    ed_cpp = f.read()

constructor = """
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::bg_jpg, BinaryData::bg_jpgSize);
    setSize (800, 600);
    
    auto setupSlider = [this](juce::Slider& s, const juce::String& paramID, std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& att) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setPopupDisplayEnabled(true, false, this);
        s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colours::transparentBlack);
        s.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colours::transparentBlack);
        s.setColour(juce::Slider::thumbColourId, juce::Colours::black.withAlpha(0.8f));
        addAndMakeVisible(s);
        att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, paramID, s);
    };

    setupSlider(driveSlider, "DRIVE", driveAttachment);
    setupSlider(mixSlider, "MIX", mixAttachment);
    setupSlider(outputSlider, "OUTPUT", outputAttachment);
    
    startTimerHz(30);
"""
ed_cpp = re.sub(r'backgroundImage = juce::ImageCache::getFromMemory[\s\S]*?attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>[^;]*;', constructor, ed_cpp)

paint = """
void GainMatchedSatAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    if (backgroundImage.isValid())
        g.drawImage(backgroundImage, getLocalBounds().toFloat());

    // Draw VU Meters (animated)
    float rmsL = audioProcessor.rmsLevelLeft.load();
    float rmsR = audioProcessor.rmsLevelRight.load();
    
    // Scale RMS to degrees for needle
    float angleL = juce::jmap(rmsL, 0.0f, 1.0f, -0.8f, 0.8f);
    float angleR = juce::jmap(rmsR, 0.0f, 1.0f, -0.8f, 0.8f);

    // Left Meter
    g.setColour(juce::Colours::black.withAlpha(0.8f));
    juce::Point<float> pivotL(265.0f, 430.0f);
    juce::Line<float> needleL(pivotL, pivotL.getPointOnCircumference(70.0f, angleL - juce::MathConstants<float>::halfPi));
    g.drawLine(needleL, 2.0f);

    // Right Meter
    juce::Point<float> pivotR(535.0f, 430.0f);
    juce::Line<float> needleR(pivotR, pivotR.getPointOnCircumference(70.0f, angleR - juce::MathConstants<float>::halfPi));
    g.drawLine(needleR, 2.0f);
}
"""
ed_cpp = re.sub(r'void GainMatchedSatAudioProcessorEditor::paint \(juce::Graphics& g\)[\s\S]*?\}', paint, ed_cpp)

resized = """
void GainMatchedSatAudioProcessorEditor::timerCallback()
{
    repaint();
}

void GainMatchedSatAudioProcessorEditor::resized()
{
    // Position knobs over the toaster holes
    int dialSize = 175;
    driveSlider.setBounds(96, 178, dialSize, dialSize);
    mixSlider.setBounds(312, 178, dialSize, dialSize);
    outputSlider.setBounds(525, 178, dialSize, dialSize);
}
"""
ed_cpp = re.sub(r'void GainMatchedSatAudioProcessorEditor::resized\(\)[\s\S]*?\}', resized, ed_cpp)

with open(ed_cpp_path, "w") as f:
    f.write(ed_cpp)

