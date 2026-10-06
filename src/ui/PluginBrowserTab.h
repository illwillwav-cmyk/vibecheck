#pragma once

#include <JuceHeader.h>

#include "host/PluginScanner.h"
#include "ui/Widgets.h"

/** Lists every plugin found on the machine and drives rescans.

    The table itself is juce::PluginListComponent, which already handles the scan UI, the
    format-by-format rescan buttons and the dead-man's-pedal recovery. This page wraps it with a
    row of headline numbers and a card, so the state of the library is readable at a glance. */
class PluginBrowserTab final : public juce::Component,
                               private juce::ChangeListener
{
public:
    explicit PluginBrowserTab (PluginScanner& scanner);
    ~PluginBrowserTab() override;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updateStatus();

    PluginScanner& pluginScanner;
    juce::PluginListComponent listComponent;

    mbs::StatTile totalTile { "Plugins" }, formatTile { "Formats" }, makerTile { "Manufacturers" }, skippedTile { "Skipped" };
    juce::TextButton resetSkippedButton { "Reset skipped" };

    juce::Rectangle<int> cardBounds;
    juce::String cardNote;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginBrowserTab)
};
