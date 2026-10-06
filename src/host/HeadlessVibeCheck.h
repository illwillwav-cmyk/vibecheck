#pragma once

#include <JuceHeader.h>

#include "PluginScanner.h"

#include <functional>

/** Runs the binary heuristics over one plugin and prints the report.

    Unlike the analysis path, this never loads or runs the plugin - it only reads the file. */
class HeadlessVibeCheck final : private juce::Thread
{
public:
    HeadlessVibeCheck (PluginScanner& scannerToUse, juce::String pluginNameQuery, std::function<void()> onFinished);
    ~HeadlessVibeCheck() override;

    void start();

private:
    void run() override;

    PluginScanner& scanner;
    juce::String query;
    std::function<void()> finished;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeadlessVibeCheck)
};
