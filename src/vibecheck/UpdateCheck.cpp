#include "UpdateCheck.h"

#ifndef VIBECHECK_UPDATE_URL
 #define VIBECHECK_UPDATE_URL ""
#endif

namespace vibecheck
{
namespace
{
juce::Array<int> numbersIn (juce::String version)
{
    version = version.trim();

    if (version.startsWithIgnoreCase ("v"))
        version = version.substring (1);

    version = version.upToFirstOccurrenceOf ("-", false, false).upToFirstOccurrenceOf ("+", false, false);

    juce::Array<int> numbers;

    for (const auto& part : juce::StringArray::fromTokens (version, ".", ""))
        numbers.add (part.getIntValue());

    return numbers;
}
} // namespace

juce::String updateUrl() { return juce::String (VIBECHECK_UPDATE_URL); }

bool isNewerVersion (const juce::String& candidate, const juce::String& current)
{
    const auto a = numbersIn (candidate), b = numbersIn (current);

    if (a.isEmpty() || b.isEmpty())
        return false;

    for (int i = 0; i < juce::jmax (a.size(), b.size()); ++i)
    {
        const auto x = i < a.size() ? a[i] : 0, y = i < b.size() ? b[i] : 0;

        if (x != y)
            return x > y;
    }

    return false;
}

UpdateInfo parseUpdateInfo (const juce::String& json, const juce::String& currentVersion)
{
    UpdateInfo info;
    const auto parsed = juce::JSON::parse (json);

    if (! parsed.isObject())
        return info;

    const auto version = parsed["version"].toString();

    if (! isNewerVersion (version, currentVersion))
        return info;

    const auto page = parsed["page"].toString();

    // Only ever offer a web address, never anything else the file might say.
    if (! (page.startsWith ("https://") || page.startsWith ("http://")))
        return info;

    info.available = true;
    info.version = version.startsWithIgnoreCase ("v") ? version.substring (1) : version;
    info.page = page;
    info.notes = parsed["notes"].toString().upToFirstOccurrenceOf ("\n", false, false).substring (0, 160);
    return info;
}

UpdateInfo checkForUpdate (const juce::String& url, const juce::String& currentVersion)
{
    // Secure addresses only, except for this machine, which lets the notice be tried without a server.
    const auto local = url.startsWith ("http://127.0.0.1") || url.startsWith ("http://localhost");

    if (! url.startsWith ("https://") && ! local)
        return {};

    const auto stream = juce::URL (url).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                               .withConnectionTimeoutMs (6000)
                                                               .withNumRedirectsToFollow (5));

    if (stream == nullptr)
        return {};

    // The file is tiny; refuse anything unreasonable rather than read it all.
    juce::MemoryBlock data;
    stream->readIntoMemoryBlock (data, 64 * 1024);
    return parseUpdateInfo (data.toString(), currentVersion);
}
} // namespace vibecheck
