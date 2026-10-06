#pragma once

#include <JuceHeader.h>

namespace vibecheck
{
struct RenderResult
{
    bool ok = false;
    juce::String error;

    juce::AudioBuffer<float> output;    ///< Latency-compensated, same length as the input plus the tail.

    int reportedLatencySamples = 0;
    int numInputChannels = 0;
    int numOutputChannels = 0;
    double sampleRate = 0.0;
    int blockSize = 0;

    double renderSeconds = 0.0;
    double timesFasterThanRealtime = 0.0;

    float peak (int channel) const;
    float rms (int channel) const;
    juce::String summary() const;
};

/** Settings for one offline render. At namespace scope because a nested type with default
    member initializers cannot be used as a default argument inside its own enclosing class. */
struct RenderOptions
{
    double sampleRate = 48000.0;
    int blockSize = 512;

    /** Extra samples rendered after the input ends, to capture reverb and delay tails. */
    int tailSamples = 0;

    /** Trim the plugin's reported latency from the head of the output, so input and output
        line up sample for sample. */
    bool compensateLatency = true;

    /** MIDI to play during the render, with event positions in samples from the start. Needed to
        measure an instrument, which makes no sound from audio input alone. */
    juce::MidiBuffer midi;
};

/** Pushes a buffer through a plugin as fast as the CPU allows, with no audio device involved.

    Measurement wants repeatability above all else: the same input must give the same output every
    time, at whatever sample rate the test calls for, with no possibility of a dropout corrupting
    an FFT. That rules out running measurements through CoreAudio. A real device is only needed
    later, for auditioning. */
class OfflineRenderer
{
public:
    using Options = RenderOptions;

    static RenderResult render (juce::AudioPluginInstance& plugin,
                                const juce::AudioBuffer<float>& input,
                                const RenderOptions& options = RenderOptions{});
};
} // namespace vibecheck
