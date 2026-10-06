#include "PluginLoader.h"

namespace vibecheck
{
LoadedPlugin loadPlugin (juce::AudioPluginFormatManager& formatManager,
                         const juce::PluginDescription& description,
                         double sampleRate,
                         int blockSize,
                         int timeoutMs)
{
    // Waiting here would stall the very thread that has to service the creation callback.
    jassert (! juce::MessageManager::existsAndIsCurrentThread());

    struct Shared
    {
        juce::WaitableEvent done;
        std::unique_ptr<juce::AudioPluginInstance> instance;
        juce::String error;
    };

    auto shared = std::make_shared<Shared>();

    juce::MessageManager::callAsync ([&formatManager, description, sampleRate, blockSize, shared]
    {
        formatManager.createPluginInstanceAsync (description, sampleRate, blockSize,
                                                 [shared] (std::unique_ptr<juce::AudioPluginInstance> instance,
                                                           const juce::String& error)
                                                 {
                                                     shared->instance = std::move (instance);
                                                     shared->error = error;
                                                     shared->done.signal();
                                                 });
    });

    if (! shared->done.wait (timeoutMs))
        return { nullptr, "timed out after " + juce::String (timeoutMs / 1000) + "s waiting for the plugin to load" };

    if (shared->instance == nullptr && shared->error.isEmpty())
        return { nullptr, "the plugin could not be created" };

    return { std::move (shared->instance), shared->error };
}
} // namespace vibecheck
