#pragma once

#include <JuceHeader.h>

#include "Heuristics.h"

#include <functional>
#include <vector>

namespace vibecheck
{
/** One fingerprint, kept short so it can be cached and exported with the score. */
struct SweepFinding
{
    juce::String family;
    juce::String finding;
    double points = 0.0;
};

/** One plugin's place in the library sweep. */
struct SweepEntry
{
    juce::PluginDescription description;
    double score = 0.0;
    double confidence = 0.0;
    bool conclusive = false;
    juce::String headline;
    int fingerprints = 0;
    bool deep = false;     ///< Its measured behaviour is part of the score.

    std::vector<SweepFinding> findings;
    juce::String binaryId;         ///< Identifies the binary itself, so the same build matches across machines.
    juce::String behaviourLine;    ///< BehaviourReport::toMachine(), when a deep check has been done.
    juce::String sourceJson;       ///< SourceReport::toJson(), when a source check has been done.
};

/** What the whole library looks like once every plugin has been weighed. */
struct SweepSummary
{
    int total = 0;
    int judged = 0;        ///< Plugins whose binaries could actually be read.
    int flagged = 0;       ///< Judged plugins scoring at or above the flag threshold.
    double averageScore = 0.0;

    /** Share of the plugins that could be judged which read as machine-generated. Deliberately
        not a share of the whole library: counting unreadable binaries as clean would understate
        it, and counting them as suspect would invent evidence that does not exist. */
    double flaggedPercent() const { return judged > 0 ? 100.0 * (double) flagged / (double) judged : 0.0; }

    juce::String line() const;
};

/** A plugin scoring this or higher is counted as flagged in the summary. */
constexpr double flagThreshold = 30.0;

SweepSummary summarise (const std::vector<SweepEntry>& entries);

/** A short identity for a plugin binary: its size and a hash of its first and last stretch. */
juce::String binaryFingerprint (const juce::File& file);

/** Weighs every plugin in the library, on a background thread.

    Each binary is read once even when several plugins share it - every Apple unit lives in the
    same CoreAudio bundle - and results are cached against the file's modification time, so a
    later sweep only has to look at what has changed. */
class LibrarySweep final : private juce::Thread
{
public:
    LibrarySweep();
    ~LibrarySweep() override;

    void start (juce::Array<juce::PluginDescription> types,
                juce::PropertiesFile* cacheFile,
                std::function<void (int done, int total, const std::vector<SweepEntry>& current)> onProgress,
                std::function<void (std::vector<SweepEntry>)> onFinished);

    void cancel();

private:
    void run() override;

    juce::Array<juce::PluginDescription> pending;
    juce::PropertiesFile* cache = nullptr;
    std::function<void (int, int, const std::vector<SweepEntry>&)> progress;
    std::function<void (std::vector<SweepEntry>)> finished;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibrarySweep)
};
} // namespace vibecheck
