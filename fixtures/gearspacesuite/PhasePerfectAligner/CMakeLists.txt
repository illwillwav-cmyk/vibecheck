cmake_minimum_required(VERSION 3.20)
project(PhasePerfectAligner VERSION 1.0.0)

juce_add_plugin(PhasePerfectAligner
    COMPANY_NAME "Gearspace Suite"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE "Gear"
    PLUGIN_CODE "GS02"
    FORMATS AU VST3 Standalone # AAX requires Avid SDK
    PRODUCT_NAME "PhasePerfectAligner"
)

# Add binary data
juce_add_binary_data(PhasePerfectAligner_Data
    HEADER_NAME "BinaryData.h"
    NAMESPACE BinaryData
    SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Resources/bg.jpg"
)

target_sources(PhasePerfectAligner PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_compile_definitions(PhasePerfectAligner PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(PhasePerfectAligner PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PhasePerfectAligner_Data
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

juce_generate_juce_header(PhasePerfectAligner)
