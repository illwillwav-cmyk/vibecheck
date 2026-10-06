#include "HeadlessHealth.h"

#include "PluginLoader.h"
#include "Watchdog.h"
#include "analysis/HealthSuite.h"
#include "vibecheck/PluginLookup.h"

#include <iostream>

HeadlessHealth::HeadlessHealth (PluginScanner& scannerToUse, juce::String pluginNameQuery, std::function<void (int)> onFinished)
    : juce::Thread ("VibeCheck headless health"),
      scanner (scannerToUse),
      query (std::move (pluginNameQuery)),
      finished (std::move (onFinished))
{
}

HeadlessHealth::~HeadlessHealth()
{
    cancel.store (true);
    stopThread (10000);
}

void HeadlessHealth::start()
{
    startThread();
}

void HeadlessHealth::run()
{
    vibecheck::Watchdog watchdog (300);

    juce::PluginDescription description;

    if (! vibecheck::lookupPlugin (scanner.getFormatManager(), scanner.getKnownPluginList(), query, description))
    {
        std::cout << "nothing found matching \"" << query << "\" - run --scan first, or pass a path" << std::endl;
        juce::MessageManager::callAsync ([callback = finished] { callback (2); });
        return;
    }

    auto loaded = vibecheck::loadPlugin (scanner.getFormatManager(), description, 48000.0, 512);

    if (loaded.instance == nullptr)
    {
        std::cout << "could not load " << description.name << ": " << loaded.error << std::endl;
        juce::MessageManager::callAsync ([callback = finished] { callback (2); });
        return;
    }

    const auto report = vibecheck::runHealthSuite (*loaded.instance, cancel);
    std::cout << report.toText (description.name + "  [" + description.pluginFormatName + "]") << std::endl
              << "checked in " << juce::String (report.seconds, 1) << " s" << std::endl;

    // Plugin instances expect to be destroyed on the message thread.
    auto* released = loaded.instance.release();
    const auto code = report.count (vibecheck::Verdict::fail) > 0 ? 1 : 0;

    juce::MessageManager::callAsync ([released, code, callback = finished]
                                     {
                                         delete released;
                                         callback (code);
                                     });
}
