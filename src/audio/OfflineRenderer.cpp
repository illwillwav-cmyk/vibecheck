#include "OfflineRenderer.h"

namespace vibecheck
{
float RenderResult::peak (int channel) const
{
    if (! juce::isPositiveAndBelow (channel, output.getNumChannels()))
        return 0.0f;

    return output.getMagnitude (channel, 0, output.getNumSamples());
}

float RenderResult::rms (int channel) const
{
    if (! juce::isPositiveAndBelow (channel, output.getNumChannels()))
        return 0.0f;

    return output.getRMSLevel (channel, 0, output.getNumSamples());
}

juce::String RenderResult::summary() const
{
    if (! ok)
        return "render failed: " + error;

    juce::String text;
    text << output.getNumSamples() << " samples, "
         << numInputChannels << " in / " << numOutputChannels << " out"
         << ", latency " << reportedLatencySamples << " samples";

    if (sampleRate > 0.0)
        text << " (" << juce::String (1000.0 * reportedLatencySamples / sampleRate, 2) << " ms)";

    for (int channel = 0; channel < output.getNumChannels(); ++channel)
        text << "\n  ch" << (channel + 1)
             << "  peak " << juce::String (juce::Decibels::gainToDecibels (peak (channel)), 2) << " dBFS"
             << "  rms " << juce::String (juce::Decibels::gainToDecibels (rms (channel)), 2) << " dBFS";

    text << "\nrendered in " << juce::String (renderSeconds, 3) << " s ("
         << juce::String (timesFasterThanRealtime, 1) << "x realtime, ~"
         << juce::String (timesFasterThanRealtime > 0.0 ? 100.0 / timesFasterThanRealtime : 0.0, 2) << "% CPU load)";

    return text;
}

RenderResult OfflineRenderer::render (juce::AudioPluginInstance& plugin,
                                      const juce::AudioBuffer<float>& input,
                                      const RenderOptions& options)
{
    RenderResult result;
    result.sampleRate = options.sampleRate;
    result.blockSize = options.blockSize;

    if (input.getNumSamples() <= 0)
    {
        result.error = "input buffer is empty";
        return result;
    }

    const auto requestedChannels = juce::jmax (1, input.getNumChannels());

    // Ask for plain stereo in and out on the main buses, with any extra bus (a sidechain, say)
    // switched off. Without this, a plugin that exposes a sidechain reports more input channels
    // than the test signal has, and the stimulus can end up on the wrong bus.
    {
        juce::MessageManagerLock mmLock;
    {
        auto layout = plugin.getBusesLayout();
        const auto mainSet = juce::AudioChannelSet::canonicalChannelSet (requestedChannels);

        for (int bus = 0; bus < layout.inputBuses.size(); ++bus)
            layout.inputBuses.getReference (bus) = bus == 0 ? mainSet : juce::AudioChannelSet::disabled();

        for (int bus = 0; bus < layout.outputBuses.size(); ++bus)
            layout.outputBuses.getReference (bus) = bus == 0 ? mainSet : juce::AudioChannelSet::disabled();

        if (plugin.checkBusesLayoutSupported (layout))
            plugin.setBusesLayout (layout);
    }

    plugin.setPlayConfigDetails (requestedChannels, requestedChannels, options.sampleRate, options.blockSize);
    plugin.setNonRealtime (true);
    plugin.prepareToPlay (options.sampleRate, options.blockSize);
    plugin.reset();
    }

    result.numInputChannels  = plugin.getTotalNumInputChannels();
    result.numOutputChannels = plugin.getTotalNumOutputChannels();
    result.reportedLatencySamples = plugin.getLatencySamples();

    const auto latency = options.compensateLatency ? juce::jmax (0, result.reportedLatencySamples) : 0;
    const auto channels = juce::jmax (result.numInputChannels, result.numOutputChannels, requestedChannels);
    const auto keptSamples = input.getNumSamples() + juce::jmax (0, options.tailSamples);

    // Render past the end of the input so that the latency trim and the tail both have real
    // samples to work with rather than truncating the plugin's output.
    const auto totalSamples = keptSamples + latency;

    juce::AudioBuffer<float> captured (channels, totalSamples);
    captured.clear();

    juce::AudioBuffer<float> scratch (channels, options.blockSize);
    juce::MidiBuffer midi;

    const auto startTime = juce::Time::getMillisecondCounterHiRes();

    for (int position = 0; position < totalSamples; position += options.blockSize)
    {
        const auto numSamples = juce::jmin (options.blockSize, totalSamples - position);

        scratch.clear();

        for (int channel = 0; channel < juce::jmin (channels, input.getNumChannels()); ++channel)
        {
            const auto available = juce::jlimit (0, numSamples, input.getNumSamples() - position);

            if (available > 0)
                scratch.copyFrom (channel, 0, input, channel, position, available);
        }

        // Hand the plugin only the events that fall inside this block, positioned relative to it.
        midi.clear();
        midi.addEvents (options.midi, position, numSamples, -position);

        juce::AudioBuffer<float> block (scratch.getArrayOfWritePointers(), channels, numSamples);
        plugin.processBlock (block, midi);

        for (int channel = 0; channel < channels; ++channel)
            captured.copyFrom (channel, position, block, channel, 0, numSamples);
    }

    result.renderSeconds = (juce::Time::getMillisecondCounterHiRes() - startTime) / 1000.0;

    {
        juce::MessageManagerLock mmLock;
        plugin.releaseResources();
    }

    const auto renderedSeconds = (double) totalSamples / options.sampleRate;
    result.timesFasterThanRealtime = result.renderSeconds > 0.0 ? renderedSeconds / result.renderSeconds : 0.0;

    // Drop the latency from the head, so output sample N corresponds to input sample N.
    result.output.setSize (juce::jmax (1, result.numOutputChannels > 0 ? result.numOutputChannels : channels),
                           keptSamples);
    result.output.clear();

    for (int channel = 0; channel < result.output.getNumChannels(); ++channel)
        if (channel < captured.getNumChannels())
            result.output.copyFrom (channel, 0, captured, channel, latency, keptSamples);

    result.ok = true;
    return result;
}
} // namespace vibecheck
