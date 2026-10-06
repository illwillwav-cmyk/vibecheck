#pragma once

#include <JuceHeader.h>

/** Owns the plugin formats, the scanned-plugin list and its persistence.

    Scanning runs out-of-process (see ScanWorker): a plugin that crashes or hangs while being
    probed kills only the worker, and the app records the failure and carries on. Instantiation
    for audio and UI, added in Phase 2, will still happen in-process. */
class PluginScanner final : private juce::ChangeListener
{
public:
    PluginScanner();
    ~PluginScanner() override;

    juce::AudioPluginFormatManager& getFormatManager()   { return formatManager; }
    juce::KnownPluginList&          getKnownPluginList() { return knownPluginList; }
    juce::PropertiesFile*           getSettings()        { return appProperties.getUserSettings(); }

    /** Marker file that lets a scan recover after a plugin crashes the worker mid-probe. */
    juce::File getDeadMansPedalFile();

    void save();

    /** Forgets every plugin that was skipped for crashing a scan, so they are all probed again.

        The plugins on that list are there because they took a worker process down with them, so
        this invites that to happen again: it exists for the case where a plugin has since been
        updated or repaired. */
    void clearSkippedPlugins();

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    juce::ApplicationProperties appProperties;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginScanner)
};
