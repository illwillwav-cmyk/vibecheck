#pragma once

#include <JuceHeader.h>

namespace vibecheck
{
/** Drives a combo box over a large plugin library: searchable, and grouped so it can be browsed.

    With five hundred plugins a flat menu is unusable - it runs off the screen and most of it
    cannot be reached. Unfiltered, this shows the recently installed ones first and files the rest
    under their manufacturer. With a search term it shows the matches instead.

    It keeps the mapping from what is on screen back to the caller's own array, so a filtered menu
    still selects the right plugin. */
class PluginChooser
{
public:
    void setSource (juce::Array<juce::PluginDescription> types);
    void setFilter (juce::String text);

    juce::String getFilter() const   { return filter; }
    int getMatchCount() const        { return visibleToSource.size(); }
    int getSourceCount() const       { return source.size(); }

    /** Rebuilds the menu inside the box. */
    void populate (juce::ComboBox& box) const;

    /** Index into the array passed to setSource, or -1 if the id was not a plugin. */
    int sourceIndexForSelectedId (int selectedId) const;

private:
    void rebuild();

    juce::Array<juce::PluginDescription> source, visible;
    juce::Array<int> visibleToSource, recent;
    juce::String filter;
};

/** When the plugin's file was last written; a description does not carry it. */
juce::Time installTimeOf (const juce::PluginDescription& description);
} // namespace vibecheck
