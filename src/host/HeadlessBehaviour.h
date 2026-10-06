#pragma once

#include <JuceHeader.h>

#include "PluginScanner.h"

#include <functional>

/** Loads one plugin by name or path, measures how it behaves on the audio thread and prints the numbers.

    The exit status is 1 when any test fails, so a plugin's build can be checked in CI:
    `VibeCheck --health=/path/to/Plugin.vst3 && echo healthy`. */
class HeadlessBehaviour final : private juce::Thread
{
public:
    HeadlessBehaviour (PluginScanner& scannerToUse, juce::String pluginNameQuery, std::function<void (int)> onFinished);
    ~HeadlessBehaviour() override;

    void start();

private:
    void run() override;

    PluginScanner& scanner;
    juce::String query;
    std::function<void (int)> finished;
    std::atomic<bool> cancel { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeadlessBehaviour)
};
