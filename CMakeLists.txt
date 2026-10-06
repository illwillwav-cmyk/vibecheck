cmake_minimum_required(VERSION 3.22)

# The version below is bumped automatically by .githooks/pre-commit on every commit.
project(VibeCheck VERSION 0.5.1 LANGUAGES C CXX)

# Windows: link the C++ runtime statically, so the app runs on a machine that has never had the
# Visual C++ redistributable installed. Must be set before the target is created.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Target architectures")

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CPM.cmake)

CPMAddPackage(
    NAME JUCE
    GITHUB_REPOSITORY juce-framework/JUCE
    GIT_TAG 8.0.9
    EXCLUDE_FROM_ALL YES
)

if(APPLE)
    set(VIBECHECK_HOST_AU 1)
else()
    set(VIBECHECK_HOST_AU 0)
endif()

juce_add_gui_app(VibeCheck
    PRODUCT_NAME            "VibeCheck"
    COMPANY_NAME            "William Wright"
    ICON_MAC                "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon/VibeCheck.icns"
    ICON_BIG                "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon/icon_win_512.png"
    BUNDLE_ID               "com.williamwright.vibecheck"
    VERSION                 ${PROJECT_VERSION}
    NEEDS_CURL              FALSE
    NEEDS_WEB_BROWSER       FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "VibeCheck needs audio input to audition plugins."
)

# Where the app looks for news of a newer version (a small latest.json). Empty means it never checks.
# The release workflow sets this to the address of the file it publishes with each release.
set(VIBECHECK_UPDATE_URL "" CACHE STRING "Address of latest.json, for the update notice")

juce_generate_juce_header(VibeCheck)

# The studio's marks, embedded so the app carries its own branding.
juce_add_binary_data(VibeCheckAssets SOURCES
    assets/brand/seal_ink.png
    assets/brand/seal_paper.png
    assets/brand/wordmark_ink.png
    assets/brand/wordmark_paper.png
)

target_sources(VibeCheck PRIVATE
    src/Main.cpp
    src/MainComponent.cpp
    src/analysis/Fft.cpp
    src/analysis/BehaviourProbe.cpp
    src/analysis/HealthSuite.cpp
    src/analysis/MeasureSuite.cpp
    src/analysis/Measurements.cpp
    src/analysis/SelfTest.cpp
    src/audio/OfflineRenderer.cpp
    src/audio/TestSignal.cpp
    src/host/ConsoleAttach.cpp
    src/host/HeadlessAnalyze.cpp
    src/host/HeadlessScan.cpp
    src/host/LibraryScan.cpp
    src/host/HeadlessBehaviour.cpp
    src/host/HeadlessHealth.cpp
    src/host/HeadlessVibeCheck.cpp
    src/host/PluginLoader.cpp
    src/host/PluginScanner.cpp
    src/host/ScanWorker.cpp
    src/ui/ParameterPanel.cpp
    src/ui/PluginBrowserTab.cpp
    src/ui/AnalysisTab.cpp
    src/ui/AppIcon.cpp
    src/ui/ComparisonTab.cpp
    src/ui/HealthTab.cpp
    src/ui/PerformanceTab.cpp
    src/ui/BrandAssets.cpp
    src/ui/MbsLookAndFeel.cpp
    src/ui/SplashScreen.cpp
    src/ui/Theme.cpp
    src/ui/PlaceholderTab.cpp
    src/ui/PluginChooser.cpp
    src/ui/PluginPicker.cpp
    src/ui/Widgets.cpp
    src/ui/Plots.cpp
    src/ui/VibeCheckTab.cpp
    src/vibecheck/BehaviourCache.cpp
    src/vibecheck/BinaryInspector.cpp
    src/vibecheck/Export.cpp
    src/vibecheck/Heuristics.cpp
    src/vibecheck/LibrarySweep.cpp
    src/vibecheck/MachOReader.cpp
    src/vibecheck/PEReader.cpp
    src/vibecheck/PluginLookup.cpp
    src/vibecheck/UpdateCheck.cpp
)

target_include_directories(VibeCheck PRIVATE src)

target_compile_definitions(VibeCheck PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:VibeCheck,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:VibeCheck,JUCE_VERSION>"
    VIBECHECK_UPDATE_URL="${VIBECHECK_UPDATE_URL}"

    # Plugin hosting. AudioUnit exists only on Apple platforms; Windows hosts VST3.
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=${VIBECHECK_HOST_AU}
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_LADSPA=0
)

if(MSVC)
    # The sources hold UTF-8 text, and the template-heavy JUCE headers produce large object files.
    target_compile_options(VibeCheck PRIVATE /utf-8 /bigobj)
endif()

target_link_libraries(VibeCheck PRIVATE
    VibeCheckAssets
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# Known-provenance plugins for testing the heuristic engine. Off by default: each one compiles
# its own copy of JUCE.
option(VIBECHECK_BUILD_FIXTURES "Build the known-provenance test plugins" OFF)

if(VIBECHECK_BUILD_FIXTURES)
    add_subdirectory(fixtures)
endif()
