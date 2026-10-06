#include "MeasureSuite.h"

#include <cmath>

#include "audio/OfflineRenderer.h"
#include "audio/TestSignal.h"

namespace vibecheck
{
namespace
{
struct Stage
{
    juce::AudioBuffer<float> input;
    RenderResult render;
};

Stage runStage (juce::AudioPluginInstance& plugin, SignalSpec spec, RenderOptions options)
{
    Stage stage;
    stage.input = generate (spec);
    stage.render = OfflineRenderer::render (plugin, stage.input, options);
    return stage;
}
} // namespace

juce::String SuiteResult::describeLatency() const
{
    const auto ms = sampleRate > 0.0 ? 1000.0 * latencySamples / sampleRate : 0.0;
    return juce::String (latencySamples) + " samples (" + juce::String (ms, 2) + " ms)";
}

SuiteResult runSuite (juce::AudioPluginInstance& plugin, const SuiteOptions& suite,
                      const std::function<void (const juce::String&)>& progress)
{
    SuiteResult result;
    result.sampleRate = suite.sampleRate;

    const auto say = [&progress] (const juce::String& text) { if (progress != nullptr) progress (text); };
    const auto rate = suite.sampleRate;
    const auto started = juce::Time::getMillisecondCounterHiRes();

    RenderOptions options;
    options.sampleRate = rate;
    options.blockSize = suite.blockSize;
    options.tailSamples = (int) rate / 2;

    SignalSpec base;
    base.sampleRate = rate;
    base.numChannels = 2;
    base.frequencyHz = suite.toneHz;

    juce::StringArray errors;

    // An instrument ignores its audio input, so none of the stimuli below would reach it. Play a
    // note and measure what comes out instead.
    const auto description = plugin.getPluginDescription();
    result.instrument = description.isInstrument || (plugin.acceptsMidi() && plugin.getTotalNumInputChannels() == 0);

    if (result.instrument)
    {
        say ("Measuring 1 of 2: a held note");

        const auto noteLength = (int) (rate * 2.0);
        juce::MidiBuffer notes;
        notes.addEvent (juce::MidiMessage::noteOn (1, suite.midiNote, (juce::uint8) 100), 0);
        notes.addEvent (juce::MidiMessage::noteOff (1, suite.midiNote), noteLength);

        auto spec = base;
        spec.type = SignalType::silence;
        spec.numSamples = noteLength;

        auto noteOptions = options;
        noteOptions.midi = notes;
        noteOptions.tailSamples = (int) (rate * 2.5);
        auto stage = runStage (plugin, spec, noteOptions);

        if (stage.render.ok)
        {
            result.noteHz = 440.0 * std::pow (2.0, (suite.midiNote - 69) / 12.0);
            result.latencySamples = stage.render.reportedLatencySamples;
            result.speedTimesRealtime = stage.render.timesFasterThanRealtime;

            // The held part only, so the release does not smear the harmonic measurement.
            juce::AudioBuffer<float> held (stage.render.output.getNumChannels(), noteLength);

            for (int c = 0; c < held.getNumChannels(); ++c)
                held.copyFrom (c, 0, stage.render.output, c, 0, noteLength);

            result.noteHz = refineFrequency (held, 0, rate, result.noteHz);
            result.harmonics = measureHarmonics (held, 0, rate, result.noteHz);

            // How long until sound starts, and how long it rings after the key is released.
            const auto* samples = stage.render.output.getReadPointer (0);
            const auto total = stage.render.output.getNumSamples();
            float peak = 0.0f;

            for (int i = 0; i < total; ++i)
                peak = juce::jmax (peak, std::abs (samples[i]));

            for (int i = 0; i < total && peak > 0.0f; ++i)
                if (std::abs (samples[i]) > peak * 0.01f)
                {
                    result.noteOnsetMs = 1000.0 * i / rate;
                    break;
                }

            int last = 0;

            for (int i = noteLength; i < total; i += 64)
            {
                float windowPeak = 0.0f;

                for (int k = i; k < juce::jmin (total, i + 64); ++k)
                    windowPeak = juce::jmax (windowPeak, std::abs (samples[k]));

                if (windowPeak > peak * 0.001f)
                    last = i + 64;
            }

            result.noteReleaseMs = last > noteLength ? 1000.0 * (last - noteLength) / rate : 0.0;
            result.noteOut = std::move (stage.render.output);
            result.anyOk = peak > 0.0f;

            if (peak <= 0.0f)
                errors.add ("the instrument made no sound for a note on channel 1");
        }
        else
        {
            errors.add (stage.render.error);
        }

        say ("Measuring 2 of 2: self-noise");
        spec.numSamples = (int) rate;
        auto silent = runStage (plugin, spec, options);

        if (silent.render.ok)
        {
            result.noise = measureLevels (silent.render.output, 0);
            result.silenceOut = std::move (silent.render.output);
        }

        errors.removeEmptyStrings();
        result.error = errors.isEmpty() ? juce::String() : errors[0];
        result.seconds = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;
        return result;
    }

    // 1. Impulse: magnitude, phase, group delay, tail. A longer tail so reverbs are seen.
    say ("Measuring 1 of 5: impulse response");
    {
        auto spec = base;
        spec.type = SignalType::impulse;
        spec.amplitudeDb = 0.0;
        spec.numSamples = 16384;

        auto longTail = options;
        longTail.tailSamples = (int) rate;
        auto stage = runStage (plugin, spec, longTail);

        if (stage.render.ok)
        {
            result.response = measureFrequencyResponse (stage.input, stage.render.output, 0, rate);
            result.responseSummary = summariseResponse (result.response);
            result.tail = measureTail (stage.render.output, 0, rate);
            result.latencySamples = stage.render.reportedLatencySamples;
            result.impulseOut = std::move (stage.render.output);
            result.anyOk = true;
        }
        else
        {
            errors.add (stage.render.error);
        }
    }

    // 2. Sine: harmonic distortion. Its render time also stands for the plugin's speed.
    say ("Measuring 2 of 5: harmonic distortion");
    {
        auto spec = base;
        spec.type = SignalType::sine;
        spec.numSamples = (int) rate * 2;
        auto stage = runStage (plugin, spec, options);

        if (stage.render.ok)
        {
            result.harmonics = measureHarmonics (stage.render.output, 0, rate, suite.toneHz);
            result.speedTimesRealtime = stage.render.timesFasterThanRealtime;
            result.sineOut = std::move (stage.render.output);
            result.anyOk = true;
        }
        else
        {
            errors.add (stage.render.error);
        }
    }

    // 3. Two tones: intermodulation.
    say ("Measuring 3 of 5: intermodulation");
    {
        auto spec = base;
        spec.type = SignalType::dualSine;
        spec.numSamples = (int) rate * 2;
        spec.frequencyHz = 60.0;
        spec.secondFrequencyHz = 7000.0;
        auto stage = runStage (plugin, spec, options);

        if (stage.render.ok)
        {
            result.imd = measureIntermodulation (stage.render.output, 0, rate, 60.0, 7000.0);
            result.dualOut = std::move (stage.render.output);
            result.anyOk = true;
        }
        else
        {
            errors.add (stage.render.error);
        }
    }

    // 4. Ramp: the transfer curve. It runs to full scale, or a compressor never engages.
    say ("Measuring 4 of 5: dynamics");
    {
        auto spec = base;
        spec.type = SignalType::ramp;
        spec.amplitudeDb = 0.0;
        spec.numSamples = (int) rate * 4;
        auto stage = runStage (plugin, spec, options);

        if (stage.render.ok)
        {
            result.transfer = measureTransferCurve (stage.input, stage.render.output, 0, rate);
            result.transferAnalysis = analyseTransfer (result.transfer);
            result.rampOut = std::move (stage.render.output);
            result.anyOk = true;
        }
        else
        {
            errors.add (stage.render.error);
        }
    }

    // 5. Silence: whatever the plugin makes on its own.
    say ("Measuring 5 of 5: self-noise");
    {
        auto spec = base;
        spec.type = SignalType::silence;
        spec.numSamples = (int) rate;
        auto stage = runStage (plugin, spec, options);

        if (stage.render.ok)
        {
            result.noise = measureLevels (stage.render.output, 0);
            result.silenceOut = std::move (stage.render.output);
        }
    }

    // 6. A high tone through any distortion: what folds back is aliasing.
    say ("Measuring 6: aliasing");
    {
        auto spec = base;
        spec.type = SignalType::sine;
        spec.amplitudeDb = -6.0;
        spec.frequencyHz = 0.1875 * rate;   // 9 kHz at 48 kHz; its harmonics fold to places that are not harmonics
        spec.numSamples = (int) (rate * 1.5);
        auto stage = runStage (plugin, spec, options);

        if (stage.render.ok)
            result.alias = measureAliasing (stage.render.output, 0, rate, spec.frequencyHz);
    }

    // 7. Optional: distortion against level and against frequency.
    if (suite.sweeps)
    {
        result.sweepsRan = true;

        const double levels[] = { -60.0, -48.0, -36.0, -24.0, -18.0, -12.0, -6.0, -3.0, 0.0 };
        const double frequencies[] = { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 12000.0 };
        const auto toDbc = [] (double percent) { return 20.0 * std::log10 (juce::jmax (1.0e-6, percent / 100.0)); };
        int step = 0;

        for (const auto level : levels)
        {
            say ("Sweeping distortion " + juce::String (++step) + " of 18");
            auto spec = base;
            spec.type = SignalType::sine;
            spec.amplitudeDb = level;
            spec.frequencyHz = suite.toneHz;
            spec.numSamples = (int) rate;
            auto stage = runStage (plugin, spec, options);

            if (stage.render.ok)
            {
                const auto h = measureHarmonics (stage.render.output, 0, rate, suite.toneHz);

                if (h.ok)
                {
                    result.sweepLevelDb.push_back (level);
                    result.thdVsLevelDb.push_back (toDbc (h.thdPercent));
                }
            }
        }

        for (const auto frequency : frequencies)
        {
            say ("Sweeping distortion " + juce::String (++step) + " of 18");

            if (frequency * 3.0 >= rate * 0.5)
                continue;

            auto spec = base;
            spec.type = SignalType::sine;
            spec.amplitudeDb = -6.0;
            spec.frequencyHz = frequency;
            spec.numSamples = (int) rate;
            auto stage = runStage (plugin, spec, options);

            if (stage.render.ok)
            {
                const auto h = measureHarmonics (stage.render.output, 0, rate, frequency);

                if (h.ok)
                {
                    result.sweepFrequencyHz.push_back (frequency);
                    result.thdVsFrequencyDb.push_back (toDbc (h.thdPercent));
                }
            }
        }
    }

    errors.removeEmptyStrings();
    result.error = errors.isEmpty() ? juce::String() : errors[0];
    result.seconds = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;
    return result;
}
} // namespace vibecheck
