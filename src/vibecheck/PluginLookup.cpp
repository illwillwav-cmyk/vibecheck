#include "PluginLookup.h"

namespace vibecheck
{
bool lookupPlugin (juce::AudioPluginFormatManager& formatManager,
                   const juce::KnownPluginList& knownPlugins,
                   const juce::String& query,
                   juce::PluginDescription& result)
{
    // A path is read where it sits, so a plugin does not have to be installed to be examined.
    if (const juce::File file (query); juce::File::isAbsolutePath (query) && file.exists())
    {
        juce::OwnedArray<juce::PluginDescription> found;

        for (auto* format : formatManager.getFormats())
            if (format->fileMightContainThisPluginType (query))
                format->findAllTypesForFile (found, query);

        if (found.isEmpty())
            return false;

        result = *found.getFirst();
        return true;
    }

    for (const auto& type : knownPlugins.getTypes())
        if (type.name.containsIgnoreCase (query))
        {
            result = type;
            return true;
        }

    return false;
}
} // namespace vibecheck
