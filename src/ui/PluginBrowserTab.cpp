#include "PluginBrowserTab.h"

#include "Theme.h"

#include <map>
#include <set>

PluginBrowserTab::PluginBrowserTab (PluginScanner& scanner)
    : pluginScanner (scanner),
      listComponent (scanner.getFormatManager(),
                     scanner.getKnownPluginList(),
                     scanner.getDeadMansPedalFile(),
                     scanner.getSettings(),
                     true)
{
    addAndMakeVisible (totalTile);
    addAndMakeVisible (formatTile);
    addAndMakeVisible (makerTile);
    addAndMakeVisible (skippedTile);
    addAndMakeVisible (listComponent);

    mbs::setButtonStyle (resetSkippedButton, mbs::ButtonStyle::ghost);
    resetSkippedButton.onClick = [this]
    {
        pluginScanner.clearSkippedPlugins();
        updateStatus();
    };
    addAndMakeVisible (resetSkippedButton);

    pluginScanner.getKnownPluginList().addChangeListener (this);
    updateStatus();
}

PluginBrowserTab::~PluginBrowserTab()
{
    pluginScanner.getKnownPluginList().removeChangeListener (this);
}

void PluginBrowserTab::resized()
{
    auto area = getLocalBounds();

    auto tiles = area.removeFromTop (96);
    const auto tileWidth = (tiles.getWidth() - 3 * mbs::gutter) / 4;

    for (auto* tile : { &totalTile, &formatTile, &makerTile, &skippedTile })
    {
        tile->setBounds (tiles.removeFromLeft (tileWidth));
        tiles.removeFromLeft (mbs::gutter);
    }

    area.removeFromTop (mbs::gutter);
    cardBounds = area;

    auto inner = cardBounds.reduced (18, 14);
    auto head = inner.removeFromTop (28);
    resetSkippedButton.setBounds (head.removeFromRight (120));
    inner.removeFromTop (4);
    listComponent.setBounds (inner);
}

void PluginBrowserTab::paint (juce::Graphics& g)
{
    mbs::drawCard (g, cardBounds.toFloat());

    auto head = cardBounds.reduced (18, 14).removeFromTop (28);
    head.removeFromRight (130);

    g.setColour (mbs::theme().inkMuted);
    g.setFont (mbs::brandFont (11.0f, true));
    mbs::drawTracked (g, "Installed plugins", head.removeFromLeft (170), 1.5f, juce::Justification::centredLeft);

    g.setColour (mbs::theme().inkFaint);
    g.setFont (mbs::brandFont (12.0f));
    g.drawText (cardNote, head, juce::Justification::centredLeft, true);
}

void PluginBrowserTab::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateStatus();
}

void PluginBrowserTab::updateStatus()
{
    const auto types = pluginScanner.getKnownPluginList().getTypes();

    std::map<juce::String, int> perFormat;
    std::set<juce::String> makers;

    for (const auto& type : types)
    {
        ++perFormat[type.pluginFormatName];

        if (type.manufacturerName.isNotEmpty())
            makers.insert (type.manufacturerName);
    }

    juce::StringArray parts;

    for (const auto& [format, count] : perFormat)
        parts.add (format + " " + mbs::num (count));

    const auto skipped = pluginScanner.getKnownPluginList().getBlacklistedFiles().size();
    resetSkippedButton.setEnabled (skipped > 0);

    totalTile.set (mbs::num (types.size()), types.isEmpty() ? "Nothing scanned yet" : "found on this Mac");
    formatTile.set (mbs::num ((int) perFormat.size()), parts.joinIntoString ("   "));
    makerTile.set (mbs::num ((int) makers.size()), "distinct makers");
    skippedTile.set (mbs::num (skipped), skipped > 0 ? "crashed or hung while scanning" : "nothing misbehaved",
                     skipped > 0 ? mbs::theme().warn : mbs::theme().good);

    cardNote = "Scanning runs in a separate process, so a plugin that crashes cannot take this app down.";
    repaint();
}
