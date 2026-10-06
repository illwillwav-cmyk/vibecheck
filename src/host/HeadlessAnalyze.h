#pragma once

#include <JuceHeader.h>

#include "PluginScanner.h"

#include <functional>

/** Loads one plugin by name, pushes test signals through it offline and prints what came out.

    The UI equivalent lives in the Analysis tab; this exists so the audio path can be exercised
    and verified without a human clicking anything. */
class HeadlessAnalyze final : private juce::Thread
{
public:
    HeadlessAnalyze (PluginScanner& scannerToUse, juce::String pluginNameQuery, std::function<void()> onFinished);
    ~HeadlessAnalyze() override;

    void start();

private:
    void run() override;

    PluginScanner& scanner;
    juce::String query;
    std::function<void()> finished;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeadlessAnalyze)
};
