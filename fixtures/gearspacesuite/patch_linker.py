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
    content = content.replace("RECOMMENDED_CONFIG", "juce::juce_recommended_config_flags\n    juce::juce_recommended_warning_flags")
    with open(cmake_path, "w") as f:
        f.write(content)
