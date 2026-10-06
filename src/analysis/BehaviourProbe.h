#pragma once

#include <JuceHeader.h>

#include <atomic>

namespace vibecheck
{
/** What a plugin does while it processes audio, as opposed to what its file contains.

    The binary can be made to look like anyone's work, but the audio thread cannot hide how it
    behaves. Code written without real-time discipline asks the allocator for memory inside
    processBlock, does heavy per-block set-up, or leaves denormals to chance, and all of that
    shows up as numbers here. None of it proves who wrote the code - a careful human can slip
    too - so it is evidence, weighed with the rest. */
struct BehaviourReport
{
    bool ok = false;
    juce::String error;
    bool instrument = false;
    bool allocationsMeasured = false;   ///< Only macOS can count allocations on the audio thread.

    // Memory allocated on the audio thread. A real-time safe plugin makes none once running.
    double allocationsPerBlock = 0.0;           ///< Steady input, parameters left alone.
    double fractionOfBlocksAllocating = 0.0;
    int    worstAllocationsInOneBlock = 0;
    double allocationsPerBlockAutomated = 0.0;  ///< With parameters moving every block.

    // Time per block.
    double cpuPercent = 0.0;           ///< Of one core at 48 kHz and 512 samples, mean.
    double spikeRatio = 0.0;           ///< Slowest 1% of blocks against the median.
    double smallBufferOverhead = 0.0;  ///< Cost per sample at 16-sample blocks against 512.
    double denormalSlowdown = 0.0;     ///< Near-silent input against normal input.

    double seconds = 0.0;

    juce::String toText (const juce::String& pluginName) const;

    /** One line of key=value pairs, for passing the result between processes and for the cache. */
    juce::String toMachine() const;
    static BehaviourReport fromMachine (const juce::String& line);
};

/** Bump when the probe changes what it measures, so cached results are taken again. */
constexpr int behaviourVersion = 1;

/** Runs the plugin through a few seconds of audio at several settings and measures it.
    Blocks for a few seconds, so call it from a worker thread. */
BehaviourReport probeBehaviour (juce::AudioPluginInstance& plugin, const std::atomic<bool>& cancel);
} // namespace vibecheck
