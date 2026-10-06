#pragma once

#include <JuceHeader.h>

#include "host/PluginScanner.h"
#include "ui/PluginChooser.h"
#include "ui/Widgets.h"

/** A search box over a menu of plugins, kept in step with the scanned library.

    Three pages need to pick a plugin, and each used to snapshot the library once at startup, so a
    page opened before the first scan finished showed an empty list for good. This listens to the
    library instead and rebuilds itself whenever it changes, keeping the current choice. */
class PluginPicker final : public juce::Component,
                           private juce::ChangeListener
{
public:
    explicit PluginPicker (PluginScanner& scanner, const juce::String& hint = "Select a plugin");
    ~PluginPicker() override;

    void resized() override;

    static constexpr int preferredHeight = 80;

    /** The plugin chosen in the menu, if any. */
    bool getSelected (juce::PluginDescription& result) const;

    /** Chooses the first plugin whose name contains the text. */
    bool selectByName (const juce::String& query, juce::PluginDescription& result);

    void setEnabled (bool shouldBeEnabled);
    void setSelectionText (const juce::String& text);

    std::function<void()> onSelectionChanged;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();

    PluginScanner& pluginScanner;
    vibecheck::PluginChooser chooser;
    juce::Array<juce::PluginDescription> source;
    juce::PluginDescription current;
    bool hasCurrent = false;

    mbs::SearchField search;
    juce::ComboBox menu;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginPicker)
};
