#pragma once

#include <JuceHeader.h>

#include "PluginScanner.h"

#include <functional>

/** Runs a full plugin scan with no UI and prints what it found, then reports back.

    This drives exactly the same path as the Plugins tab - PluginDirectoryScanner into
    KnownPluginList::scanAndAddFile into the out-of-process custom scanner - so it is a real
    test of the hosting foundation, and it makes the scan usable from a script or from CI. */
class HeadlessScan final : private juce::Thread
{
public:
    HeadlessScan (PluginScanner& scannerToUse, std::function<void()> onFinished);
    ~HeadlessScan() override;

    void start();

private:
    void run() override;

    PluginScanner& scanner;
    std::function<void()> finished;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeadlessScan)
};
