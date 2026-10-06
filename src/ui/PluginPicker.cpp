#include "PluginPicker.h"

PluginPicker::PluginPicker (PluginScanner& scanner, const juce::String& hint)
    : pluginScanner (scanner)
{
    search.setTextToShowWhenEmpty ("Filter " + juce::String ("by name, maker or format"), mbs::theme().inkFaint);
    search.onTextChange = [this]
    {
        hasCurrent = false;
        chooser.setFilter (search.getText());
        chooser.populate (menu);
    };
    addAndMakeVisible (search);

    menu.setTextWhenNothingSelected (hint);
    menu.onChange = [this]
    {
        const auto index = chooser.sourceIndexForSelectedId (menu.getSelectedId());
        hasCurrent = juce::isPositiveAndBelow (index, source.size());

        if (hasCurrent)
            current = source[index];

        if (onSelectionChanged != nullptr)
            onSelectionChanged();
    };
    addAndMakeVisible (menu);

    pluginScanner.getKnownPluginList().addChangeListener (this);
    refresh();
}

PluginPicker::~PluginPicker()
{
    pluginScanner.getKnownPluginList().removeChangeListener (this);
}

void PluginPicker::resized()
{
    auto area = getLocalBounds();
    search.setBounds (area.removeFromTop (36));
    area.removeFromTop (8);
    menu.setBounds (area.removeFromTop (36));
}

void PluginPicker::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void PluginPicker::refresh()
{
    // Remember the choice by identity, so a rescan does not silently change which plugin is
    // about to be measured.
    source = pluginScanner.getKnownPluginList().getTypes();
    chooser.setSource (source);
    chooser.setFilter (search.getText());
    chooser.populate (menu);

    if (hasCurrent)
        setSelectionText (current.name + "  [" + current.pluginFormatName + "]");
}

bool PluginPicker::getSelected (juce::PluginDescription& result) const
{
    if (hasCurrent)
        result = current;

    return hasCurrent;
}

bool PluginPicker::selectByName (const juce::String& query, juce::PluginDescription& result)
{
    for (const auto& description : source)
        if (description.name.containsIgnoreCase (query))
        {
            result = description;
            current = description;
            hasCurrent = true;
            setSelectionText (description.name + "  [" + description.pluginFormatName + "]");
            return true;
        }

    return false;
}

void PluginPicker::setSelectionText (const juce::String& text)
{
    menu.setText (text, juce::dontSendNotification);
}

void PluginPicker::setEnabled (bool shouldBeEnabled)
{
    menu.setEnabled (shouldBeEnabled);
    search.setEnabled (shouldBeEnabled);
}
