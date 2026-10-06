#pragma once

#include <JuceHeader.h>

namespace vibecheck
{
/** The stimulus shapes the analysis suite needs. Each one exists to expose a different
    property of the plugin under test. */
enum class SignalType
{
    impulse,     ///< Single full-scale delta. Magnitude and phase response (Phase 3).
    sine,        ///< One pure tone. Harmonic distortion.
    dualSine,    ///< SMPTE pair, 60 Hz and 7 kHz at 4:1. Intermodulation distortion.
    ramp,        ///< Tone with an envelope that rises then falls in dB. Compressor curves.
    whiteNoise,  ///< Broadband excitation.
    silence      ///< Control: reveals self-noise, hum and idle output.
};

struct SignalSpec
{
    SignalType type = SignalType::sine;

    double sampleRate = 48000.0;
    int numSamples = 48000;
    int numChannels = 2;

    double frequencyHz = 1000.0;
    double secondFrequencyHz = 7000.0;  ///< dualSine only; frequencyHz is the low tone there.
    double amplitudeDb = -6.0;

    double rampStartDb = -60.0;         ///< ramp only.
    double rampEndDb = 0.0;

    int impulsePositionSamples = 0;     ///< impulse only.
};

/** Renders the stimulus. Generated in double precision and written to a float buffer, which is
    what the overwhelming majority of plugins actually process. */
juce::AudioBuffer<float> generate (const SignalSpec& spec);

juce::String describe (const SignalSpec& spec);
juce::String toString (SignalType type);
} // namespace vibecheck
