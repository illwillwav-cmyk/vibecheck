import os

plugins = [
    "AntiLoudnessLimiter",
    "ZeroDistortionEQ",
    "PhasePerfectAligner",
    "ScalableCompressor",
    "NoDongleReverb",
    "FreeDelay",
    "GainMatchedSat",
    "CPUFriendlySynth",
    "RealTimeAnalyzer",
    "UpdateProofUtility"
]

root_dir = "/Users/williamwright/dev stuff/GearspaceSuite"

for p in plugins:
    cmake_path = os.path.join(root_dir, p, "CMakeLists.txt")
    with open(cmake_path, "r") as f:
        content = f.read()
    if "juce_generate_juce_header" not in content:
        content += f"\njuce_generate_juce_header({p})\n"
    with open(cmake_path, "w") as f:
        f.write(content)

# Add Vibe to GainMatchedSat
sat_cpp_path = os.path.join(root_dir, "GainMatchedSat/Source/PluginProcessor.cpp")
with open(sat_cpp_path, "r") as f:
    sat_cpp = f.read()
sat_cpp = sat_cpp.replace(
    "buffer.clear (i, 0, buffer.getNumSamples());",
    "auto* channelData = buffer.getWritePointer (i);\n        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {\n            channelData[sample] = std::tanh(channelData[sample] * 2.0f) * 0.5f;\n        }"
)
with open(sat_cpp_path, "w") as f:
    f.write(sat_cpp)

