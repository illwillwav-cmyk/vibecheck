#pragma once

#include <JuceHeader.h>
#include <memory>

namespace vibecheck
{
/** Command-line token that marks a process as a plugin-scan worker. Shared by both
    sides of the connection: the coordinator in PluginScanner.cpp and the worker here. */
inline constexpr const char* scanProcessUID = "vibecheckPluginScan";
}

/** If this process was launched as a scan worker, returns the live worker; the app should
    then run headless and do nothing else. Returns nullptr for a normal app launch. */
std::unique_ptr<juce::ChildProcessWorker> createScanWorkerIfRequested (const juce::String& commandLine);
