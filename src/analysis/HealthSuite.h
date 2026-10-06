#pragma once

#include <JuceHeader.h>

#include <atomic>
#include <functional>
#include <vector>

namespace vibecheck
{
enum class Verdict
{
    pending,   ///< Not run yet.
    pass,
    warn,      ///< Works, but something deserves a look.
    fail,      ///< Broken in a way a user could hit.
    info       ///< A fact, not a judgement.
};

/** One robustness test and what it found. */
struct HealthTest
{
    juce::String id;
    juce::String title;
    juce::String explain;      ///< Why this matters, in a sentence a plugin user would follow.
    juce::String result;       ///< What was found.
    Verdict verdict = Verdict::pending;
    double milliseconds = 0.0;
};

struct HealthReport
{
    std::vector<HealthTest> tests;
    double seconds = 0.0;
    bool stopped = false;
    bool instrument = false;

    int count (Verdict verdict) const;

    /** "Healthy", "Mostly healthy" or "Has problems". */
    juce::String headline() const;

    juce::String toText (const juce::String& pluginName) const;
};

/** An instrument is played with MIDI rather than fed audio, and is tested that way. */
bool looksLikeInstrument (const juce::AudioPluginInstance& plugin);

/** The tests that will run for a plugin, in order, all still pending. Lets a page list them
    before anything has happened. */
std::vector<HealthTest> plannedHealthTests (bool instrument);

/** Runs every test against a loaded plugin, with the plugin's own parameters put back afterwards.

    This is robustness, not sound quality: does it produce finite output, behave the same at any
    buffer size, keep its promises about latency, survive random settings and save its state. It
    blocks for a while, so call it from a worker thread. `onTest` is told as each test finishes. */
HealthReport runHealthSuite (juce::AudioPluginInstance& plugin,
                             const std::atomic<bool>& cancel,
                             const std::function<void (int index, int total, const HealthTest&)>& onTest = {},
                             const std::function<void (const juce::String& next)>& onStart = {});
} // namespace vibecheck
