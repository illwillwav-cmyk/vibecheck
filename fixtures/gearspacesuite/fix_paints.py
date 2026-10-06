import os
import glob

def fix_paint(file_path):
    with open(file_path, 'r') as f:
        content = f.read()

    # Find the paint method
    if "void paint (juce::Graphics& g) override {" in content or "void paint(juce::Graphics& g) override {" in content or "void paint(juce::Graphics& g) {" in content or "void paint(juce::Graphics& g) override" in content or "void paint(juce::Graphics& g){" in content or "void paint (juce::Graphics& g) {" in content:
        print("Found paint in " + file_path)
    else:
        return

    # We can just replace the start of the paint method to include the drawImage
    # First, let's just replace all `g.fillAll` calls with the image drawing logic.
    new_code = """
    if (backgroundImage.isValid()) {
        g.drawImage(backgroundImage, getLocalBounds().toFloat());
    } else {
        g.fillAll(juce::Colours::black);
    }
"""
    # Replace the generic background drawing
    content = content.replace('    g.fillAll(juce::Colour(0xFF0A0A1A));\n    g.setColour(juce::Colour(0xAA101030));\n    g.fillRoundedRectangle(20,20,760,560,16);', new_code)
    content = content.replace('    g.fillAll(juce::Colour(0xFF0D0D1A));\n    g.setColour(juce::Colour(0xAA151530));g.fillRoundedRectangle(20,20,760,560,16);', new_code)
    content = content.replace('    g.fillAll(juce::Colour(0xFF0A0A1A));\n    g.setColour(juce::Colour(0xAA101030));g.fillRoundedRectangle(20,20,760,560,16);', new_code)
    content = content.replace('    g.fillAll(juce::Colour(0xFF050510));\n    g.setColour(juce::Colour(0xAA0A0A20));g.fillRoundedRectangle(20,20,760,560,16);', new_code)

    # Remove the generic text drawing
    content = content.replace('    g.setColour(juce::Colour(0xFF4D96FF));\n    g.setFont(juce::Font(32.0f));', '')
    content = content.replace('    g.setColour(juce::Colour(0xFFFF6B9D));g.setFont(juce::Font(32.0f));\n    g.drawText("SCALABLE COMPRESSOR",0,30,800,40,juce::Justification::centred);\n    g.setFont(14.0f);', '')
    content = content.replace('    g.setColour(juce::Colour(0xFFFF9F43));g.setFont(juce::Font(32.0f));\n    g.drawText("ANTI-LOUDNESS LIMITER",0,30,800,40,juce::Justification::centred);\n    g.setFont(14.0f);', '')
    content = content.replace('    g.setColour(juce::Colour(0xFF6BCB77));g.setFont(juce::Font(32.0f));\n    g.drawText("PHASE PERFECT ALIGNER",0,40,800,40,juce::Justification::centred);\n    g.setFont(14.0f);', '')
    content = content.replace('    g.setColour(juce::Colour(0xFFB388FF));g.setFont(juce::Font(32.0f));\n    g.drawText("CPU FRIENDLY SYNTH",0,40,800,40,juce::Justification::centred);\n    g.setFont(16.0f);g.drawText("8-voice sine + saw  |  Play MIDI notes",0,90,800,30,juce::Justification::centred);', '')
    content = content.replace('    g.setColour(juce::Colour(0xFF00FFB3));g.setFont(juce::Font(28.0f));\n    g.drawText("REAL-TIME ANALYZER",0,30,800,30,juce::Justification::centred);', '')
    content = content.replace('    g.setColour(juce::Colour(0xFF54A0FF));g.setFont(juce::Font(32.0f));\n    g.drawText("UPDATE-PROOF UTILITY",0,30,800,40,juce::Justification::centred);\n    g.setFont(14.0f);', '')

    with open(file_path, 'w') as f:
        f.write(content)

plugins = ["ZeroDistortionEQ", "ScalableCompressor", "AntiLoudnessLimiter", "PhasePerfectAligner", "CPUFriendlySynth", "RealTimeAnalyzer", "UpdateProofUtility"]
for p in plugins:
    fix_paint(f"{p}/Source/PluginEditor.cpp")
print("All PluginEditor.cpp files updated!")
