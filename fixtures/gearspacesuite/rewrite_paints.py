import os
import re

plugins = ["ZeroDistortionEQ", "ScalableCompressor", "AntiLoudnessLimiter", "PhasePerfectAligner", "CPUFriendlySynth", "RealTimeAnalyzer", "UpdateProofUtility"]

for p in plugins:
    path = f"{p}/Source/PluginEditor.cpp"
    with open(path, 'r') as f:
        content = f.read()
    
    # Regex to replace everything inside the paint method BEFORE the first custom meter/slider drawing
    # or just replace the g.fillAll and generic text completely
    
    # Match the beginning of the paint function
    pattern = r'(void\s+[a-zA-Z0-9_]+::paint\s*\(\s*juce::Graphics&\s*g\s*\)\s*\{)(.*?)(//|\s+float|\s+juce::Colour|\s+g\.drawText\("DELAY)'
    
    def replacer(match):
        prefix = match.group(1)
        suffix = match.group(3)
        new_paint = """
    if (backgroundImage.isValid()) {
        g.drawImage(backgroundImage, getLocalBounds().toFloat());
    } else {
        g.fillAll(juce::Colours::black);
    }
"""
        return prefix + new_paint + suffix

    new_content = re.sub(pattern, replacer, content, flags=re.DOTALL)
    
    with open(path, 'w') as f:
        f.write(new_content)

print("Paints rewritten!")
