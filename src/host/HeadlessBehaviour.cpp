#include "HeadlessBehaviour.h"

#include "PluginLoader.h"
#include "Watchdog.h"
#include "analysis/BehaviourProbe.h"
#include "vibecheck/PluginLookup.h"

#include <iostream>

HeadlessBehaviour::HeadlessBehaviour (PluginScanner& scannerToUse, juce::String pluginNameQuery, std::function<void (int)> onFinished)
    : juce::Thread ("VibeCheck headless behaviour"),
      scanner (scannerToUse),
      query (std::move (pluginNameQuery)),
      finished (std::move (onFinished))
{
}

HeadlessBehaviour::~HeadlessBehaviour()
{
    cancel.store (true);
    stopThread (10000);
}

void HeadlessBehaviour::start()
{
    startThread();
}

void HeadlessBehaviour::run()
{
    vibecheck::Watchdog watchdog (300);

    juce::PluginDescription description;

    // "id:" asks for one exact entry in the known list, which is how the app names a plugin when
    // two formats share a name.
    bool found = false;

    if (query.startsWith ("id:"))
    {
        for (const auto& type : scanner.getKnownPluginList().getTypes())
            if (type.fileOrIdentifier == query.fromFirstOccurrenceOf ("id:", false, false))
            {
                description = type;
                found = true;
                break;
            }
    }
    else
    {
        found = vibecheck::lookupPlugin (scanner.getFormatManager(), scanner.getKnownPluginList(), query, description);
    }

    // A plugin opened straight from a file is not in the scanned list, so its id is simply its path.
    if (! found && query.startsWith ("id:"))
        found = vibecheck::lookupPlugin (scanner.getFormatManager(), scanner.getKnownPluginList(),
                                         query.fromFirstOccurrenceOf ("id:", false, false), description);

    if (! found)
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

    const auto report = vibecheck::probeBehaviour (*loaded.instance, cancel);
    std::cout << report.toText (description.name + "  [" + description.pluginFormatName + "]") << std::endl
              << report.toMachine() << std::endl;

    // Plugin instances expect to be destroyed on the message thread.
    auto* released = loaded.instance.release();
    const auto code = report.ok ? 0 : 1;

    juce::MessageManager::callAsync ([released, code, callback = finished]
                                     {
                                         delete released;
                                         callback (code);
                                     });
}
