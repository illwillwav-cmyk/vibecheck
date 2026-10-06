#pragma once

#include <JuceHeader.h>

#include "Measurements.h"

#include <functional>

namespace vibecheck
{
struct SuiteOptions
{
    double sampleRate = 48000.0;
    int blockSize = 512;
    double toneHz = 1000.0;   ///< The single tone used for harmonic distortion.

    /** Also measure distortion at a range of levels and frequencies. Slower, so off by default. */
    bool sweeps = false;

    int midiNote = 60;        ///< The note played to measure an instrument.
};

/** Every measurement the app can take of one plugin, from one set of renders. */
struct SuiteResult
{
    bool anyOk = false;
    juce::String error;

    FrequencyResponse response;
    ResponseSummary responseSummary;
    TailResult tail;
    HarmonicResult harmonics;
    ImdResult imd;
    TransferCurve transfer;
    TransferAnalysis transferAnalysis;
    LevelStats noise;

    /** An instrument makes no sound from audio input, so it is measured by playing a note. */
    bool instrument = false;
    double noteHz = 0.0;
    double noteOnsetMs = 0.0;       ///< Delay from note-on to sound.
    double noteReleaseMs = 0.0;     ///< How long it rings after note-off.

    AliasResult alias;

    /** Distortion against input level (at the test tone) and against frequency, in dBc. */
    bool sweepsRan = false;
    std::vector<double> sweepLevelDb, thdVsLevelDb, sweepFrequencyHz, thdVsFrequencyDb;

    int latencySamples = 0;
    double speedTimesRealtime = 0.0;   ///< How much faster than realtime the plugin renders.
    double seconds = 0.0;              ///< Wall time for the whole suite.
    double sampleRate = 48000.0;

    /** The plugin's output for each stimulus, latency-compensated. */
    juce::AudioBuffer<float> impulseOut, sineOut, dualOut, rampOut, silenceOut, noteOut;

    juce::String describeLatency() const;
};

/** Renders impulse, sine, two-tone, ramp and silence through a plugin and measures each.

    Blocks the calling thread for several seconds, so call it from a worker. The plugin must not
    be touched from anywhere else meanwhile. `progress` is told which stage is starting. */
SuiteResult runSuite (juce::AudioPluginInstance& plugin, const SuiteOptions& options,
                      const std::function<void (const juce::String&)>& progress = {});
} // namespace vibecheck
