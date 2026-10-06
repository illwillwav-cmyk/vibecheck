#pragma once

#include <JuceHeader.h>

#include "PluginScanner.h"

#include <functional>

/** Loads one plugin by name or path, runs the full health suite and prints the verdicts.

    The exit status is 1 when any test fails, so a plugin's build can be checked in CI:
    `VibeCheck --health=/path/to/Plugin.vst3 && echo healthy`. */
class HeadlessHealth final : private juce::Thread
{
public:
    HeadlessHealth (PluginScanner& scannerToUse, juce::String pluginNameQuery, std::function<void (int)> onFinished);
    ~HeadlessHealth() override;

    void start();

private:
    void run() override;

    PluginScanner& scanner;
    juce::String query;
    std::function<void (int)> finished;
    std::atomic<bool> cancel { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeadlessHealth)
};
