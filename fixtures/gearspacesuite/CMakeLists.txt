cmake_minimum_required(VERSION 3.20)
project(GearspaceSuite VERSION 1.0.0)

# Fetch JUCE
include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG master
)
FetchContent_MakeAvailable(juce)

# Add plugin subdirectories
add_subdirectory(AntiLoudnessLimiter)
add_subdirectory(ZeroDistortionEQ)
add_subdirectory(PhasePerfectAligner)
add_subdirectory(ScalableCompressor)
add_subdirectory(NoDongleReverb)
add_subdirectory(FreeDelay)
add_subdirectory(GainMatchedSat)
add_subdirectory(CPUFriendlySynth)
add_subdirectory(RealTimeAnalyzer)
add_subdirectory(UpdateProofUtility)
