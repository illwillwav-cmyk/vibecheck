#include "HeadlessScan.h"

#include "LibraryScan.h"

#include <iostream>

namespace
{
void print (const juce::String& line)
{
    std::cout << line << std::endl;
}
}

HeadlessScan::HeadlessScan (PluginScanner& scannerToUse, std::function<void()> onFinished)
    : juce::Thread ("VibeCheck headless scan"),
      scanner (scannerToUse),
      finished (std::move (onFinished))
{
}

HeadlessScan::~HeadlessScan()
{
    stopThread (5000);
}

void HeadlessScan::start()
{
    startThread();
}

void HeadlessScan::run()
{
    auto& list = scanner.getKnownPluginList();

    juce::String lastFormat;

    const auto added = vibecheck::scanForNewPlugins (scanner,
                                                     [&lastFormat] (const vibecheck::ScanProgress& update)
                                                     {
                                                         if (update.formatName != lastFormat)
                                                         {
                                                             lastFormat = update.formatName;
                                                             print ("--- scanning " + update.formatName + " ---");
                                                         }
                                                     },
                                                     [this] { return threadShouldExit(); });

    if (threadShouldExit())
        return;

    print ("--- result ---");
    print (juce::String (added) + " newly found");

    std::map<juce::String, int> perFormat;

    for (const auto& type : list.getTypes())
        ++perFormat[type.pluginFormatName];

    print ("--- result ---");

    for (const auto& [formatName, count] : perFormat)
        print (juce::String (count) + " " + formatName);

    print (juce::String (list.getTypes().size()) + " plugins total, "
               + juce::String (list.getBlacklistedFiles().size()) + " blacklisted");

    for (const auto& blacklisted : list.getBlacklistedFiles())
        print ("  blacklisted: " + blacklisted);

    juce::MessageManager::callAsync (finished);
}
