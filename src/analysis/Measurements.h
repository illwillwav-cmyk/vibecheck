#pragma once

#include <JuceHeader.h>

#include <vector>

namespace vibecheck
{
struct Spectrum
{
    std::vector<double> frequencyHz;
    std::vector<double> magnitudeDb;
};

struct FrequencyResponse
{
    bool ok = false;
    juce::String error;

    std::vector<double> frequencyHz;
    std::vector<double> magnitudeDb;
    std::vector<double> phaseDegrees;   ///< Unwrapped, so a delay reads as a straight slope.
};

struct Harmonic
{
    int order = 0;
    double frequencyHz = 0.0;
    double levelDb = 0.0;      ///< Absolute, dBFS.
    double relativeDb = 0.0;   ///< Relative to the fundamental, dBc.
};

struct HarmonicResult
{
    bool ok = false;
    juce::String error;

    double fundamentalHz = 0.0;
    double fundamentalDb = 0.0;
    double thdPercent = 0.0;
    double thdPlusNPercent = 0.0;
    double noiseFloorDb = 0.0;

    std::vector<Harmonic> harmonics;
    Spectrum spectrum;

    /** Energy in the even and odd harmonics, each as a percentage of the fundamental. Even-order
        distortion is what asymmetric saturation (a tube, a transformer) adds; odd-order is what
        symmetric clipping adds. Which dominates says a good deal about the character. */
    double evenPercent = 0.0;
    double oddPercent = 0.0;
    juce::String character;

    juce::String summary() const;
};

struct ImdResult
{
    bool ok = false;
    juce::String error;

    double imdPercent = 0.0;
    double carrierDb = 0.0;
    std::vector<Harmonic> sidebands;
    Spectrum spectrum;

    juce::String summary() const;
};

struct TransferCurve
{
    bool ok = false;
    juce::String error;

    /** Input level against output level, in dB, split into the branch where the stimulus was
        getting louder and the branch where it was getting quieter. A compressor separates the
        two; the gap between them is its attack and release behaviour. */
    std::vector<double> risingInputDb, risingOutputDb;
    std::vector<double> fallingInputDb, fallingOutputDb;

    juce::String summary() const;
};

/** What a measured response says in numbers. */
struct ResponseSummary
{
    bool ok = false;
    double gainAt1kDb = 0.0;     ///< Insertion gain at 1 kHz: the plugin's overall level change.
    double lowCornerHz = 0.0;    ///< Where it falls 3 dB below the 1 kHz level, going down. 0 if it never does.
    double highCornerHz = 0.0;   ///< The same, going up.
    double peakDb = 0.0;         ///< Highest point of the curve, relative to the 1 kHz level.
    double peakHz = 0.0;
    double troughDb = 0.0;       ///< Lowest point between 40 Hz and 16 kHz, relative to 1 kHz.
    double groupDelayMs = 0.0;   ///< Phase slope at 1 kHz.
    juce::String description;    ///< A plain-language reading: flat, low-passed, bell, and so on.
};

/** Level statistics of a captured channel. */
struct LevelStats
{
    bool ok = false;
    double peakDb = -180.0;
    double rmsDb = -180.0;
    double crestDb = 0.0;
    double dcOffset = 0.0;       ///< Mean sample value; a plugin that adds DC wastes headroom.
};

/** Energy that is not a harmonic of the test tone. Distortion that creates frequencies above the
    sampling limit folds them back down as tones that are not harmonically related to anything,
    which sound harsh and inharmonic. A plugin that oversamples well keeps them far below the
    signal; one that does not lets them through. */
struct AliasResult
{
    bool ok = false;
    juce::String error;
    double toneHz = 0.0;
    double worstDbc = -180.0;   ///< The loudest non-harmonic component, relative to the tone.
    double worstHz = 0.0;
    juce::String description;
};

/** How long a plugin keeps ringing after an impulse. */
struct TailResult
{
    bool ok = false;
    double tailMs = 0.0;          ///< Time until the response stays below -60 dB of its peak.
    bool truncated = false;       ///< Still ringing when the capture ended: the true tail is longer.
};

/** What the ramp says about a compressor, limiter or saturator, read from its transfer curve. */
struct TransferAnalysis
{
    bool ok = false;
    double smallSignalGainDb = 0.0;   ///< Gain applied to quiet material, before anything engages.
    bool engages = false;             ///< Does the gain ever change with level?
    double thresholdDb = 0.0;         ///< Input level where gain has dropped by 1 dB.
    double ratio = 1.0;               ///< Input change per output change above the knee; large means limiting.
    double gainReductionAtTopDb = 0.0;
    double hysteresisDb = 0.0;        ///< Mean gap between the rising and falling branches: its timing.
    juce::String description;
};

/** Smooths a magnitude curve over a fraction of an octave (1/3 means third-octave), averaging in
    the power domain the way analysers do. Phase is carried over untouched. */
FrequencyResponse smoothResponse (const FrequencyResponse& response, double octaveFraction);

ResponseSummary summariseResponse (const FrequencyResponse& response);

/** The strongest frequency within a few percent of an expected one. A synth is rarely tuned
    exactly, and harmonic measurement needs the real fundamental, not the nominal one. */
double refineFrequency (const juce::AudioBuffer<float>& output, int channel, double sampleRate, double approximateHz,
                        double tolerance = 0.04);

AliasResult measureAliasing (const juce::AudioBuffer<float>& output, int channel, double sampleRate, double toneHz);

LevelStats measureLevels (const juce::AudioBuffer<float>& buffer, int channel, int skipSamples = 0);

TailResult measureTail (const juce::AudioBuffer<float>& impulseOutput, int channel, double sampleRate);

TransferAnalysis analyseTransfer (const TransferCurve& curve);

/** Transfer function of output over input. Works with any stimulus that excites the whole
    spectrum; an impulse is the usual choice. */
FrequencyResponse measureFrequencyResponse (const juce::AudioBuffer<float>& input,
                                            const juce::AudioBuffer<float>& output,
                                            int channel,
                                            double sampleRate);

HarmonicResult measureHarmonics (const juce::AudioBuffer<float>& output,
                                 int channel,
                                 double sampleRate,
                                 double fundamentalHz);

ImdResult measureIntermodulation (const juce::AudioBuffer<float>& output,
                                  int channel,
                                  double sampleRate,
                                  double lowToneHz,
                                  double highToneHz);

TransferCurve measureTransferCurve (const juce::AudioBuffer<float>& input,
                                    const juce::AudioBuffer<float>& output,
                                    int channel,
                                    double sampleRate);
} // namespace vibecheck
