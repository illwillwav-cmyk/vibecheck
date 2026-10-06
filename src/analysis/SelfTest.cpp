#include "SelfTest.h"

#include "Measurements.h"
#include "audio/TestSignal.h"
#include "ui/PluginChooser.h"
#include "vibecheck/Export.h"
#include "vibecheck/Heuristics.h"
#include "vibecheck/PEReader.h"
#include "vibecheck/UpdateCheck.h"

#include <cmath>
#include <iostream>
#include <set>

namespace vibecheck
{
namespace
{
int failures = 0;

void check (const juce::String& name, double actual, double expected, double tolerance)
{
    const auto passed = std::abs (actual - expected) <= tolerance;

    if (! passed)
        ++failures;

    std::cout << (passed ? "  pass  " : "  FAIL  ")
              << name
              << ": got " << juce::String (actual, 6)
              << ", expected " << juce::String (expected, 6)
              << " +/- " << juce::String (tolerance, 6)
              << std::endl;
}

constexpr double sampleRate = 48000.0;

/** A tone plus a deliberate set of harmonics, at amplitudes we choose. */
juce::AudioBuffer<float> toneWithHarmonics (double fundamentalHz,
                                            double fundamentalAmplitude,
                                            const std::vector<std::pair<int, double>>& harmonics,
                                            int numSamples)
{
    juce::AudioBuffer<float> buffer (1, numSamples);
    buffer.clear();

    auto* samples = buffer.getWritePointer (0);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto phase = juce::MathConstants<double>::twoPi * fundamentalHz * (double) i / sampleRate;
        double value = fundamentalAmplitude * std::sin (phase);

        for (const auto& [order, amplitude] : harmonics)
            value += amplitude * std::sin (phase * order);

        samples[i] = (float) value;
    }

    return buffer;
}

double valueAt (const std::vector<double>& x, const std::vector<double>& y, double target)
{
    double best = 0.0;
    double bestDistance = std::numeric_limits<double>::max();

    for (std::size_t i = 0; i < x.size() && i < y.size(); ++i)
    {
        const auto distance = std::abs (x[i] - target);

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = y[i];
        }
    }

    return best;
}
} // namespace

