#include "Measurements.h"

#include "Fft.h"

#include <algorithm>
#include <cmath>

namespace vibecheck
{
namespace
{
constexpr double minimumDb = -180.0;

juce::String oneDecimal (double value)
{
    return juce::String (std::round (value * 10.0) / 10.0, 1);
}

double toDb (double amplitude)
{
    return amplitude > 0.0 ? juce::jmax (minimumDb, 20.0 * std::log10 (amplitude)) : minimumDb;
}

/** Copies a channel into a double vector, optionally skipping a settling region at the start,
    trimmed to a power of two so the radix-2 transform can be used. */
std::vector<double> takeAnalysisWindow (const juce::AudioBuffer<float>& buffer, int channel, int skipSamples)
{
    if (! juce::isPositiveAndBelow (channel, buffer.getNumChannels()))
        return {};

    const auto available = buffer.getNumSamples() - skipSamples;

    if (available <= 0)
        return {};

    const auto length = largestPowerOfTwoAtMost ((std::size_t) available);
    std::vector<double> samples (length);

    const auto* source = buffer.getReadPointer (channel);

    for (std::size_t i = 0; i < length; ++i)
        samples[i] = (double) source[skipSamples + (int) i];

    return samples;
}

struct AnalysedSpectrum
{
    /** Per-bin amplitudes, scaled for display. */
    std::vector<double> amplitudes;

