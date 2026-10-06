#pragma once

#include <JuceHeader.h>

namespace vibecheck
{
/** Where the running build looks for news of a newer version. Set at build time from
    -DVIBECHECK_UPDATE_URL=..., which the release workflow fills in with the address of the
    `latest.json` it publishes. Empty in a local build, which then never touches the network. */
juce::String updateUrl();

struct UpdateInfo
{
    bool available = false;
    juce::String version;   ///< The newer version, e.g. "0.6.0".
    juce::String page;      ///< Where to download it.
    juce::String notes;     ///< One line about what changed, if the release says.
};

/** True when `candidate` is a later version than `current`. Compares dotted numbers, so 0.10.0 is
    later than 0.9.0, and ignores a leading "v" and anything after a dash. */
bool isNewerVersion (const juce::String& candidate, const juce::String& current);

/** Reads a latest.json document and says whether it describes a newer version than this one. */
UpdateInfo parseUpdateInfo (const juce::String& json, const juce::String& currentVersion);

/** Fetches the document and parses it. Gives up quickly and quietly: no network, a missing file or
    a bad answer all mean "no update to report". Sends nothing but an ordinary request for the file.
    Blocks, so call it from a worker thread. */
UpdateInfo checkForUpdate (const juce::String& url, const juce::String& currentVersion);
} // namespace vibecheck
