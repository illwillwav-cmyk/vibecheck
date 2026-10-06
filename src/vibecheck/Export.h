#pragma once

#include <JuceHeader.h>

#include "Labels.h"
#include "LibrarySweep.h"

#include <vector>

namespace vibecheck
{
/** Version of the export file layout. A newer file than this build understands is refused rather
    than half-read. */
constexpr int exportSchema = 1;

/** Everything about each weighed plugin that is worth sharing, and nothing about the machine it
    came from: no file paths, no user name, no host name. A plugin is identified by what it is
    (name, maker, format, version and a short fingerprint of its binary), so the same build
    reported by two people lands on the same row of a master list. */
juce::var buildExport (const std::vector<SweepEntry>& entries, const juce::String& appVersion,
                       const std::map<juce::String, Label>& labels = {});

juce::Result writeExport (const juce::File& destination, const std::vector<SweepEntry>& entries, const juce::String& appVersion,
                          const std::map<juce::String, Label>& labels = {});

/** The outcome of folding several exports into one list. */
struct MergeResult
{
    juce::var master;
    int filesRead = 0;
    int pluginsSeen = 0;      ///< Rows across all files, before they were combined.
    int pluginsMerged = 0;    ///< Rows in the master list.
    juce::StringArray problems;
};

MergeResult mergeExports (const juce::Array<juce::File>& files);

/** The master list as CSV, for a spreadsheet. */
juce::String masterToCsv (const juce::var& master);

/** Reads a path that may be one export, a comma-separated list, or a folder of them. */
juce::Array<juce::File> exportFilesFrom (const juce::String& argument);
} // namespace vibecheck
