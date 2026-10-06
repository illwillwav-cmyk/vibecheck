#include "HeadlessVibeCheck.h"

#include "vibecheck/BinaryInspector.h"
#include "vibecheck/Heuristics.h"
#include "vibecheck/PluginLookup.h"

#include <iostream>

HeadlessVibeCheck::HeadlessVibeCheck (PluginScanner& scannerToUse, juce::String pluginNameQuery, std::function<void()> onFinished)
    : juce::Thread ("VibeCheck heuristics"),
      scanner (scannerToUse),
      query (std::move (pluginNameQuery)),
      finished (std::move (onFinished))
{
}

HeadlessVibeCheck::~HeadlessVibeCheck()
{
    stopThread (10000);
}

void HeadlessVibeCheck::start()
{
    startThread();
}

void HeadlessVibeCheck::run()
{
    juce::PluginDescription description;

    if (! vibecheck::lookupPlugin (scanner.getFormatManager(), scanner.getKnownPluginList(), query, description))
    {
        std::cout << "nothing found matching \"" << query << "\" - run --scan first, or pass a path"
                  << std::endl;
        juce::MessageManager::callAsync (finished);
        return;
    }

    const auto* match = &description;

    std::cout << match->name << "  [" << match->pluginFormatName << "]  by " << match->manufacturerName
              << "  v" << match->version << std::endl;

    auto inspector = vibecheck::createBinaryInspector();
    const auto facts = inspector->inspect (*match);

    if (! facts.ok)
    {
        std::cout << "could not inspect: " << facts.error << std::endl;
        juce::MessageManager::callAsync (finished);
        return;
    }

    std::cout << "binary: " << facts.executable.getFullPathName()
              << "  (" << juce::File::descriptionOfSizeInBytes (facts.sizeInBytes) << ")" << std::endl
              << facts.definedSymbols.size() << " exported symbols, "
              << facts.undefinedSymbols.size() << " undefined, "
              << facts.strings.size() << " strings"
              << (facts.paceWrapped ? ", copy protected" : "")
              << (facts.stripped ? ", stripped" : "") << std::endl
              << std::endl
              << vibecheck::assessVibe (facts, *match).summary() << std::endl;

    juce::MessageManager::callAsync (finished);
}
