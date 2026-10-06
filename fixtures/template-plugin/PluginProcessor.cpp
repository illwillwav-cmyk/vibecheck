#include "PluginProcessor.h"

AudioPluginAudioProcessor::AudioPluginAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // Kept in a member so the linker cannot discard them: the point of this fixture is that
    // these strings end up in the binary where the heuristics can find them.
    notes.add ("Here is the implementation of the audio processing callback.");
    notes.add ("TODO: implement proper gain smoothing to avoid zipper noise.");
    notes.add ("Note: this is a simplified version for demonstration purposes.");
    notes.add ("Replace this with your own DSP code.");
}

void AudioPluginAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    phase = 0.0;
}

void AudioPluginAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // A tremolo written the naive way: a transcendental call per sample, per channel, on the
    // audio thread, with no wavetable and nothing vectorised.
    const double increment = 2.0 * 3.14159265358979323846 * 5.0 / currentSampleRate;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const float gain = (float) (0.5 + 0.5 * std::sin (phase));
        phase += increment;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample (channel, sample, buffer.getSample (channel, sample) * gain);
    }
}

juce::AudioProcessorEditor* AudioPluginAudioProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}
