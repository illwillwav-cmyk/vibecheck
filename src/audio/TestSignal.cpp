#include "TestSignal.h"

namespace vibecheck
{
namespace
{
double gainFor (double decibels)
{
    return juce::Decibels::decibelsToGain (decibels, -120.0);
}

/** Phase accumulation in double, so a long tone does not drift enough to pollute an FFT. */
void addTone (juce::AudioBuffer<float>& buffer, double sampleRate, double frequencyHz, double gain)
{
    const auto increment = juce::MathConstants<double>::twoPi * frequencyHz / sampleRate;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto value = (float) (std::sin (increment * (double) sample) * gain);

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.addSample (channel, sample, value);
    }
}
} // namespace

juce::AudioBuffer<float> generate (const SignalSpec& spec)
{
    juce::AudioBuffer<float> buffer (juce::jmax (1, spec.numChannels), juce::jmax (1, spec.numSamples));
    buffer.clear();

    const auto gain = gainFor (spec.amplitudeDb);

    switch (spec.type)
    {
        case SignalType::silence:
            break;

        case SignalType::impulse:
        {
            const auto position = juce::jlimit (0, buffer.getNumSamples() - 1, spec.impulsePositionSamples);

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.setSample (channel, position, (float) gain);

            break;
        }

        case SignalType::sine:
            addTone (buffer, spec.sampleRate, spec.frequencyHz, gain);
            break;

        case SignalType::dualSine:
            // SMPTE intermodulation: the low tone carries four times the amplitude of the high one.
            addTone (buffer, spec.sampleRate, spec.frequencyHz, gain * 0.8);
            addTone (buffer, spec.sampleRate, spec.secondFrequencyHz, gain * 0.2);
            break;

        case SignalType::ramp:
        {
            // A tone whose level climbs and then falls, so one pass shows both the static
            // transfer curve and the attack and release behaviour around it.
            addTone (buffer, spec.sampleRate, spec.frequencyHz, 1.0);

            const auto halfway = buffer.getNumSamples() / 2;

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const auto position = sample < halfway
                                        ? (double) sample / (double) juce::jmax (1, halfway)
                                        : 1.0 - (double) (sample - halfway) / (double) juce::jmax (1, buffer.getNumSamples() - halfway);

                const auto envelope = gainFor (spec.rampStartDb + position * (spec.rampEndDb - spec.rampStartDb));

                for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                    buffer.setSample (channel, sample, (float) (buffer.getSample (channel, sample) * envelope * gain));
            }

            break;
        }

        case SignalType::whiteNoise:
        {
            juce::Random random (20260917);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                    buffer.setSample (channel, sample, (float) (random.nextDouble() * 2.0 - 1.0) * (float) gain);

            break;
        }
    }

    return buffer;
}

juce::String toString (SignalType type)
{
    switch (type)
    {
        case SignalType::impulse:    return "impulse";
        case SignalType::sine:       return "sine";
        case SignalType::dualSine:   return "dual sine";
        case SignalType::ramp:       return "ramp";
        case SignalType::whiteNoise: return "white noise";
        case SignalType::silence:    return "silence";
    }

    return "unknown";
}

juce::String describe (const SignalSpec& spec)
{
    juce::String text = toString (spec.type);

    switch (spec.type)
    {
        case SignalType::sine:
            text << " " << juce::String (spec.frequencyHz, 1) << " Hz";
            break;

        case SignalType::dualSine:
            text << " " << juce::String (spec.frequencyHz, 1) << " Hz + "
                 << juce::String (spec.secondFrequencyHz, 1) << " Hz";
            break;

        case SignalType::ramp:
            text << " " << juce::String (spec.frequencyHz, 1) << " Hz, "
                 << juce::String (spec.rampStartDb, 1) << " to " << juce::String (spec.rampEndDb, 1) << " dB";
            break;

        case SignalType::impulse:
        case SignalType::whiteNoise:
        case SignalType::silence:
            break;
    }

    if (spec.type != SignalType::ramp && spec.type != SignalType::silence)
        text << " at " << juce::String (spec.amplitudeDb, 1) << " dBFS";

    text << ", " << juce::String (spec.numSamples) << " samples @ "
         << juce::String (spec.sampleRate / 1000.0, 1) << " kHz";

    return text;
}
} // namespace vibecheck
