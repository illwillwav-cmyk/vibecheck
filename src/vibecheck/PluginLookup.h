#pragma once

#include <JuceHeader.h>

namespace vibecheck
{
/** Resolves a query to a plugin description: either a name from the scanned list, or a path to a
    plugin that is not installed. Shared by the Vibe Check tab and the command line so both find
    plugins the same way. */
bool lookupPlugin (juce::AudioPluginFormatManager& formatManager,
                   const juce::KnownPluginList& knownPlugins,
                   const juce::String& query,
                   juce::PluginDescription& result);
} // namespace vibecheck