int runSelfTest()
{
    failures = 0;
    const int twoSeconds = (int) sampleRate * 2;

    std::cout << "\nharmonic measurement" << std::endl;
    {
        // A pure tone has no harmonics, so THD must land on zero.
        const auto clean = toneWithHarmonics (1000.0, 0.5, {}, twoSeconds);
        const auto result = measureHarmonics (clean, 0, sampleRate, 1000.0);
        check ("clean tone level (dBFS)", result.fundamentalDb, -6.0206, 0.01);
        check ("clean tone THD (%)", result.thdPercent, 0.0, 0.0005);
        check ("clean tone THD+N (%)", result.thdPlusNPercent, 0.0, 0.05);
    }
    {
        // Second harmonic at exactly 1% of the fundamental.
        const auto dirty = toneWithHarmonics (1000.0, 0.5, { { 2, 0.005 } }, twoSeconds);
        const auto result = measureHarmonics (dirty, 0, sampleRate, 1000.0);
        check ("1% second harmonic: THD (%)", result.thdPercent, 1.0, 0.01);
        check ("1% second harmonic: H2 (dBc)", result.harmonics.front().relativeDb, -40.0, 0.1);
        check ("1% second harmonic: THD+N (%)", result.thdPlusNPercent, 1.0, 0.05);
    }
    {
        // 1% second plus 0.5% third: THD is the root of the sum of the squares, 1.118%.
        const auto dirty = toneWithHarmonics (1000.0, 0.5, { { 2, 0.005 }, { 3, 0.0025 } }, twoSeconds);
        const auto result = measureHarmonics (dirty, 0, sampleRate, 1000.0);
        check ("1% + 0.5% harmonics: THD (%)", result.thdPercent, 1.1180, 0.01);
    }

    std::cout << "\nfrequency response" << std::endl;

    SignalSpec impulseSpec;
    impulseSpec.type = SignalType::impulse;
    impulseSpec.sampleRate = sampleRate;
    impulseSpec.numSamples = 8192;
    impulseSpec.numChannels = 1;
    impulseSpec.amplitudeDb = 0.0;
    const auto impulse = generate (impulseSpec);

    {
        // Straight through: flat at 0 dB, no phase shift.
        const auto response = measureFrequencyResponse (impulse, impulse, 0, sampleRate);
        check ("identity: magnitude at 1 kHz (dB)", valueAt (response.frequencyHz, response.magnitudeDb, 1000.0), 0.0, 0.001);
        check ("identity: magnitude at 10 kHz (dB)", valueAt (response.frequencyHz, response.magnitudeDb, 10000.0), 0.0, 0.001);
        check ("identity: phase at 1 kHz (deg)", valueAt (response.frequencyHz, response.phaseDegrees, 1000.0), 0.0, 0.001);
    }
    {
        // Six decibels of gain should read as 6.02 dB and nothing else.
        auto louder = impulse;
        louder.applyGain (2.0f);
        const auto response = measureFrequencyResponse (impulse, louder, 0, sampleRate);
        check ("2x gain: magnitude (dB)", valueAt (response.frequencyHz, response.magnitudeDb, 1000.0), 6.0206, 0.001);
    }
    {
        // One sample of delay is a phase slope: -360 degrees per sample rate, so -90 at 12 kHz.
        juce::AudioBuffer<float> delayed (1, impulse.getNumSamples());
        delayed.clear();
        delayed.copyFrom (0, 1, impulse, 0, 0, impulse.getNumSamples() - 1);

        const auto response = measureFrequencyResponse (impulse, delayed, 0, sampleRate);
        check ("1 sample delay: magnitude (dB)", valueAt (response.frequencyHz, response.magnitudeDb, 5000.0), 0.0, 0.001);
        check ("1 sample delay: phase at 12 kHz (deg)", valueAt (response.frequencyHz, response.phaseDegrees, 12000.0), -90.0, 0.5);
        check ("1 sample delay: phase at 6 kHz (deg)", valueAt (response.frequencyHz, response.phaseDegrees, 6000.0), -45.0, 0.5);
    }

    std::cout << "\nintermodulation" << std::endl;
    {
        SignalSpec spec;
        spec.type = SignalType::dualSine;
        spec.sampleRate = sampleRate;
        spec.numSamples = twoSeconds;
        spec.numChannels = 1;
        spec.frequencyHz = 60.0;
        spec.secondFrequencyHz = 7000.0;
        const auto twoTone = generate (spec);

        // Two tones added together, with nothing nonlinear in the path, make no sidebands.
        const auto result = measureIntermodulation (twoTone, 0, sampleRate, 60.0, 7000.0);
        check ("linear two-tone: IMD (%)", result.imdPercent, 0.0, 0.01);
    }

    std::cout << "\ntransfer curve" << std::endl;
    {
        SignalSpec spec;
        spec.type = SignalType::ramp;
        spec.sampleRate = sampleRate;
        spec.numSamples = twoSeconds;
        spec.numChannels = 1;
        const auto ramp = generate (spec);

        // Unity gain: output equals input at every point on both branches.
        const auto curve = measureTransferCurve (ramp, ramp, 0, sampleRate);

        double largestError = 0.0;

        for (std::size_t i = 0; i < curve.risingInputDb.size(); ++i)
            largestError = juce::jmax (largestError, std::abs (curve.risingInputDb[i] - curve.risingOutputDb[i]));

        check ("unity gain: largest in/out difference (dB)", largestError, 0.0, 0.001);
        check ("unity gain: both branches traced", (double) (curve.risingInputDb.empty() || curve.fallingInputDb.empty() ? 0 : 1), 1.0, 0.0);
    }

    std::cout << "\naliasing" << std::endl;
    {
        // A clean tone has nothing that is not a harmonic.
        const auto clean = toneWithHarmonics (9000.0, 0.5, {}, twoSeconds);
        check ("clean tone: no alias content (dBc)", measureAliasing (clean, 0, sampleRate, 9000.0).worstDbc, -140.0, 40.0);

        // A real harmonic is not aliasing, even when it folds back below the sampling limit:
        // the 3rd harmonic of 9 kHz is 27 kHz, which lands at 21 kHz at 48 kHz.
        const auto harmonic = toneWithHarmonics (9000.0, 0.5, { { 2, 0.05 } }, twoSeconds);
        check ("a harmonic is not counted as aliasing (dBc)", measureAliasing (harmonic, 0, sampleRate, 9000.0).worstDbc, -140.0, 40.0);

        // A stray tone that is not harmonically related, 40 dB under the signal, is.
        auto stray = toneWithHarmonics (9000.0, 0.5, {}, twoSeconds);

        for (int i = 0; i < stray.getNumSamples(); ++i)
            stray.setSample (0, i, stray.getSample (0, i) + 0.005f * (float) std::sin (juce::MathConstants<double>::twoPi * 13337.0 * (double) i / sampleRate));

        const auto found = measureAliasing (stray, 0, sampleRate, 9000.0);
        check ("a stray tone 40 dB down is found (dBc)", found.worstDbc, -40.0, 1.0);
        check ("and located (Hz)", found.worstHz, 13337.0, 10.0);
    }

    std::cout << "\nresponse summary" << std::endl;
    {
        // A one-pole low-pass at 2 kHz: -3 dB at the corner, and a known group delay at 1 kHz.
        FrequencyResponse lowpass;
        const auto corner = 2000.0;

        for (int i = 0; i < 3000; ++i)
        {
            const auto f = 20.0 * std::pow (1200.0, (double) i / 2999.0);
            const auto ratio = f / corner;
            lowpass.frequencyHz.push_back (f);
            lowpass.magnitudeDb.push_back (-10.0 * std::log10 (1.0 + ratio * ratio));
            lowpass.phaseDegrees.push_back (-std::atan (ratio) * 180.0 / juce::MathConstants<double>::pi);
        }

        lowpass.ok = true;
        const auto summary = summariseResponse (lowpass);
        // The corner is 3 dB below the level at 1 kHz, which already sits 0.97 dB down on this curve,
        // so it lands where the response is -3.97 dB: at 2 kHz * sqrt(1.5), about 2449 Hz.
        check ("low-pass: high corner, 3 dB below the 1 kHz level (Hz)", summary.highCornerHz, corner * std::sqrt (1.5), 120.0);
        check ("low-pass: no low corner", summary.lowCornerHz, 0.0, 0.0);
        check ("low-pass: group delay at 1 kHz (ms)", summary.groupDelayMs, 0.0637, 0.004);

        // Smoothing a flat curve must leave it flat.
        FrequencyResponse flat = lowpass;
        std::fill (flat.magnitudeDb.begin(), flat.magnitudeDb.end(), -3.0);
        const auto smoothed = smoothResponse (flat, 1.0 / 3.0);
        check ("smoothing keeps a flat curve flat (dB)", smoothed.magnitudeDb[1500], -3.0, 0.001);
        check ("flat curve: gain at 1 kHz (dB)", summariseResponse (flat).gainAt1kDb, -3.0, 0.01);
    }

    std::cout << "\nlevels and tail" << std::endl;
    {
        juce::AudioBuffer<float> constant (1, 4800);

        for (int i = 0; i < 4800; ++i)
            constant.setSample (0, i, 0.1f);

        const auto stats = measureLevels (constant, 0);
        check ("DC offset", stats.dcOffset, 0.1, 0.0001);
        check ("peak of 0.1 (dBFS)", stats.peakDb, -20.0, 0.01);

        juce::AudioBuffer<float> ring (1, (int) sampleRate);

        for (int i = 0; i < ring.getNumSamples(); ++i)
            ring.setSample (0, i, (float) std::exp (-(double) i / sampleRate / 0.05));

        // e^(-t/0.05) falls 60 dB at 0.05 * ln(1000), which is 345 ms.
        check ("tail length of a 50 ms decay (ms)", measureTail (ring, 0, sampleRate).tailMs, 345.0, 12.0);
    }

    std::cout << "\nharmonic character and transfer analysis" << std::endl;
    {
        const auto evenOnly = toneWithHarmonics (1000.0, 0.5, { { 2, 0.005 } }, twoSeconds);
        const auto oddOnly  = toneWithHarmonics (1000.0, 0.5, { { 3, 0.005 } }, twoSeconds);
        check ("even harmonics read as even-dominant", measureHarmonics (evenOnly, 0, sampleRate, 1000.0).character.startsWith ("Even") ? 1.0 : 0.0, 1.0, 0.0);
        check ("odd harmonics read as odd-dominant", measureHarmonics (oddOnly, 0, sampleRate, 1000.0).character.startsWith ("Odd") ? 1.0 : 0.0, 1.0, 0.0);

        // A 4:1 compressor with its threshold at -20 dB.
        TransferCurve curve;

        for (int i = 0; i <= 100; ++i)
        {
            const auto in = -60.0 + 0.6 * i;
            const auto out = in < -20.0 ? in : -20.0 + (in + 20.0) / 4.0;
            curve.risingInputDb.push_back (in);
            curve.risingOutputDb.push_back (out);
            curve.fallingInputDb.push_back (in);
            curve.fallingOutputDb.push_back (out);
        }

        curve.ok = true;
        const auto analysis = analyseTransfer (curve);
        check ("compressor: engages", analysis.engages ? 1.0 : 0.0, 1.0, 0.0);
        check ("compressor: threshold (dB)", analysis.thresholdDb, -18.7, 1.0);
        check ("compressor: ratio", analysis.ratio, 4.0, 0.3);
        check ("compressor: no hysteresis (dB)", analysis.hysteresisDb, 0.0, 0.05);

        for (std::size_t i = 0; i < curve.risingOutputDb.size(); ++i)
            curve.risingOutputDb[i] = curve.risingInputDb[i] + 3.0;

        const auto linear = analyseTransfer (curve);
        check ("linear gain: does not engage", linear.engages ? 1.0 : 0.0, 0.0, 0.0);
        check ("linear gain: small-signal gain (dB)", linear.smallSignalGainDb, 3.0, 0.01);
    }

    std::cout << "\nvibe check scoring" << std::endl;
    {
        const auto plentyOfEvidence = [] (BinaryFacts& facts)
        {
            for (int i = 0; i < 600; ++i)
                facts.definedSymbols.add ("_someSymbol" + juce::String (i));

            for (int i = 0; i < 2500; ++i)
                facts.strings.add ("ordinary string number " + juce::String (i));

            facts.ok = true;
        };

        {
            // Encrypted binary: the engine must abstain rather than call it clean.
            BinaryFacts facts;
            facts.ok = true;
            facts.paceWrapped = true;
            plentyOfEvidence (facts);

            juce::PluginDescription description;
            description.name = "Some Commercial EQ";
            description.manufacturerName = "Metric Halo";
            description.version = "4.0.89";

            const auto report = assessVibe (facts, description);
            check ("copy protected: refuses to conclude", report.conclusive ? 1.0 : 0.0, 0.0, 0.0);
            check ("copy protected: confidence", report.confidence, 0.05, 0.001);
        }
        {
            // Nothing readable at all: also an abstention, for a different reason.
            BinaryFacts facts;
            facts.ok = true;
            facts.stripped = true;

            juce::PluginDescription description;
            description.name = "Mystery";
            description.manufacturerName = "Someone";
            description.version = "3.1.4";

            const auto report = assessVibe (facts, description);
            check ("unreadable binary: refuses to conclude", report.conclusive ? 1.0 : 0.0, 0.0, 0.0);
        }
        {
            // Placeholder identity and template class names, with plenty to read.
            BinaryFacts facts;
            plentyOfEvidence (facts);
            facts.definedSymbols.add ("AudioPluginAudioProcessor");
            facts.strings.add ("TODO: implement proper gain smoothing");

            juce::PluginDescription description;
            description.name = "NewProject";
            description.manufacturerName = "Yourcompany";
            description.version = "1.0.0";

            const auto report = assessVibe (facts, description);
            check ("template-shaped plugin: conclusive", report.conclusive ? 1.0 : 0.0, 1.0, 0.0);
            check ("template-shaped plugin: scores high", report.score >= 70.0 ? 1.0 : 0.0, 1.0, 0.0);
        }
        {
            // Same amount of readable evidence, none of it incriminating.
            BinaryFacts facts;
            plentyOfEvidence (facts);

            juce::PluginDescription description;
            description.name = "Sway";
            description.manufacturerName = "Wright Audio";
            description.version = "2.3.1";

            const auto report = assessVibe (facts, description);
            check ("ordinary plugin: conclusive", report.conclusive ? 1.0 : 0.0, 1.0, 0.0);
            check ("ordinary plugin: scores zero", report.score, 0.0, 0.001);
        }
    }

    std::cout << "\nframework scaffolding is not evidence" << std::endl;
    {
        // Everything here exists in every hand-written JUCE plugin: the overrides the framework
        // requires, a plugin whose name appears in capitals and in title case, JUCE's own lock,
        // and a parameter lookup. None of it may add up to a verdict.
        BinaryFacts facts;
        facts.ok = true;

        for (int i = 0; i < 600; ++i)
            facts.definedSymbols.add ("_someSymbol" + juce::String (i));

        for (int i = 0; i < 2500; ++i)
            facts.strings.add ("ordinary string number " + juce::String (i));

        for (const auto* symbol : { "SwayProcessor::getStateInformation", "SwayProcessor::setStateInformation",
                                    "SwayProcessor::getNumPrograms", "SwayProcessor::changeProgramName",
                                    "juce::CriticalSection::enter", "juce::AudioProcessorValueTreeState::getRawParameterValue",
                                    "SwayEditor::timerCallback" })
            facts.definedSymbols.add (symbol);

        facts.strings.add ("SWAY");
        facts.strings.add ("Sway");
        facts.strings.add ("juce::Component");

        juce::PluginDescription description;
        description.name = "Sway";
        description.manufacturerName = "Wright Audio";
        description.version = "2.3.1";

        const auto report = assessVibe (facts, description);
        check ("hand-written JUCE plugin: conclusive", report.conclusive ? 1.0 : 0.0, 1.0, 0.0);
        check ("hand-written JUCE plugin: scores under 10%", report.score < 10.0 ? 1.0 : 0.0, 1.0, 0.0);

        // The pattern it is meant to catch must still register: two audio parameter IDs in
        // capitals, each with a title-case label.
        facts.strings.add ("GAIN");
        facts.strings.add ("Gain");
        facts.strings.add ("DRIVE");
        facts.strings.add ("Drive");
        check ("capitalised parameter IDs with labels still count", assessVibe (facts, description).score >= 25.0 ? 1.0 : 0.0, 1.0, 0.0);
    }

    std::cout << "\nmore ways to tell" << std::endl;
    {
        const auto baseline = [] (BinaryFacts& facts)
        {
            for (int i = 0; i < 600; ++i)
                facts.definedSymbols.add ("_someSymbol" + juce::String (i));

            for (int i = 0; i < 2500; ++i)
                facts.strings.add ("ordinary string number " + juce::String (i));

            facts.ok = true;
        };

        juce::PluginDescription description;
        description.name = "Sway";
        description.manufacturerName = "Wright Audio";
        description.version = "2.3.1";

        {
            // A build path through an AI tool's folder is strong evidence on its own.
            BinaryFacts facts;
            baseline (facts);
            facts.strings.add ("/Users/someone/.claude/projects/synth/Source/PluginProcessor.cpp");
            check ("build path through an AI tool folder scores", assessVibe (facts, description).score >= 30.0 ? 1.0 : 0.0, 1.0, 0.0);
        }
        {
            // A path that merely mentions a plain project folder does not.
            BinaryFacts facts;
            baseline (facts);
            facts.strings.add ("/Users/someone/dev/synth/Source/Voice.cpp");
            check ("an ordinary build path scores nothing", assessVibe (facts, description).score, 0.0, 0.001);
        }
        {
            // AudioUnit codes still on JUCE's defaults.
            BinaryFacts facts;
            baseline (facts);
            facts.infoPlist = "<key>manufacturer</key><string>Manu</string><key>subtype</key><string>Dem0</string>";
            check ("template AudioUnit codes score", assessVibe (facts, description).score, 14.0, 0.001);

            facts.infoPlist = "<key>manufacturer</key><string>Wrgt</string><key>subtype</key><string>Swy1</string>";
            check ("real AudioUnit codes score nothing", assessVibe (facts, description).score, 0.0, 0.001);
        }
        {
            // "Contest Audio" contains "test" but is not a placeholder; "Test" on its own is.
            BinaryFacts facts;
            baseline (facts);

            juce::PluginDescription real = description;
            real.manufacturerName = "Contest Audio";
            check ("a maker whose name contains 'test' is not a placeholder", assessVibe (facts, real).score, 0.0, 0.001);

            real.manufacturerName = "Test";
            check ("a maker called Test is", assessVibe (facts, real).score >= 18.0 ? 1.0 : 0.0, 1.0, 0.0);
        }
        {
            // Class names are read from the mangled symbol table.
            BinaryFacts facts;
            baseline (facts);
            facts.definedSymbols.add ("__ZN15PluginProcessor12processBlockERN4juce11AudioBufferIfEERNS0_10MidiBufferE");
            check ("generic class names count a little", assessVibe (facts, description).score, 6.0, 0.001);

            BinaryFacts framework;
            baseline (framework);
            framework.definedSymbols.add ("__ZN4juce15AudioProcessor12processBlockEv");
            check ("a framework class is not the author's", assessVibe (framework, description).score, 0.0, 0.001);
        }
        {
            // A developer team in the signature is listed as a point the other way, not scored.
            BinaryFacts facts;
            baseline (facts);
            facts.hasSignature = true;
            facts.teamIdentifier = "ABCDE12345";
            const auto report = assessVibe (facts, description);
            check ("a developer team signature is noted", report.humanSignals.joinIntoString (" ").contains ("ABCDE12345") ? 1.0 : 0.0, 1.0, 0.0);
        }

        {
            // Tutorial leftovers: one is ordinary, several together are the shape of a generated project.
            BinaryFacts facts;
            baseline (facts);
            facts.strings.add ("__GLOBAL__sub_I_PluginEditor.cpp");
            check ("one tutorial leftover scores a little", assessVibe (facts, description).score, 8.0, 0.001);

            facts.strings.add ("createParameterLayout");
            facts.strings.add ("Parameters");
            facts.strings.add ("getRawParameterValue");
            // files 8 + layout 8 + raw value 4 + three-together bonus 12
            check ("three leftovers together earn the bonus", assessVibe (facts, description).score, 32.0, 0.001);

            facts.definedSymbols.add ("__ZN12SwayProcessor13timerCallbackEv");
            facts.definedSymbols.add ("__ZN15SwayEditor13timerCallbackEv");
            const auto four = assessVibe (facts, description).score;
            check ("four leftovers together score well above one", four >= 40.0 ? 1.0 : 0.0, 1.0, 0.0);
        }

        {
            // A lone upper-case string is on hand-written plugins too, so it is not scored.
            BinaryFacts facts;
            baseline (facts);
            facts.strings.add ("RATE");
            check ("a single ALL CAPS string scores nothing", assessVibe (facts, description).score, 0.0, 0.001);
        }

        {
            // iPlug2's example controls kept as the interface.
            BinaryFacts facts;
            baseline (facts);
            facts.definedSymbols.add ("__ZN5iplug9igraphics20IVSlideSwitchControlC1Ev");
            facts.definedSymbols.add ("__ZN5iplug9igraphics19IVMenuButtonControlC1Ev");
            check ("stock iPlug2 controls score", assessVibe (facts, description).score, 8.0, 0.001);
        }
    }

    std::cout << "\nbehaviour on the audio thread" << std::endl;
    {
        BinaryFacts facts;

        for (int i = 0; i < 600; ++i)
            facts.definedSymbols.add ("_someSymbol" + juce::String (i));

        for (int i = 0; i < 2500; ++i)
            facts.strings.add ("ordinary string number " + juce::String (i));

        facts.ok = true;

        juce::PluginDescription description;
        description.name = "Sway";
        description.manufacturerName = "Wright Audio";
        description.version = "2.3.1";

        BehaviourReport clean;
        clean.ok = true;
        clean.allocationsMeasured = true;
        clean.cpuPercent = 0.4;
        clean.spikeRatio = 1.3;
        clean.smallBufferOverhead = 2.0;
        clean.denormalSlowdown = 1.0;

        check ("a plugin that behaves scores nothing", assessVibe (facts, description, &clean).score, 0.0, 0.001);
        check ("and says so", assessVibe (facts, description, &clean).humanSignals.joinIntoString (" ").contains ("no allocations") ? 1.0 : 0.0, 1.0, 0.0);

        auto allocating = clean;
        allocating.allocationsPerBlock = 1.0;
        allocating.fractionOfBlocksAllocating = 1.0;
        check ("allocating in every block scores 25", assessVibe (facts, description, &allocating).score, 25.0, 0.001);

        auto automated = clean;
        automated.allocationsPerBlockAutomated = 40.0;
        check ("allocating only under automation scores 15", assessVibe (facts, description, &automated).score, 15.0, 0.001);

        auto heavy = clean;
        heavy.cpuPercent = 12.0;
        check ("an unusually expensive plugin scores 12", assessVibe (facts, description, &heavy).score, 12.0, 0.001);

        auto unmeasured = clean;
        unmeasured.allocationsMeasured = false;
        check ("no allocation count on this system: nothing invented", assessVibe (facts, description, &unmeasured).score, 0.0, 0.001);

        // The result survives the trip between processes and the cache.
        const auto back = BehaviourReport::fromMachine (allocating.toMachine());
        check ("a result round-trips through its text form", back.ok && back.allocationsMeasured && std::abs (back.fractionOfBlocksAllocating - 1.0) < 1.0e-6 ? 1.0 : 0.0, 1.0, 0.0);
        check ("a garbled result is refused", BehaviourReport::fromMachine ("nonsense").ok ? 1.0 : 0.0, 0.0, 0.0);
    }

    std::cout << "\nupdate notice" << std::endl;
    {
        check ("a later patch is newer", isNewerVersion ("0.5.1", "0.5.0") ? 1.0 : 0.0, 1.0, 0.0);
        check ("0.10.0 is later than 0.9.0, not earlier", isNewerVersion ("0.10.0", "0.9.0") ? 1.0 : 0.0, 1.0, 0.0);
        check ("the same version is not newer", isNewerVersion ("0.5.0", "0.5.0") ? 1.0 : 0.0, 0.0, 0.0);
        check ("an older version is not newer", isNewerVersion ("0.4.9", "0.5.0") ? 1.0 : 0.0, 0.0, 0.0);
        check ("a leading v is ignored", isNewerVersion ("v1.0.0", "0.9.9") ? 1.0 : 0.0, 1.0, 0.0);
        check ("nonsense is never newer", isNewerVersion ("", "0.5.0") ? 1.0 : 0.0, 0.0, 0.0);

        const auto yes = parseUpdateInfo ("{\"version\":\"0.6.0\",\"page\":\"https://example.com/get\",\"notes\":\"Faster scans\\nmore\"}", "0.5.0");
        check ("a newer release is reported", yes.available ? 1.0 : 0.0, 1.0, 0.0);
        check ("with its version and first line of notes", yes.version == "0.6.0" && yes.notes == "Faster scans" ? 1.0 : 0.0, 1.0, 0.0);
        check ("the same release is not reported", parseUpdateInfo ("{\"version\":\"0.5.0\",\"page\":\"https://example.com\"}", "0.5.0").available ? 1.0 : 0.0, 0.0, 0.0);
        check ("a page that is not a web address is refused", parseUpdateInfo ("{\"version\":\"9.0.0\",\"page\":\"file:///etc/passwd\"}", "0.5.0").available ? 1.0 : 0.0, 0.0, 0.0);
        check ("garbage is no update", parseUpdateInfo ("<html>", "0.5.0").available ? 1.0 : 0.0, 0.0, 0.0);
        check ("a local build with no address never checks", checkForUpdate ({}, "0.5.0").available ? 1.0 : 0.0, 0.0, 0.0);
    }

    std::cout << "\nWindows binaries" << std::endl;
    {
        // A minimal PE32+ file, built by hand: one section, one imported DLL with one function, one
        // MSVC class name, one wide string, and a signature directory entry.
        std::vector<uint8_t> pe (0x800, 0);
        const auto put16 = [&pe] (size_t at, uint32_t v) { pe[at] = (uint8_t) v; pe[at + 1] = (uint8_t) (v >> 8); };
        const auto put32 = [&put16] (size_t at, uint32_t v) { put16 (at, v & 0xffff); put16 (at + 2, v >> 16); };
        const auto putText = [&pe] (size_t at, const char* text) { for (size_t i = 0; text[i] != 0; ++i) pe[at + i] = (uint8_t) text[i]; };

        pe[0] = 'M'; pe[1] = 'Z';
        put32 (0x3c, 0x80);
        putText (0x80, "PE");                       // "PE\0\0"
        put16 (0x84, 0x8664);                      // x86_64
        put16 (0x86, 1);                           // one section
        put16 (0x94, 240);                         // optional header size
        put16 (0x98, 0x20b);                       // PE32+
        put32 (0x98 + 108, 16);                    // data directories
        put32 (0x98 + 112 + 1 * 8, 0x1000);        // import directory: RVA
        put32 (0x98 + 112 + 1 * 8 + 4, 40);
        put32 (0x98 + 112 + 4 * 8, 0x3000);        // security directory (a file offset)
        put32 (0x98 + 112 + 4 * 8 + 4, 100);

        const size_t section = 0x98 + 240;
        putText (section, ".rdata");
        put32 (section + 8, 0x1000);               // virtual size
        put32 (section + 12, 0x1000);              // virtual address
        put32 (section + 16, 0x400);               // raw size
        put32 (section + 20, 0x400);               // raw pointer

        const size_t raw = 0x400;                  // RVA 0x1000 maps here
        put32 (raw + 0, 0x1100);                   // import descriptor: original first thunk
        put32 (raw + 12, 0x1080);                  //   name
        put32 (raw + 16, 0x1100);                  //   first thunk
        putText (raw + 0x80, "KERNEL32.dll");
        put32 (raw + 0x100, 0x1140);               // thunk -> hint/name
        putText (raw + 0x140 + 2, "sin");
        putText (raw + 0x200, ".?AVEditor@Sway@@");

        const char* wide = "Wright Audio";
        for (size_t i = 0; wide[i] != 0; ++i)
            pe[raw + 0x240 + i * 2] = (uint8_t) wide[i];

        const auto facts = readPEFromMemory (pe.data(), pe.size(), 1000, 1000);
        check ("a hand-built Windows executable is read", facts.ok ? 1.0 : 0.0, 1.0, 0.0);
        check ("its architecture is named", facts.architecture == "x86_64" ? 1.0 : 0.0, 1.0, 0.0);
        check ("the DLL it imports is listed", facts.linkedLibraries.contains ("KERNEL32.dll") ? 1.0 : 0.0, 1.0, 0.0);
        check ("the function it imports is listed, spelt as on a Mac", facts.undefinedSymbols.contains ("_sin") ? 1.0 : 0.0, 1.0, 0.0);
        check ("a class name is recovered from run-time type information", facts.definedSymbols.contains ("__ZN4Sway6EditorE") ? 1.0 : 0.0, 1.0, 0.0);
        check ("wide strings are read, which is where company names live", facts.strings.contains ("Wright Audio") ? 1.0 : 0.0, 1.0, 0.0);
        check ("a signature is noticed", facts.hasSignature ? 1.0 : 0.0, 1.0, 0.0);

        const uint8_t notPe[64] = { 'M', 'Z' };
        check ("garbage that starts with MZ is refused", readPEFromMemory (notPe, sizeof (notPe), 10, 10).ok ? 1.0 : 0.0, 0.0, 0.0);
        uint8_t machO[64] = { 0xcf, 0xfa, 0xed, 0xfe };
        check ("a Mach-O header is not a Windows executable", readPEFromMemory (machO, sizeof (machO), 10, 10).ok ? 1.0 : 0.0, 0.0, 0.0);

        const auto path = classPathFromRtti (".?AVEditor@Sway@@");
        check ("an MSVC class name becomes a path, outermost first", path.size() == 2 && path[0] == "Sway" && path[1] == "Editor" ? 1.0 : 0.0, 1.0, 0.0);
        check ("a template instance is not the author's name", classPathFromRtti (".?AV?$vector@H@std@@").isEmpty() ? 1.0 : 0.0, 1.0, 0.0);

        // The scoring engine treats Windows fairly: no symbol table is normal, and sin() proves nothing.
        BinaryFacts windows;
        windows.ok = true;
        windows.platform = "Windows";
        windows.symbolTableExpected = false;
        windows.undefinedSymbols.add ("_sin");
        windows.undefinedSymbols.add ("_cos");

        for (int i = 0; i < 2500; ++i)
            windows.strings.add ("ordinary string number " + juce::String (i));

        juce::PluginDescription description;
        description.name = "Sway";
        description.manufacturerName = "Wright Audio";
        description.version = "2.3.1";

        const auto report = assessVibe (windows, description);
        check ("a Windows plugin without symbols can still be judged", report.conclusive ? 1.0 : 0.0, 1.0, 0.0);
        check ("calling sin() on Windows scores nothing", report.score, 0.0, 0.001);
    }

    std::cout << "\nexport and merge" << std::endl;
    {
        // Two people export the same plugin; a master list holds it once, with both reports.
        const auto makeEntry = [] (const juce::String& name, const juce::String& maker, double score, const juce::String& binary)
        {
            SweepEntry entry;
            entry.description.name = name;
            entry.description.manufacturerName = maker;
            entry.description.pluginFormatName = "VST3";
            entry.description.version = "1.0.0";
            entry.description.fileOrIdentifier = "/Users/someone/Library/" + name + ".vst3";
            entry.score = score;
            entry.confidence = 1.0;
            entry.conclusive = true;
            entry.headline = "x";
            entry.binaryId = binary;
            entry.findings.push_back ({ "boilerplate", "default template source file names in the binary", 8.0 });
            return entry;
        };

        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vibecheck-selftest-merge");
        folder.deleteRecursively();
        folder.createDirectory();

        const auto a = folder.getChildFile ("a.json");
        const auto b = folder.getChildFile ("b.json");

        const std::vector<SweepEntry> first { makeEntry ("Alpha", "Acme", 40.0, "10-aaaa"), makeEntry ("Beta", "Acme", 2.0, "20-bbbb") };
        const std::vector<SweepEntry> second { makeEntry ("Alpha", "Acme", 50.0, "10-aaaa"), makeEntry ("Gamma", "Other", 9.0, "30-cccc") };

        check ("an export is written", vibecheck::writeExport (a, first, "9.9").wasOk() ? 1.0 : 0.0, 1.0, 0.0);
        vibecheck::writeExport (b, second, "9.9");

        const auto text = a.loadFileAsString();
        check ("an export holds no file path", text.contains ("/Users/") || text.contains ("someone") ? 1.0 : 0.0, 0.0, 0.0);

        const auto merged = vibecheck::mergeExports (vibecheck::exportFilesFrom (folder.getFullPathName()));
        check ("both files are read", merged.filesRead, 2.0, 0.0);
        check ("four rows become three plugins", merged.pluginsMerged, 3.0, 0.0);

        double alphaScore = -1.0, alphaReports = -1.0;

        if (const auto* list = merged.master["plugins"].getArray())
            for (const auto& plugin : *list)
                if (plugin["name"].toString() == "Alpha")
                {
                    alphaScore = (double) plugin["score"];
                    alphaReports = (double) plugin["reports"];
                }

        check ("a shared plugin takes the median score", alphaScore, 45.0, 0.001);
        check ("and counts both reports", alphaReports, 2.0, 0.0);

        const auto stranger = folder.getChildFile ("c.json");
        stranger.replaceWithText ("{\"format\":\"something-else\"}");
        const auto withStranger = vibecheck::mergeExports (vibecheck::exportFilesFrom (folder.getFullPathName()));
        check ("a file that is not an export is skipped, not fatal", withStranger.filesRead, 2.0, 0.0);
        check ("and reported", withStranger.problems.size(), 1.0, 0.0);

        folder.deleteRecursively();
    }

    std::cout << "\nplugin menu" << std::endl;
    {
        // A flat menu of five hundred plugins is unusable, but a hierarchy is only an
        // improvement if everything in it can still be reached and still maps to the right
        // plugin. Both of those are worth checking rather than assuming.
        juce::Array<juce::PluginDescription> types;

        for (const auto* maker : { "Apple", "Metric Halo", "Wright Audio", "Sonosaurus" })
        {
            for (int i = 1; i <= 25; ++i)
            {
                juce::PluginDescription description;
                description.name = juce::String (maker).removeCharacters (" ") + " Thing " + juce::String (i);
                description.manufacturerName = maker;
                description.pluginFormatName = (i % 2 == 0) ? "VST3" : "AudioUnit";
                description.fileOrIdentifier = "/tmp/vibecheck-fixture/" + description.name + ".vst3";
                description.uniqueId = types.size() + 1;
                types.add (description);
            }
        }

        juce::ComboBox box;
        vibecheck::PluginChooser chooser;
        chooser.setSource (types);
        chooser.populate (box);

        std::set<int> reachable;
        int mismatched = 0;

        if (auto* root = box.getRootMenu())
        {
            for (juce::PopupMenu::MenuItemIterator iterator (*root, true); iterator.next();)
            {
                const auto& item = iterator.getItem();

                if (item.itemID == 0)
                    continue;

                const auto index = chooser.sourceIndexForSelectedId (item.itemID);

                if (! juce::isPositiveAndBelow (index, types.size()))
                {
                    ++mismatched;
                    continue;
                }

                // The menu text must name the plugin the id resolves to, or picking an entry
                // would quietly analyse a different plugin.
                if (! item.text.contains (types[index].name))
                    ++mismatched;

                reachable.insert (index);
            }
        }

        check ("every plugin is reachable in the menu", (double) reachable.size(), (double) types.size(), 0.0);
        check ("no entry resolves to the wrong plugin", (double) mismatched, 0.0, 0.0);

        // Searching narrows the menu, and the narrowed menu must still select the right plugin:
        // the ids are positions in the filtered list, not in the library.
        chooser.setFilter ("Metric Halo");
        chooser.populate (box);

        int filteredMismatches = 0;
        std::set<int> filteredReachable;

        if (auto* root = box.getRootMenu())
        {
            for (juce::PopupMenu::MenuItemIterator iterator (*root, true); iterator.next();)
            {
                const auto& item = iterator.getItem();

                if (item.itemID == 0)
                    continue;

                const auto index = chooser.sourceIndexForSelectedId (item.itemID);

                if (! juce::isPositiveAndBelow (index, types.size())
                    || types[index].manufacturerName != "Metric Halo"
                    || ! item.text.contains (types[index].name))
                    ++filteredMismatches;
                else
                    filteredReachable.insert (index);
            }
        }

        check ("search finds every matching plugin", (double) filteredReachable.size(), 25.0, 0.0);
        check ("search results select the right plugin", (double) filteredMismatches, 0.0, 0.0);
    }

    std::cout << "\n" << (failures == 0 ? "all checks passed" : juce::String (failures) + " CHECK(S) FAILED") << std::endl;
    return failures;
}
} // namespace vibecheck
