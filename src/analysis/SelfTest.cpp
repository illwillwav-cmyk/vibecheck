#include "SelfTest.h"

#include "Measurements.h"
#include "audio/TestSignal.h"
#include "ui/PluginChooser.h"
#include "vibecheck/Export.h"
#include "vibecheck/Heuristics.h"
#include "vibecheck/Labels.h"
#include "vibecheck/PEReader.h"
#include "vibecheck/SourceInspector.h"
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

    std::cout << "\nsource code" << std::endl;
    {
        const auto has = [] (const SourceReport& report, const char* start)
        {
            for (const auto& finding : report.findings)
                if (finding.finding.startsWith (start))
                    return true;

            return false;
        };

        // A generated-looking plugin: empty state functions, and an audio callback that allocates,
        // locks and looks parameters up by name.
        {
            SourceScan scan;
            scan.addSource ("Source/PluginProcessor.cpp", juce::String (juce::CharPointer_UTF8 (
                "void P::getStateInformation (juce::MemoryBlock& destData) {}\n"
                "void P::setStateInformation (const void* data, int size) { juce::ignoreUnused (data, size); }\n"
                "void P::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)\n"
                "{\n"
                "    std::vector<float> temp;\n"
                "    temp.push_back (1.0f);\n"
                "    auto* scratch = new float[512];\n"
                "    const std::lock_guard<std::mutex> guard (mutex);\n"
                "    auto a = apvts.getRawParameterValue (\"A\")->load();\n"
                "    auto b = apvts.getRawParameterValue (\"B\")->load();\n"
                "    auto c = apvts.getRawParameterValue (\"C\")->load();\n"
                "    // \"new\" in a comment, and a string that says new Thing, must not count\n"
                "    log (\"allocating new Buffer here\");\n"
                "}\n")));
            const auto report = scan.finish ("a test");

            check ("empty save and restore are found", has (report, "saving and restoring settings are both empty") ? 1.0 : 0.0, 1.0, 0.0);
            check ("an allocation in the audio callback is found", has (report, "the audio callback asks for memory") ? 1.0 : 0.0, 1.0, 0.0);
            check ("a growing container in the audio callback is found", has (report, "the audio callback grows a container") ? 1.0 : 0.0, 1.0, 0.0);
            check ("a lock in the audio callback is found", has (report, "the audio callback waits for a lock") ? 1.0 : 0.0, 1.0, 0.0);
            check ("parameter lookups by name are found", has (report, "looks parameters up by name") ? 1.0 : 0.0, 1.0, 0.0);
            // 12 for the state functions, 3 x 6 for the callback, 4 for the lookups.
            check ("and they add up as documented", report.points(), 34.0, 0.001);

            bool quotedCleanly = false;

            for (const auto& finding : report.findings)
                if (finding.finding.startsWith ("looks parameters") && finding.detail.contains ("getRawParameterValue (\"A\")"))
                    quotedCleanly = true;

            check ("examples quote the line as written", quotedCleanly ? 1.0 : 0.0, 1.0, 0.0);
        }

        // A careful plugin: real state functions, a clean callback, the same words only in comments
        // and strings. Nothing should be found.
        {
            SourceScan scan;
            scan.addSource ("Source/Proc.cpp", juce::String (
                "void P::getStateInformation (juce::MemoryBlock& d) { copyXmlToBinary (*state.createXml(), d); }\n"
                "void P::setStateInformation (const void* data, int size) { restore (data, size); }\n"
                "void P::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)\n"
                "{\n"
                "    // never call new or push_back here; take no lock_guard\n"
                "    const juce::SpinLock::ScopedTryLockType attempt (lock);\n"
                "    renewGain (buffer);   /* malloc( is only mentioned */\n"
                "    for (int i = 0; i < buffer.getNumSamples(); ++i) process (i);\n"
                "}\n"
                "void helper() { auto* later = new Thing(); std::vector<int> v; v.push_back (1); }\n"));
            const auto report = scan.finish ("a test");
            check ("a clean plugin's source scores nothing", report.points(), 0.0, 0.001);
            check ("and is still a successful read", report.ok ? 1.0 : 0.0, 1.0, 0.0);
        }

        // Typographic characters, step comments, and the repository's own evidence.
        {
            SourceScan scan;
            juce::String text;

            for (int i = 0; i < 6; ++i)
                text << juce::String (juce::CharPointer_UTF8 ("int v")) << i << juce::String (juce::CharPointer_UTF8 (" = 0; // set up \xe2\x80\x94 then go \xe2\x86\x92 next\n"));

            text << "// Step 1: load\n// Step 2: run\n// Step 3: save\n";
            scan.addSource ("a.cpp", text);
            scan.noteFile ("CLAUDE.md");
            scan.noteFile (".cursor/rules/style.mdc");
            scan.noteFile ("docs/readme.md");
            scan.addCommitMessages ({ "Fix the meter\n\nCo-Authored-By: Claude <noreply@anthropic.com>", "Tidy up", "Bump version", "Add tests" });
            const auto report = scan.finish ("a test");

            check ("typographic comments are found", has (report, "typographic characters") ? 1.0 : 0.0, 1.0, 0.0);
            check ("numbered step comments are found", has (report, "numbered step comments") ? 1.0 : 0.0, 1.0, 0.0);
            check ("AI tool files are found", has (report, "the repository is set up for an AI coding tool") ? 1.0 : 0.0, 1.0, 0.0);
            check ("an AI co-author line is found", has (report, "AI co-author lines") ? 1.0 : 0.0, 1.0, 0.0);
            // 28 (one commit in four) + 20 + 6 + 4
            check ("and they add up as documented", report.points(), 58.0, 0.001);

            const auto back = SourceReport::fromJson (report.toJson());
            check ("a source report round-trips", back.ok && back.findings.size() == report.findings.size() && std::abs (back.points() - report.points()) < 1.0e-6 ? 1.0 : 0.0, 1.0, 0.0);
        }

        {
            SourceScan scan;
            scan.addCommitMessages ({ "Add reverb\n\nCo-authored-by: Jane Smith <jane@example.com>", "Fix clip" });
            scan.addSource ("a.cpp", "int main() { return 0; }\n");
            check ("a human co-author is not an AI trailer", scan.finish ("t").points(), 0.0, 0.001);
        }

        check ("framework folders are not read", SourceScan::wantsContents ("JUCE/modules/juce_core/juce_core.cpp", 100) ? 1.0 : 0.0, 0.0, 0.0);
        check ("third-party folders are not read", SourceScan::wantsContents ("third_party/lib/x.cpp", 100) ? 1.0 : 0.0, 0.0, 0.0);
        check ("the plugin's own source is read", SourceScan::wantsContents ("Source/PluginProcessor.cpp", 100) ? 1.0 : 0.0, 1.0, 0.0);
        check ("generated resource files are not read", SourceScan::wantsContents ("Source/BinaryData.cpp", 100) ? 1.0 : 0.0, 0.0, 0.0);
        check ("files that are not code are not read", SourceScan::wantsContents ("Source/notes.txt", 100) ? 1.0 : 0.0, 0.0, 0.0);

        const auto repo = parseRepository ("see https://github.com/SessionLoops/PitchNet.git for source");
        check ("a GitHub address is parsed", repo.owner == "SessionLoops" && repo.name == "PitchNet" ? 1.0 : 0.0, 1.0, 0.0);
        check ("something that is not one is refused", parseRepository ("https://example.com/a/b").isValid() ? 1.0 : 0.0, 0.0, 0.0);

        const auto found = findRepository ({ "https://github.com/juce-framework/JUCE", "https://github.com/steinbergmedia/vst3sdk",
                                             "Report bugs at https://github.com/acme/sway/issues", "https://github.com/acme/sway" });
        check ("a plugin's own repository is picked out from the frameworks'", found.owner == "acme" && found.name == "sway" ? 1.0 : 0.0, 1.0, 0.0);
        check ("a binary with only framework addresses has none", findRepository ({ "https://github.com/juce-framework/JUCE" }).isValid() ? 1.0 : 0.0, 0.0, 0.0);

        // Source evidence joins the score, and makes an unreadable binary judgeable.
        BinaryFacts sealed;
        sealed.ok = true;
        sealed.paceWrapped = true;

        juce::PluginDescription description;
        description.name = "Sway";
        description.manufacturerName = "Wright Audio";
        description.version = "2.3.1";

        SourceReport source;
        source.ok = true;
        source.origin = "github.com/acme/sway";
        source.findings.push_back ({ "AI co-author lines in the commit history", "3 of the last 20 commits", "why", 28.0 });

        check ("a copy-protected plugin cannot be judged from its binary", assessVibe (sealed, description).conclusive ? 1.0 : 0.0, 0.0, 0.0);
        const auto judged = assessVibe (sealed, description, nullptr, &source);
        check ("but can from its source", judged.conclusive ? 1.0 : 0.0, 1.0, 0.0);
        check ("and the source points are its score", judged.score, 28.0, 0.001);
        check ("under their own family", judged.pointsFor (Family::source), 28.0, 0.001);
    }

    std::cout << "\nlabels" << std::endl;
    {
        check ("labels survive their text form", labelFromString (toString (Label::vibeCoded)) == Label::vibeCoded
                                                 && labelFromString (toString (Label::handWritten)) == Label::handWritten
                                                 && labelFromString ("") == Label::none ? 1.0 : 0.0, 1.0, 0.0);
        check ("both builds of a plugin share a label", labelKey ("AL-1", "Naturl Audio") == labelKey (" al-1 ", "NATURL AUDIO") ? 1.0 : 0.0, 1.0, 0.0);

        const auto plugin = [] (const char* name, double score, const char* label, std::initializer_list<const char*> findings)
        {
            auto* object = new juce::DynamicObject();
            object->setProperty ("name", name);
            object->setProperty ("score", score);
            object->setProperty ("conclusive", true);
            object->setProperty ("label", label);
            juce::Array<juce::var> list;

            for (const auto* finding : findings)
            {
                auto* item = new juce::DynamicObject();
                item->setProperty ("finding", finding);
                list.add (juce::var (item));
            }

            object->setProperty ("findings", list);
            return juce::var (object);
        };

        juce::Array<juce::var> plugins { plugin ("A", 60.0, "ai", { "template files", "timer editor" }),
                                         plugin ("B", 35.0, "ai", { "template files" }),
                                         plugin ("C", 12.0, "human", { "timer editor" }),
                                         plugin ("D", 0.0, "human", {}),
                                         plugin ("E", 80.0, "", { "template files" }) };
        const auto text = evaluateLabels (juce::var (plugins));

        check ("the evaluation counts each group", text.contains ("2 known vibe-coded, 2 known hand-written (of 5 rows)") ? 1.0 : 0.0, 1.0, 0.0);
        check ("it reports what the 30 line catches and wrongly flags", text.contains ("catches 2 of 2 vibe-coded, wrongly flags 0 of 2 hand-written") ? 1.0 : 0.0, 1.0, 0.0);
        check ("it reports that the 10 line wrongly flags one", text.contains ("catches 2 of 2 vibe-coded, wrongly flags 1 of 2 hand-written") ? 1.0 : 0.0, 1.0, 0.0);
        check ("it lists the most telling fingerprint first", text.indexOf ("template files") < text.indexOf ("timer editor") && text.indexOf ("template files") > 0 ? 1.0 : 0.0, 1.0, 0.0);
        check ("with one kind missing it says what is needed", evaluateLabels (juce::var (juce::Array<juce::var> { plugin ("A", 60.0, "ai", {}) })).contains ("Both kinds are needed") ? 1.0 : 0.0, 1.0, 0.0);
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

        // Each person has labelled Alpha as vibe-coded; only the first has labelled Beta.
        const std::map<juce::String, Label> firstLabels { { labelKey ("Alpha", "Acme"), Label::vibeCoded }, { labelKey ("Beta", "Acme"), Label::handWritten } };
        const std::map<juce::String, Label> secondLabels { { labelKey ("Alpha", "Acme"), Label::vibeCoded } };

        check ("an export is written", vibecheck::writeExport (a, first, "9.9", firstLabels).wasOk() ? 1.0 : 0.0, 1.0, 0.0);
        vibecheck::writeExport (b, second, "9.9", secondLabels);

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

        double alphaAi = -1.0, betaHuman = -1.0;
        juce::String alphaLabel, gammaLabel = "unset";

        if (const auto* list = merged.master["plugins"].getArray())
            for (const auto& plugin : *list)
            {
                if (plugin["name"].toString() == "Alpha") { alphaAi = (double) plugin["labelAi"]; alphaLabel = plugin["label"].toString(); }
                if (plugin["name"].toString() == "Beta")  betaHuman = (double) plugin["labelHuman"];
                if (plugin["name"].toString() == "Gamma") gammaLabel = plugin["label"].toString();
            }

        check ("two people's labels count as two votes", alphaAi, 2.0, 0.0);
        check ("and decide the plugin's label", alphaLabel == "ai" ? 1.0 : 0.0, 1.0, 0.0);
        check ("one person's label counts as one", betaHuman, 1.0, 0.0);
        check ("an unlabelled plugin stays unlabelled", gammaLabel.isEmpty() ? 1.0 : 0.0, 1.0, 0.0);
        check ("the spreadsheet carries the votes", vibecheck::masterToCsv (merged.master).contains ("says_vibe_coded") ? 1.0 : 0.0, 1.0, 0.0);

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
