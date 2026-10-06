#pragma once

#include <JuceHeader.h>

#include "analysis/BehaviourProbe.h"

#include <optional>

namespace vibecheck
{
/** Remembers what the behaviour probe found for each plugin, against the file's modification time,
    so a plugin that has been updated is measured again. Both the library sweep and the detail view
    read from here, which is what keeps their scores equal. */
std::optional<BehaviourReport> findBehaviour (juce::PropertiesFile* settings, const juce::PluginDescription& description, juce::int64 modified);
void storeBehaviour (juce::PropertiesFile* settings, const juce::PluginDescription& description, juce::int64 modified, const BehaviourReport& report);

/** Measures a plugin by running this program again on it, so a plugin that crashes or hangs cannot
    take the app with it. Gives up after the timeout. Blocks, so call it from a worker thread. */
BehaviourReport measureInChildProcess (const juce::PluginDescription& description, int timeoutMs, const std::atomic<bool>& cancel);
} // namespace vibecheck
