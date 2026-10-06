#pragma once

#include <JuceHeader.h>

namespace vibecheck
{
struct LoadedPlugin
{
    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::String error;
};

/** Instantiates a plugin without ever blocking the message thread.

    Some formats insist on an unblocked message thread while they are being created
    (AudioPluginFormat::requiresUnblockedMessageThreadDuringCreation), so creation is always
    dispatched to the message thread and the calling thread waits for it. That means this must be
    called from a background thread - calling it on the message thread would deadlock. */
LoadedPlugin loadPlugin (juce::AudioPluginFormatManager& formatManager,
                         const juce::PluginDescription& description,
                         double sampleRate,
                         int blockSize,
                         int timeoutMs = 30000);
} // namespace vibecheck
