#pragma once

#include <JuceHeader.h>

#include "BinaryInspector.h"
#include "SourceInspector.h"
#include "analysis/BehaviourProbe.h"

#include <vector>

namespace vibecheck
{
enum class Family
{
    metadata,       ///< Identity a developer forgot to fill in.
    boilerplate,    ///< Template scaffolding left untouched.
    dspNaivety,     ///< Audio code written without regard for the audio thread.
    stringArtifact, ///< Text the author never meant to ship.
    behaviour,      ///< What the plugin does on the audio thread when it is actually run.
    source          ///< What its source code and repository show, where they are public.
};

/** Bump this whenever a heuristic or its weight changes. Sweep results are cached against it, so a
    stale score from an older rule set is never shown beside a fresh one. */
constexpr int heuristicsVersion = 7;

juce::String toString (Family family);

struct Evidence
{
    Family family;
    juce::String finding;       ///< What it means, in words.
    juce::String detail;        ///< The literal thing that was found.
    juce::String explanation;   ///< Why this points to an AI generator.
    double points = 0.0;        ///< How much it moved the score.
};

struct VibeReport
{
    /** False when the binary could not be read well enough to judge. The score is meaningless
        in that case and the UI says so instead of showing a number. */
    bool conclusive = false;

    double score = 0.0;        ///< 0 to 100.
    double confidence = 0.0;   ///< 0 to 1: how much of the binary could actually be read.

    juce::String headline;
    juce::String opacityReason;

    std::vector<Evidence> evidence;
    juce::StringArray humanSignals;   ///< Things that point the other way.

    /** Families that could not be judged at all, and why. Absence of evidence here means
        nothing was looked at, not that nothing was found. */
    juce::StringArray notEvaluated;

    /** Points contributed by one family of fingerprints. */
    double pointsFor (Family family) const
    {
        double total = 0.0;

        for (const auto& item : evidence)
            if (item.family == family)
                total += item.points;

        return total;
    }

    juce::String summary() const;
};

/** Weighs the fingerprints. Reads only what the inspector found - it never loads or runs code. */
VibeReport assessVibe (const BinaryFacts& facts, const juce::PluginDescription& description,
                       const BehaviourReport* behaviour = nullptr,
                       const SourceReport* source = nullptr);
} // namespace vibecheck
