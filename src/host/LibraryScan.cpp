#include "LibraryScan.h"

namespace vibecheck
{
int scanForNewPlugins (PluginScanner& scanner,
                       const std::function<void (const ScanProgress&)>& onProgress,
                       const std::function<bool()>& shouldStop)
{
    auto& list = scanner.getKnownPluginList();
    const auto before = list.getTypes().size();

    for (auto* format : scanner.getFormatManager().getFormats())
    {
        if (shouldStop != nullptr && shouldStop())
            break;

        juce::PluginDirectoryScanner directoryScanner (list,
                                                       *format,
                                                       format->getDefaultLocationsToSearch(),
                                                       true,
                                                       scanner.getDeadMansPedalFile(),
                                                       false);

        juce::String pluginBeingScanned;

        while (directoryScanner.scanNextFile (true, pluginBeingScanned))
        {
            if (shouldStop != nullptr && shouldStop())
                return list.getTypes().size() - before;

            if (onProgress != nullptr)
                onProgress ({ format->getName(), pluginBeingScanned, directoryScanner.getProgress() });
        }
    }

    // Save straight away: a scan that found something, or blacklisted something that crashed,
    // must not lose that if the next probe brings the process down.
    scanner.save();

    return list.getTypes().size() - before;
}

BackgroundLibraryScan::BackgroundLibraryScan() : juce::Thread ("VibeCheck library scan") {}

BackgroundLibraryScan::~BackgroundLibraryScan()
{
    cancel();
}

void BackgroundLibraryScan::cancel()
{
    stopThread (15000);
}

void BackgroundLibraryScan::start (PluginScanner& scanner,
                                   std::function<void (const ScanProgress&)> onProgress,
                                   std::function<void (int)> onFinished)
{
    cancel();

    target = &scanner;
    progress = std::move (onProgress);
    finished = std::move (onFinished);

    startThread();
}

void BackgroundLibraryScan::run()
{
    if (target == nullptr)
        return;

    const auto added = scanForNewPlugins (*target,
                                          [this] (const ScanProgress& update)
                                          {
                                              if (progress != nullptr)
                                                  juce::MessageManager::callAsync ([callback = progress, update]
                                                                                   { callback (update); });
                                          },
                                          [this] { return threadShouldExit(); });

    if (threadShouldExit())
        return;

    if (finished != nullptr)
        juce::MessageManager::callAsync ([callback = finished, added] { callback (added); });
}
} // namespace vibecheck
