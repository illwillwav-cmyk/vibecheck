#include "PluginChooser.h"

#include <map>

namespace vibecheck
{
namespace
{
constexpr int recentCount = 15;
constexpr int firstRecentId = 1;

/** AudioUnits are named by component codes rather than paths, so their files have to be found by
    matching those codes against the Info.plist of everything installed. Indexed once: doing it
    per plugin would mean hundreds of directory walks. */
const std::map<juce::String, juce::Time>& audioUnitTimes()
{
    static const std::map<juce::String, juce::Time> index = []
    {
        std::map<juce::String, juce::Time> times;

        const juce::Array<juce::File> folders {
            juce::File ("/Library/Audio/Plug-Ins/Components"),
            juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                .getChildFile ("Library/Audio/Plug-Ins/Components"),
            juce::File ("/System/Library/Components")
        };

        for (const auto& folder : folders)
        {
            if (! folder.isDirectory())
                continue;

            for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*.component", juce::File::findDirectories))
            {
                const auto bundle = entry.getFile();
                const auto plist = bundle.getChildFile ("Contents/Info.plist");

                if (! plist.existsAsFile())
                    continue;

                const auto when = bundle.getLastModificationTime();

                // Every four-character code in the plist is recorded. Type and manufacturer codes
                // come along too, which is harmless: only subtypes are ever looked up.
                for (const auto& piece : juce::StringArray::fromTokens (plist.loadFileAsString(), "<>\"", ""))
                    if (piece.length() == 4)
                        times[piece] = when;
            }
        }

        return times;
    }();

    return index;
}

juce::String recencyLabel (juce::Time when)
{
    if (when.toMilliseconds() <= 0)
        return {};

    const auto days = (int) (juce::Time::getCurrentTime() - when).inDays();

    if (days <= 0)  return "today";
    if (days == 1)  return "yesterday";
    if (days < 30)  return juce::String (days) + "d ago";
    if (days < 365) return juce::String (days / 30) + "mo ago";

    return juce::String (days / 365) + "y ago";
}

juce::String labelFor (const juce::PluginDescription& description, bool withDate)
{
    juce::String text;
    text << description.name << "  [" << description.pluginFormatName << "]";

    if (description.manufacturerName.isNotEmpty())
        text << "  " << description.manufacturerName;

    if (withDate)
        if (const auto age = recencyLabel (installTimeOf (description)); age.isNotEmpty())
            text << "  -  " << age;

    return text;
}

bool matches (const juce::PluginDescription& description, const juce::String& filter)
{
    return description.name.containsIgnoreCase (filter)
           || description.manufacturerName.containsIgnoreCase (filter)
           || description.pluginFormatName.containsIgnoreCase (filter)
           || description.category.containsIgnoreCase (filter);
}
} // namespace

juce::Time installTimeOf (const juce::PluginDescription& description)
{
    if (juce::File::isAbsolutePath (description.fileOrIdentifier))
        if (const juce::File file (description.fileOrIdentifier); file.exists())
            return file.getLastModificationTime();

    // "AudioUnit:Effects/aumf,74au,!UAD" - the middle code identifies the unit.
    auto codes = juce::StringArray::fromTokens (description.fileOrIdentifier.fromLastOccurrenceOf ("/", false, false), ",", "");
    codes.trim();

    if (codes.size() >= 2)
    {
        const auto& times = audioUnitTimes();

        if (const auto found = times.find (codes[1]); found != times.end())
            return found->second;
    }

    return description.lastInfoUpdateTime;
}

void PluginChooser::setSource (juce::Array<juce::PluginDescription> types)
{
    source = std::move (types);
    rebuild();
}

void PluginChooser::setFilter (juce::String text)
{
    filter = text.trim();
    rebuild();
}

void PluginChooser::rebuild()
{
    visible.clearQuick();
    visibleToSource.clearQuick();
    recent.clearQuick();

    for (int i = 0; i < source.size(); ++i)
    {
        if (filter.isNotEmpty() && ! matches (source[i], filter))
            continue;

        visible.add (source[i]);
        visibleToSource.add (i);
    }

    if (filter.isNotEmpty())
        return;

    juce::Array<int> order;

    for (int i = 0; i < visible.size(); ++i)
        order.add (i);

    std::stable_sort (order.begin(), order.end(), [this] (int a, int b)
    {
        return installTimeOf (visible[a]).toMilliseconds() > installTimeOf (visible[b]).toMilliseconds();
    });

    order.removeRange (juce::jmin (order.size(), recentCount), order.size());
    recent = order;
}

void PluginChooser::populate (juce::ComboBox& box) const
{
    box.clear (juce::dontSendNotification);

    auto* root = box.getRootMenu();

    if (root == nullptr)
        return;

    root->clear();

    if (visible.isEmpty())
    {
        box.setTextWhenNothingSelected (filter.isEmpty() ? "no plugins - scan first"
                                                         : "nothing matches \"" + filter + "\"");
        return;
    }

    box.setTextWhenNothingSelected (filter.isEmpty() ? "select a plugin"
                                                     : juce::String (visible.size()) + " matching \"" + filter + "\"");

    if (! recent.isEmpty())
    {
        root->addSectionHeader ("Recently installed");

        for (int position = 0; position < recent.size(); ++position)
            root->addItem (firstRecentId + position, labelFor (visible[recent[position]], true));

        root->addSeparator();
        root->addSectionHeader ("By manufacturer");
    }

    // JUCE builds the manufacturer submenus and numbers the items so a choice can be mapped
    // back to the plugin.
    juce::KnownPluginList::addToMenu (*root, visible, juce::KnownPluginList::SortMethod::sortByManufacturer);
}

int PluginChooser::sourceIndexForSelectedId (int selectedId) const
{
    if (selectedId <= 0)
        return -1;

    auto visibleIndex = juce::KnownPluginList::getIndexChosenByMenu (visible, selectedId);

    if (visibleIndex < 0)
    {
        // JUCE's own ids sit at a high base, so the small ids used by the recent section cannot
        // collide with them.
        const auto position = selectedId - firstRecentId;
        visibleIndex = juce::isPositiveAndBelow (position, recent.size()) ? recent[position] : -1;
    }

    return juce::isPositiveAndBelow (visibleIndex, visibleToSource.size()) ? visibleToSource[visibleIndex] : -1;
}
} // namespace vibecheck