    /** Applied after summing power across a lobe; see WindowGains::lobeCorrection. */
    double lobeCorrection = 1.0;
};

AnalysedSpectrum amplitudeSpectrum (std::vector<double> samples, Window window)
{
    const auto gains = applyWindow (samples, window);
    const auto coherentGain = gains.coherent;
    const auto n = samples.size();

    std::vector<Complex> spectrum (n);

    for (std::size_t i = 0; i < n; ++i)
        spectrum[i] = Complex { samples[i], 0.0 };

    fft (spectrum);

    std::vector<double> amplitudes (n / 2);
    const auto scale = 2.0 / ((double) n * juce::jmax (1.0e-12, coherentGain));

    for (std::size_t bin = 0; bin < amplitudes.size(); ++bin)
        amplitudes[bin] = std::abs (spectrum[bin]) * scale;

    return { std::move (amplitudes), gains.lobeCorrection() };
}

int binForFrequency (double frequencyHz, double sampleRate, std::size_t fftSize)
{
    return (int) std::lround (frequencyHz * (double) fftSize / sampleRate);
}

/** Amplitude of a component that the window has smeared across neighbouring bins.

    A tone almost never lands exactly on a bin, so its energy has to be gathered from the whole
    main lobe. Summing power that way needs the window's RMS gain rather than its coherent gain,
    hence the correction - without it every level reads 3 dB high. */
double amplitudeAround (const AnalysedSpectrum& spectrum, int centreBin, int halfWidth)
{
    const auto& amplitudes = spectrum.amplitudes;
    const auto first = juce::jmax (0, centreBin - halfWidth);
    const auto last  = juce::jmin ((int) amplitudes.size() - 1, centreBin + halfWidth);

    double sumOfSquares = 0.0;

    for (int bin = first; bin <= last; ++bin)
        sumOfSquares += amplitudes[(std::size_t) bin] * amplitudes[(std::size_t) bin];

    return std::sqrt (sumOfSquares) * spectrum.lobeCorrection;
}

Spectrum buildSpectrum (const std::vector<double>& amplitudes, double sampleRate, std::size_t fftSize)
{
    Spectrum spectrum;
    spectrum.frequencyHz.reserve (amplitudes.size());
    spectrum.magnitudeDb.reserve (amplitudes.size());

    for (std::size_t bin = 1; bin < amplitudes.size(); ++bin)
    {
        const auto frequency = (double) bin * sampleRate / (double) fftSize;

        if (frequency < 10.0)
            continue;

        spectrum.frequencyHz.push_back (frequency);
        spectrum.magnitudeDb.push_back (toDb (amplitudes[bin]));
    }

    return spectrum;
}
} // namespace

FrequencyResponse measureFrequencyResponse (const juce::AudioBuffer<float>& input,
                                            const juce::AudioBuffer<float>& output,
                                            int channel,
                                            double sampleRate)
{
    FrequencyResponse response;

    auto inputSamples  = takeAnalysisWindow (input, juce::jmin (channel, input.getNumChannels() - 1), 0);
    auto outputSamples = takeAnalysisWindow (output, channel, 0);

    if (inputSamples.empty() || outputSamples.empty())
    {
        response.error = "not enough samples to analyse";
        return response;
    }

    const auto n = juce::jmin (inputSamples.size(), outputSamples.size());
    inputSamples.resize (n);
    outputSamples.resize (n);

    std::vector<Complex> inputSpectrum (n), outputSpectrum (n);

    for (std::size_t i = 0; i < n; ++i)
    {
        inputSpectrum[i]  = Complex { inputSamples[i], 0.0 };
        outputSpectrum[i] = Complex { outputSamples[i], 0.0 };
    }

    fft (inputSpectrum);
    fft (outputSpectrum);

    // A bin where the stimulus has no energy says nothing about the plugin, so ignore it.
    double largestInput = 0.0;

    for (std::size_t bin = 1; bin < n / 2; ++bin)
        largestInput = juce::jmax (largestInput, std::abs (inputSpectrum[bin]));

    if (largestInput <= 0.0)
    {
        response.error = "the stimulus contains no energy";
        return response;
    }

    const auto threshold = largestInput * 1.0e-6;
    double previousPhase = 0.0;
    double unwrapOffset = 0.0;
    bool first = true;

    for (std::size_t bin = 1; bin < n / 2; ++bin)
    {
        const auto frequency = (double) bin * sampleRate / (double) n;

        if (frequency < 10.0 || frequency > sampleRate * 0.5)
            continue;

        if (std::abs (inputSpectrum[bin]) < threshold)
            continue;

        const auto transfer = outputSpectrum[bin] / inputSpectrum[bin];
        auto phase = std::arg (transfer);

        if (! first)
        {
            // Unwrap, so a plain delay shows up as a straight line rather than a sawtooth.
            const auto delta = phase + unwrapOffset - previousPhase;

            if (delta > juce::MathConstants<double>::pi)
                unwrapOffset -= juce::MathConstants<double>::twoPi;
            else if (delta < -juce::MathConstants<double>::pi)
                unwrapOffset += juce::MathConstants<double>::twoPi;
        }

        first = false;
        previousPhase = phase + unwrapOffset;

        response.frequencyHz.push_back (frequency);
        response.magnitudeDb.push_back (toDb (std::abs (transfer)));
        response.phaseDegrees.push_back (previousPhase * 180.0 / juce::MathConstants<double>::pi);
    }

    response.ok = ! response.frequencyHz.empty();

    if (! response.ok)
        response.error = "no usable bins";

    return response;
}

HarmonicResult measureHarmonics (const juce::AudioBuffer<float>& output,
                                 int channel,
                                 double sampleRate,
                                 double fundamentalHz)
{
    HarmonicResult result;
    result.fundamentalHz = fundamentalHz;

    // Skip the first tenth of a second so a plugin's own attack does not count as distortion.
    const auto samples = takeAnalysisWindow (output, channel, (int) (sampleRate * 0.1));

    if (samples.size() < 1024)
    {
        result.error = "not enough steady-state samples to analyse";
        return result;
    }

    const auto fftSize = samples.size();
    const auto analysed = amplitudeSpectrum (samples, Window::blackmanHarris);
    const auto& amplitudes = analysed.amplitudes;

    // Blackman-Harris spreads a tone over roughly four bins either side.
    constexpr int halfWidth = 5;

    const auto fundamentalBin = binForFrequency (fundamentalHz, sampleRate, fftSize);
    const auto fundamental = amplitudeAround (analysed, fundamentalBin, halfWidth);

    if (fundamental <= 0.0)
    {
        result.error = "no energy at the fundamental - is the plugin passing audio?";
        return result;
    }

    result.fundamentalDb = toDb (fundamental);

    double harmonicPowerSum = 0.0;

    for (int order = 2; order <= 10; ++order)
    {
        const auto frequency = fundamentalHz * order;

        if (frequency >= sampleRate * 0.5)
            break;

        const auto amplitude = amplitudeAround (analysed, binForFrequency (frequency, sampleRate, fftSize), halfWidth);
        harmonicPowerSum += amplitude * amplitude;

        result.harmonics.push_back ({ order, frequency, toDb (amplitude), toDb (amplitude / fundamental) });
    }

    result.thdPercent = 100.0 * std::sqrt (harmonicPowerSum) / fundamental;

    double evenPower = 0.0, oddPower = 0.0;

    for (const auto& harmonic : result.harmonics)
    {
        const auto amplitude = std::pow (10.0, harmonic.levelDb / 20.0);
        (harmonic.order % 2 == 0 ? evenPower : oddPower) += amplitude * amplitude;
    }

    result.evenPercent = 100.0 * std::sqrt (evenPower) / fundamental;
    result.oddPercent  = 100.0 * std::sqrt (oddPower) / fundamental;

    if (result.thdPercent < 0.003)
        result.character = "Transparent: no measurable harmonic content";
    else if (result.evenPercent > result.oddPercent * 1.5)
        result.character = "Even-order dominant: asymmetric saturation, warm";
    else if (result.oddPercent > result.evenPercent * 1.5)
        result.character = "Odd-order dominant: symmetric clipping, harder edged";
    else
        result.character = "Mixed even and odd harmonics";

    // THD+N: everything that is not the fundamental, against the fundamental. The same lobe
    // correction that individual components get has to be applied to the total, otherwise the
    // fundamental's own energy fails to cancel and a clean tone reads as 100% distortion.
    // (Broadband noise strictly wants the window's noise bandwidth instead; for signals
    // dominated by discrete components, which is what these tests produce, this is right.)
    double totalPower = 0.0;

    for (std::size_t bin = 1; bin < amplitudes.size(); ++bin)
        totalPower += amplitudes[bin] * amplitudes[bin];

    totalPower *= analysed.lobeCorrection * analysed.lobeCorrection;

    const auto fundamentalPower = fundamental * fundamental;
    result.thdPlusNPercent = 100.0 * std::sqrt (juce::jmax (0.0, totalPower - fundamentalPower)) / fundamental;

    // Noise floor: the median bin, which ignores the handful of bins holding real components.
    auto sorted = amplitudes;
    std::sort (sorted.begin(), sorted.end());
    result.noiseFloorDb = toDb (sorted[sorted.size() / 2]);

    result.spectrum = buildSpectrum (amplitudes, sampleRate, fftSize);
    result.ok = true;
    return result;
}

ImdResult measureIntermodulation (const juce::AudioBuffer<float>& output,
                                  int channel,
                                  double sampleRate,
                                  double lowToneHz,
                                  double highToneHz)
{
    ImdResult result;

    const auto samples = takeAnalysisWindow (output, channel, (int) (sampleRate * 0.1));

    if (samples.size() < 1024)
    {
        result.error = "not enough steady-state samples to analyse";
        return result;
    }

    const auto fftSize = samples.size();
    const auto analysed = amplitudeSpectrum (samples, Window::blackmanHarris);
    const auto& amplitudes = analysed.amplitudes;
    constexpr int halfWidth = 5;

    const auto carrier = amplitudeAround (analysed, binForFrequency (highToneHz, sampleRate, fftSize), halfWidth);

    if (carrier <= 0.0)
    {
        result.error = "no energy at the upper tone - is the plugin passing audio?";
        return result;
    }

    result.carrierDb = toDb (carrier);

    // SMPTE intermodulation: sidebands appear around the high tone, spaced by the low tone.
    double sidebandPower = 0.0;

    for (int order = 1; order <= 5; ++order)
    {
        for (const auto frequency : { highToneHz - order * lowToneHz, highToneHz + order * lowToneHz })
        {
            if (frequency <= 0.0 || frequency >= sampleRate * 0.5)
                continue;

            const auto amplitude = amplitudeAround (analysed, binForFrequency (frequency, sampleRate, fftSize), halfWidth);
            sidebandPower += amplitude * amplitude;

            result.sidebands.push_back ({ order, frequency, toDb (amplitude), toDb (amplitude / carrier) });
        }
    }

    result.imdPercent = 100.0 * std::sqrt (sidebandPower) / carrier;
    result.spectrum = buildSpectrum (amplitudes, sampleRate, fftSize);
    result.ok = true;
    return result;
}

TransferCurve measureTransferCurve (const juce::AudioBuffer<float>& input,
                                    const juce::AudioBuffer<float>& output,
                                    int channel,
                                    double sampleRate)
{
    juce::ignoreUnused (sampleRate);

    TransferCurve curve;

    const auto inputChannel = juce::jmin (channel, input.getNumChannels() - 1);

    if (inputChannel < 0 || ! juce::isPositiveAndBelow (channel, output.getNumChannels()))
    {
        curve.error = "channel not present";
        return curve;
    }

    constexpr int blockSize = 1024;
    const auto numSamples = juce::jmin (input.getNumSamples(), output.getNumSamples());

    if (numSamples < blockSize * 4)
    {
        curve.error = "not enough samples to trace a curve";
        return curve;
    }

    std::vector<double> inputDb, outputDb;

    for (int position = 0; position + blockSize <= numSamples; position += blockSize / 2)
    {
        const auto in  = (double) input.getRMSLevel (inputChannel, position, blockSize);
        const auto out = (double) output.getRMSLevel (channel, position, blockSize);

        if (in <= 0.0)
            continue;

        inputDb.push_back (toDb (in));
        outputDb.push_back (toDb (out));
    }

    if (inputDb.size() < 4)
    {
        curve.error = "the stimulus never rose above silence";
        return curve;
    }

    // The ramp climbs and then falls; split at the loudest point so the two directions can be
    // compared. For a compressor they separate, and that separation is its time behaviour.
    const auto peak = (std::size_t) std::distance (inputDb.begin(), std::max_element (inputDb.begin(), inputDb.end()));

    for (std::size_t i = 0; i <= peak; ++i)
    {
        curve.risingInputDb.push_back (inputDb[i]);
        curve.risingOutputDb.push_back (outputDb[i]);
    }

    for (std::size_t i = peak; i < inputDb.size(); ++i)
    {
        curve.fallingInputDb.push_back (inputDb[i]);
        curve.fallingOutputDb.push_back (outputDb[i]);
    }

    curve.ok = true;
    return curve;
}

FrequencyResponse smoothResponse (const FrequencyResponse& response, double octaveFraction)
{
    if (! response.ok || octaveFraction <= 0.0)
        return response;

    auto smoothed = response;
    const auto n = response.frequencyHz.size();

    // Power running sums make each point O(1): the window slides along the sorted frequencies.
    std::vector<double> power (n), prefix (n + 1, 0.0);

    for (std::size_t i = 0; i < n; ++i)
    {
        power[i] = std::pow (10.0, response.magnitudeDb[i] / 10.0);
        prefix[i + 1] = prefix[i] + power[i];
    }

    const auto halfSpan = std::pow (2.0, octaveFraction * 0.5);
    std::size_t low = 0, high = 0;

    for (std::size_t i = 0; i < n; ++i)
    {
        const auto centre = response.frequencyHz[i];

        while (low < n && response.frequencyHz[low] < centre / halfSpan)
            ++low;

        while (high < n && response.frequencyHz[high] <= centre * halfSpan)
            ++high;

        const auto count = (double) (high - low);
        smoothed.magnitudeDb[i] = count > 0.0 ? 10.0 * std::log10 (juce::jmax (1.0e-30, (prefix[high] - prefix[low]) / count))
                                              : response.magnitudeDb[i];
    }

    return smoothed;
}

namespace
{
/** Linear interpolation of a curve at x, for a curve sorted by x. */
double interpolate (const std::vector<double>& xs, const std::vector<double>& ys, double target)
{
    if (xs.empty())
        return 0.0;

    const auto it = std::lower_bound (xs.begin(), xs.end(), target);

    if (it == xs.begin()) return ys.front();
    if (it == xs.end())   return ys.back();

    const auto i = (std::size_t) std::distance (xs.begin(), it);
    const auto span = xs[i] - xs[i - 1];
    const auto t = span > 0.0 ? (target - xs[i - 1]) / span : 0.0;
    return ys[i - 1] + t * (ys[i] - ys[i - 1]);
}
} // namespace

ResponseSummary summariseResponse (const FrequencyResponse& raw)
{
    ResponseSummary summary;

    if (! raw.ok || raw.frequencyHz.size() < 8)
        return summary;

    // Judge the curve at sixth-octave resolution: an impulse response has thousands of bins and
    // every one of them wobbles, which would make corners and peaks meaningless.
    const auto response = smoothResponse (raw, 1.0 / 6.0);
    const auto& f = response.frequencyHz;
    const auto& m = response.magnitudeDb;

    summary.gainAt1kDb = interpolate (f, m, 1000.0);
    const auto reference = summary.gainAt1kDb;

    // Group delay: the negative slope of unwrapped phase against angular frequency, taken over a
    // quarter octave either side of 1 kHz.
    {
        const auto p1 = interpolate (raw.frequencyHz, raw.phaseDegrees, 890.0);
        const auto p2 = interpolate (raw.frequencyHz, raw.phaseDegrees, 1120.0);
        const auto radians = (p2 - p1) * juce::MathConstants<double>::pi / 180.0;
        const auto omega = juce::MathConstants<double>::twoPi * (1120.0 - 890.0);
        summary.groupDelayMs = -1000.0 * radians / omega;
    }

    double peak = -1.0e9, trough = 1.0e9;

    for (std::size_t i = 0; i < f.size(); ++i)
    {
        if (f[i] < 40.0 || f[i] > 16000.0)
            continue;

        const auto relative = m[i] - reference;

        if (relative > peak)   { peak = relative;   summary.peakHz = f[i]; }
        if (relative < trough) { trough = relative; }
    }

    summary.peakDb = peak;
    summary.troughDb = trough;

    // Corners: walk away from 1 kHz until the curve is 3 dB down.
    std::size_t centre = 0;

    while (centre + 1 < f.size() && f[centre] < 1000.0)
        ++centre;

    for (std::size_t i = centre; i-- > 0;)
        if (m[i] <= reference - 3.0 && f[i] >= 20.0)
        {
            summary.lowCornerHz = f[i];
            break;
        }

    for (std::size_t i = centre; i < f.size(); ++i)
        if (m[i] <= reference - 3.0 && f[i] <= 22000.0)
        {
            summary.highCornerHz = f[i];
            break;
        }

    juce::StringArray notes;

    if (peak < 1.0 && trough > -1.0)
        notes.add ("Flat within " + oneDecimal (juce::jmax (peak, -trough)) + " dB from 40 Hz to 16 kHz");
    else
    {
        if (summary.lowCornerHz > 0.0)  notes.add ("high-passes below " + juce::String (juce::roundToInt (summary.lowCornerHz)) + " Hz");
        if (summary.highCornerHz > 0.0) notes.add ("low-passes above " + juce::String (juce::roundToInt (summary.highCornerHz)) + " Hz");
        if (peak >= 1.5)  notes.add ("boosts " + oneDecimal (peak) + " dB near " + juce::String (juce::roundToInt (summary.peakHz)) + " Hz");
        if (trough <= -1.5 && summary.lowCornerHz == 0.0 && summary.highCornerHz == 0.0) notes.add ("cuts up to " + oneDecimal (-trough) + " dB");
    }

    if (notes.isEmpty())
        notes.add ("Gently shaped, within " + oneDecimal (juce::jmax (peak, -trough)) + " dB");

    summary.description = notes.joinIntoString ("; ");
    summary.description = summary.description.substring (0, 1).toUpperCase() + summary.description.substring (1);
    summary.ok = true;
    return summary;
}

double refineFrequency (const juce::AudioBuffer<float>& output, int channel, double sampleRate, double approximateHz, double tolerance)
{
    const auto samples = takeAnalysisWindow (output, channel, (int) (sampleRate * 0.1));

    if (samples.size() < 4096)
        return approximateHz;

    const auto analysed = amplitudeSpectrum (samples, Window::blackmanHarris);
    const auto low = juce::jmax (1, binForFrequency (approximateHz * (1.0 - tolerance), sampleRate, samples.size()));
    const auto high = juce::jmin ((int) analysed.amplitudes.size() - 2, binForFrequency (approximateHz * (1.0 + tolerance), sampleRate, samples.size()));

    int best = low;

    for (int bin = low; bin <= high; ++bin)
        if (analysed.amplitudes[(std::size_t) bin] > analysed.amplitudes[(std::size_t) best])
            best = bin;

    return (double) best * sampleRate / (double) samples.size();
}

AliasResult measureAliasing (const juce::AudioBuffer<float>& output, int channel, double sampleRate, double toneHz)
{
    AliasResult result;
    result.toneHz = toneHz;

    const auto samples = takeAnalysisWindow (output, channel, (int) (sampleRate * 0.1));

    if (samples.size() < 4096)
    {
        result.error = "not enough steady-state samples to analyse";
        return result;
    }

    const auto fftSize = samples.size();
    const auto analysed = amplitudeSpectrum (samples, Window::blackmanHarris);
    const auto& amplitudes = analysed.amplitudes;
    constexpr int halfWidth = 6;

    const auto toneBin = binForFrequency (toneHz, sampleRate, fftSize);
    const auto tone = amplitudeAround (analysed, toneBin, halfWidth);

    if (tone <= 0.0)
    {
        result.error = "no energy at the test tone";
        return result;
    }

    // Everything that is not the tone or one of its harmonics, up to the sampling limit. A harmonic
    // above the limit folds back, so the folded position is masked too: those are the legitimate
    // distortion products. What is left is what should not be there.
    std::vector<bool> masked (amplitudes.size(), false);

    for (int k = 1; k * toneHz < sampleRate * 3.0; ++k)
    {
        auto frequency = std::fmod ((double) k * toneHz, sampleRate);

        if (frequency > sampleRate * 0.5)
            frequency = sampleRate - frequency;

        const auto centre = binForFrequency (frequency, sampleRate, fftSize);

        for (int bin = centre - halfWidth; bin <= centre + halfWidth; ++bin)
            if (juce::isPositiveAndBelow (bin, (int) masked.size()))
                masked[(std::size_t) bin] = true;
    }

    // Do not count the very bottom: DC offset and slow drift are not aliasing.
    const auto firstBin = juce::jmax (1, binForFrequency (60.0, sampleRate, fftSize));
    double worst = 0.0;
    int worstBin = 0;

    for (std::size_t bin = (std::size_t) firstBin; bin + 1 < amplitudes.size(); ++bin)
        if (! masked[bin] && amplitudes[bin] > worst)
        {
            worst = amplitudes[bin];
            worstBin = (int) bin;
        }

    result.worstDbc = toDb (worst / tone);
    result.worstHz = (double) worstBin * sampleRate / (double) fftSize;

    const auto level = result.worstDbc;
    result.description = level < -100.0 ? "Clean: nothing measurable that is not a harmonic"
                       : level < -80.0  ? "Very low: below -80 dB, inaudible in practice"
                       : level < -60.0  ? "Low: faint inharmonic content, rarely audible"
                       : level < -40.0  ? "Audible on bright material: inharmonic content above -60 dB"
                                        : "Strong: this plugin aliases badly";

    result.ok = true;
    return result;
}

LevelStats measureLevels (const juce::AudioBuffer<float>& buffer, int channel, int skipSamples)
{
    LevelStats stats;

    if (! juce::isPositiveAndBelow (channel, buffer.getNumChannels()) || buffer.getNumSamples() <= skipSamples)
        return stats;

    const auto* samples = buffer.getReadPointer (channel) + skipSamples;
    const auto count = buffer.getNumSamples() - skipSamples;

    double sum = 0.0, squares = 0.0, peak = 0.0;

    for (int i = 0; i < count; ++i)
    {
        const auto v = (double) samples[i];
        sum += v;
        squares += v * v;
        peak = juce::jmax (peak, std::abs (v));
    }

    stats.dcOffset = sum / (double) count;
    stats.peakDb = toDb (peak);
    stats.rmsDb = toDb (std::sqrt (squares / (double) count));
    stats.crestDb = stats.peakDb - stats.rmsDb;
    stats.ok = true;
    return stats;
}

TailResult measureTail (const juce::AudioBuffer<float>& output, int channel, double sampleRate)
{
    TailResult result;

    if (! juce::isPositiveAndBelow (channel, output.getNumChannels()) || output.getNumSamples() < 16 || sampleRate <= 0.0)
        return result;

    const auto* samples = output.getReadPointer (channel);
    const auto count = output.getNumSamples();

    float peak = 0.0f;

    for (int i = 0; i < count; ++i)
        peak = juce::jmax (peak, std::abs (samples[i]));

    if (peak <= 0.0f)
        return result;

    // -60 dB of the peak, measured on short windows so one stray sample does not extend the tail.
    const auto floor = peak * 0.001f;
    constexpr int window = 64;
    int last = 0;

    for (int start = 0; start + window <= count; start += window)
    {
        float windowPeak = 0.0f;

        for (int i = start; i < start + window; ++i)
            windowPeak = juce::jmax (windowPeak, std::abs (samples[i]));

        if (windowPeak > floor)
            last = start + window;
    }

    result.tailMs = 1000.0 * (double) last / sampleRate;
    result.truncated = last >= count - window * 2;
    result.ok = true;
    return result;
}

TransferAnalysis analyseTransfer (const TransferCurve& curve)
{
    TransferAnalysis analysis;

    const auto& in = curve.risingInputDb;
    const auto& out = curve.risingOutputDb;

    if (! curve.ok || in.size() < 8 || out.size() != in.size())
        return analysis;

    // Gain at each point of the rising branch.
    std::vector<double> gain (in.size());

    for (std::size_t i = 0; i < in.size(); ++i)
        gain[i] = out[i] - in[i];

    // Quiet material, before anything engages: median gain of the quietest fifth.
    const auto quietCount = juce::jmax<std::size_t> (3, in.size() / 5);
    std::vector<double> quiet (gain.begin(), gain.begin() + (std::ptrdiff_t) quietCount);
    std::sort (quiet.begin(), quiet.end());
    analysis.smallSignalGainDb = quiet[quiet.size() / 2];

    // Where the gain first falls a decibel below that, and stays there.
    std::size_t onset = in.size();

    for (std::size_t i = quietCount; i < in.size(); ++i)
        if (gain[i] < analysis.smallSignalGainDb - 1.0)
        {
            onset = i;
            break;
        }

    analysis.gainReductionAtTopDb = analysis.smallSignalGainDb - gain.back();

    // A rise in gain with level is expansion or a boost into saturation; call it out separately.
    const auto expands = gain.back() > analysis.smallSignalGainDb + 1.0;

    if (onset < in.size())
    {
        analysis.engages = true;
        analysis.thresholdDb = in[onset];

        // Ratio from the slope of the last part of the curve, once well past the onset.
        const auto begin = juce::jmin (in.size() - 3, onset + (in.size() - onset) / 2);
        const auto dx = in.back() - in[begin];
        const auto dy = out.back() - out[begin];
        const auto slope = dx > 1.0 ? dy / dx : 1.0;
        analysis.ratio = slope > 0.03 ? 1.0 / slope : 100.0;
    }

    // Timing: how far apart the rising and falling branches sit at the same input level.
    if (curve.fallingInputDb.size() >= 4)
    {
        std::vector<std::pair<double, double>> falling;

        for (std::size_t i = 0; i < curve.fallingInputDb.size(); ++i)
            falling.emplace_back (curve.fallingInputDb[i], curve.fallingOutputDb[i]);

        std::sort (falling.begin(), falling.end());
        std::vector<double> fx, fy;

        for (const auto& [a, b] : falling) { fx.push_back (a); fy.push_back (b); }

        double sum = 0.0;
        int counted = 0;

        for (std::size_t i = 0; i < in.size(); ++i)
            if (in[i] > fx.front() && in[i] < fx.back())
            {
                sum += std::abs (interpolate (fx, fy, in[i]) - out[i]);
                ++counted;
            }

        analysis.hysteresisDb = counted > 0 ? sum / (double) counted : 0.0;
    }

    juce::String text;

    if (analysis.engages)
    {
        text << (analysis.ratio >= 20.0 ? "Limiting" : analysis.ratio >= 1.5 ? "Compressing" : "Soft saturation")
             << " from about " << juce::String (juce::roundToInt (analysis.thresholdDb)) << " dB in";

        if (analysis.ratio >= 20.0)
            text << ", effectively a brick wall";
        else
            text << ", roughly " << juce::String (analysis.ratio, 1) << ":1";
    }
    else if (expands)
        text << "Gain rises with level: expansion, or a boost driving into saturation";
    else
        text << "Linear: gain does not change with level";

    if (analysis.hysteresisDb > 0.4)
        text << ". The rising and falling branches differ by " << juce::String (analysis.hysteresisDb, 1) << " dB, so its attack and release are audible in the curve";

    analysis.description = text;
    analysis.ok = true;
    return analysis;
}

juce::String HarmonicResult::summary() const
{
    if (! ok)
        return "harmonic analysis: " + error;

    juce::String text;
    text << "fundamental " << juce::String (fundamentalHz, 1) << " Hz at "
         << juce::String (fundamentalDb, 2) << " dBFS"
         << "\nTHD " << juce::String (thdPercent, 5) << " %  (" << juce::String (toDb (thdPercent / 100.0), 1) << " dB)"
         << "\nTHD+N " << juce::String (thdPlusNPercent, 5) << " %"
         << "\nnoise floor " << juce::String (noiseFloorDb, 1) << " dBFS";

    for (const auto& harmonic : harmonics)
        if (harmonic.relativeDb > -140.0)
            text << "\n  H" << harmonic.order << " at " << juce::String (harmonic.frequencyHz, 0) << " Hz: "
                 << juce::String (harmonic.relativeDb, 1) << " dBc";

    return text;
}

juce::String ImdResult::summary() const
{
    if (! ok)
        return "intermodulation: " + error;

    juce::String text;
    text << "SMPTE IMD " << juce::String (imdPercent, 5) << " %"
         << "\nupper tone at " << juce::String (carrierDb, 2) << " dBFS";

    for (const auto& sideband : sidebands)
        if (sideband.relativeDb > -140.0)
            text << "\n  " << juce::String (sideband.frequencyHz, 0) << " Hz: "
                 << juce::String (sideband.relativeDb, 1) << " dBc";

    return text;
}

juce::String TransferCurve::summary() const
{
    if (! ok)
        return "transfer curve: " + error;

    juce::String text;
    text << "traced " << (int) (risingInputDb.size() + fallingInputDb.size()) << " points";

    if (! risingInputDb.empty())
    {
        const auto lastIndex = risingInputDb.size() - 1;
        text << "\nat the loudest point: in " << juce::String (risingInputDb[lastIndex], 1)
             << " dB, out " << juce::String (risingOutputDb[lastIndex], 1) << " dB"
             << "  (gain " << juce::String (risingOutputDb[lastIndex] - risingInputDb[lastIndex], 2) << " dB)";
    }

    return text;
}
} // namespace vibecheck
