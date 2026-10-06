#pragma once

#include <JuceHeader.h>

#include "PluginScanner.h"

#include <functional>

namespace vibecheck
{
struct ScanProgress
{
    juce::String formatName;
    juce::String pluginName;
    float fraction = 0.0f;
};

/** Looks through every format for plugins that are not already known.

    Plugins already in the list are skipped, so this is quick once the library has been seen
    before and is worth running at every launch to pick up new installs. Must be called from a
    background thread: each plugin is probed in a worker process and this waits for the answer. */
int scanForNewPlugins (PluginScanner& scanner,
                       const std::function<void (const ScanProgress&)>& onProgress,
                       const std::function<bool()>& shouldStop);

/** Runs scanForNewPlugins on its own thread and reports back on the message thread. */
class BackgroundLibraryScan final : private juce::Thread
{
public:
    BackgroundLibraryScan();
    ~BackgroundLibraryScan() override;

    void start (PluginScanner& scanner,
                std::function<void (const ScanProgress&)> onProgress,
                std::function<void (int added)> onFinished);

    void cancel();

private:
    void run() override;

    PluginScanner* target = nullptr;
    std::function<void (const ScanProgress&)> progress;
    std::function<void (int)> finished;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BackgroundLibraryScan)
};
} // namespace vibecheck
