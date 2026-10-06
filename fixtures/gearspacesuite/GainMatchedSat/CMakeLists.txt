cmake_minimum_required(VERSION 3.20)
project(GainMatchedSat VERSION 1.0.0)

juce_add_plugin(GainMatchedSat
    COMPANY_NAME "Gearspace Suite"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE "Gear"
    PLUGIN_CODE "RtS1"
    FORMATS AU VST3 Standalone # AAX requires Avid SDK
    PRODUCT_NAME "RetroSauce"
)

# Add binary data
juce_add_binary_data(GainMatchedSat_Data
    HEADER_NAME "BinaryData.h"
    NAMESPACE BinaryData
    SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Resources/bg.jpg"
)

target_sources(GainMatchedSat PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_compile_definitions(GainMatchedSat PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(GainMatchedSat PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    GainMatchedSat_Data
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

juce_generate_juce_header(GainMatchedSat)
