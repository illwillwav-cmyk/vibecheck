# Two plugins with known provenance, built only so the heuristic engine can be tested against
# something whose authorship is not in doubt. See README.md in this folder.

juce_add_plugin(TemplatePlugin
    PRODUCT_NAME        "TemplatePlugin"
    COMPANY_NAME        "Yourcompany"
    BUNDLE_ID           "com.yourcompany.templateplugin"
    VERSION             1.0.0
    PLUGIN_MANUFACTURER_CODE Manu
    PLUGIN_CODE         Tmpl
    FORMATS             VST3
    IS_SYNTH            FALSE
    NEEDS_MIDI_INPUT    FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(TemplatePlugin PRIVATE template-plugin/PluginProcessor.cpp)
target_compile_definitions(TemplatePlugin PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(TemplatePlugin PRIVATE juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags)

juce_add_plugin(CraftedPlugin
    PRODUCT_NAME        "Sway"
    COMPANY_NAME        "Wright Audio"
    BUNDLE_ID           "com.williamwright.sway"
    VERSION             2.3.1
    PLUGIN_MANUFACTURER_CODE Wrht
    PLUGIN_CODE         Sway
    FORMATS             VST3
    IS_SYNTH            FALSE
    NEEDS_MIDI_INPUT    FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(CraftedPlugin PRIVATE crafted-plugin/TremoloProcessor.cpp)
target_compile_definitions(CraftedPlugin PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(CraftedPlugin PRIVATE juce::juce_audio_utils "-framework Accelerate" PUBLIC juce::juce_recommended_config_flags)
