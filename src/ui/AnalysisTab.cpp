#include "AnalysisTab.h"

#include "analysis/MeasureSuite.h"
#include "analysis/Measurements.h"
#include "audio/OfflineRenderer.h"
#include "host/PluginLoader.h"
#include "ui/Theme.h"

#include <functional>
#include <utility>

namespace
{
const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));

juce::String signedDb (double value, int decimals = 1)
{
    return (value > 0.0 ? "+" : "") + mbs::num (value, decimals) + " dB";
}

/** Holds a plugin's own editor. The plugin outlives the window, never the other way round. */
class EditorWindow final : public juce::DocumentWindow
{
public:
    EditorWindow (juce::AudioPluginInstance& instance, std::function<void()> onClose)
        : DocumentWindow (instance.getName(), mbs::theme().paper, DocumentWindow::closeButton),
          closed (std::move (onClose))
    {
        setUsingNativeTitleBar (true);

        if (auto* editor = instance.createEditorIfNeeded())
            setContentOwned (editor, true);
        else
            setContentOwned (new juce::GenericAudioProcessorEditor (instance), true);

        setResizable (true, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        if (closed != nullptr)
            closed();
    }

private:
    std::function<void()> closed;
};
/** Holds the parameter list for the loaded plugin. Like the editor window, it must go before the plugin does. */
class ParameterWindow final : public juce::DocumentWindow
{
public:
    ParameterWindow (juce::AudioPluginInstance& instance, std::function<void()> onChange, std::function<void()> onClose)
        : DocumentWindow (instance.getName() + " parameters", mbs::theme().paper, DocumentWindow::closeButton),
          closed (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new ParameterPanel (instance, std::move (onChange)), true);
        setResizable (true, false);
        setResizeLimits (360, 300, 900, 1400);
        centreWithSize (460, 680);
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        if (closed != nullptr)
            closed();
    }

private:
    std::function<void()> closed;
};
} // namespace

/** Everything one run produced: the captures plus every measurement we can take from them. */
struct AnalysisTab::Outcome
{
    vibecheck::SignalType shown = vibecheck::SignalType::sine;
    double sampleRate = 48000.0;
    juce::AudioBuffer<float> captured;

    vibecheck::FrequencyResponse response;
    vibecheck::ResponseSummary responseSummary;
    vibecheck::HarmonicResult harmonics;
    vibecheck::ImdResult imd;
    vibecheck::TransferCurve transfer;
    vibecheck::TransferAnalysis transferAnalysis;
    vibecheck::TailResult tail;
    vibecheck::LevelStats levels, noise;
    vibecheck::AliasResult alias;

    bool instrument = false;
    double noteHz = 0.0, noteOnsetMs = 0.0, noteReleaseMs = 0.0;

    bool sweepsRan = false;
    std::vector<double> sweepLevelDb, thdVsLevelDb, sweepFrequencyHz, thdVsFrequencyDb;

    int latencySamples = 0;
    double renderSeconds = 0.0;
    bool anyOk = false;

    juce::String console, status, report;
};

// --- Results dashboard --------------------------------------------------------------------------

class AnalysisTab::Results final : public juce::Component
{
public:
    Results()
    {
        for (auto* tile : { &thd, &imd, &gain, &latency, &noise })
            addAndMakeVisible (tile);

        for (auto* graph : { &magnitude, &phase, &spectrum, &transfer, &thdLevel, &thdFrequency })
            addAndMakeVisible (graph);

        addAndMakeVisible (waveform);

        for (auto [id, text] : { std::pair<int, const char*> { 1, "Raw" }, { 2, "1/12 octave" }, { 3, "1/6 octave" }, { 4, "1/3 octave" } })
            smoothing.addItem (text, id);

        smoothing.setSelectedId (3, juce::dontSendNotification);
        smoothing.onChange = [this] { refreshTraces(); };
        addAndMakeVisible (smoothing);

        magnitude.setAxes ({ 20.0, 24000.0, true, "Hz", {}, false }, { -48.0, 12.0, false, {}, " dB", true });
        phase.setAxes     ({ 20.0, 24000.0, true, "Hz", {}, false }, { -180.0, 180.0, false, {}, juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")), true });
        spectrum.setAxes  ({ 20.0, 24000.0, true, "Hz", {}, false }, { -160.0, 0.0, false, {}, " dB", true });
        transfer.setAxes  ({ -72.0, 0.0, false, "dB in", " dB", false }, { -72.0, 0.0, false, {}, " dB", true });

        thdLevel.setAxes     ({ -60.0, 0.0, false, "dBFS in", {}, false }, { -120.0, 0.0, false, {}, " dB", true });
        thdFrequency.setAxes ({ 20.0, 24000.0, true, "Hz", {}, false }, { -120.0, 0.0, false, {}, " dB", true });
        thdLevel.setPlaceholder ("Turn on \"Also sweep distortion\" and run again to see distortion change with level");
        thdFrequency.setPlaceholder ("The same sweep across frequency appears here");

        magnitude.setPlaceholder ("Load a plugin and run the measurements to see its frequency response");
        phase.setPlaceholder ("Phase appears here, unwrapped so a delay shows as a straight slope");
        spectrum.setPlaceholder ("Harmonics and intermodulation products appear here");
        transfer.setPlaceholder ("The level-in versus level-out curve appears here");

        clear();
    }

    void clear()
    {
        thd.set ("-", "Total harmonic distortion");
        imd.set ("-", "SMPTE, 60 Hz and 7 kHz");
        gain.set ("-", "Insertion gain");
        latency.set ("-", "Reported by the plugin");
        noise.set ("-", "Output with silence in");

        for (auto& row : rows)
            row.second = "Not measured yet";

        for (auto* graph : { &magnitude, &phase, &spectrum, &transfer, &thdLevel, &thdFrequency })
            graph->clearTraces();
    }

    void setOutcome (std::shared_ptr<const Outcome> newOutcome)
    {
        outcome = std::move (newOutcome);
        const auto& o = *outcome;

        // Headline numbers.
        if (o.harmonics.ok)
            thd.set (mbs::percent (o.harmonics.thdPercent), "THD+N " + mbs::percent (o.harmonics.thdPlusNPercent));
        else
            thd.set ("-", "No steady tone captured");

        if (o.imd.ok)
            imd.set (mbs::percent (o.imd.imdPercent), "Carrier " + mbs::num (o.imd.carrierDb, 1) + " dBFS");
        else
            imd.set ("-", "No two-tone capture");

        if (o.responseSummary.ok)
            gain.set (signedDb (o.responseSummary.gainAt1kDb, 2), "Delay " + mbs::num (o.responseSummary.groupDelayMs, 2) + " ms");
        else
            gain.set ("-", "No impulse response");

        const auto latencyMs = o.sampleRate > 0.0 ? 1000.0 * o.latencySamples / o.sampleRate : 0.0;
        latency.set (mbs::num (latencyMs, 2) + " ms", mbs::num (o.latencySamples) + " samples");

        if (o.noise.ok)
            noise.set (o.noise.rmsDb <= -150.0 ? "Silent" : mbs::num (o.noise.rmsDb, 1) + " dBFS",
                       "Peak " + mbs::num (o.noise.peakDb, 1) + " dBFS" + (std::abs (o.noise.dcOffset) > 1.0e-4 ? ", DC present" : ""),
                       o.noise.rmsDb > -80.0 ? mbs::theme().warn : juce::Colours::transparentBlack);
        else
            noise.set ("-", "Not measured");

        // The plain-language reading.
        if (o.instrument)
        {
            static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            const auto midi = juce::roundToInt (69.0 + 12.0 * std::log2 (o.noteHz / 440.0));
            const auto noteName = juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);

            imd.set ("n/a", "needs audio input");
            gain.set ("n/a", "needs audio input");

            rows[0].second = "Instrument: measured by playing " + noteName + " (" + mbs::num (o.noteHz, 1) + " Hz). Frequency response needs audio input.";
            rows[1].second = o.harmonics.ok ? o.harmonics.character + (o.harmonics.harmonics.size() >= 2
                                                                           ? dot + "H2 " + mbs::num (o.harmonics.harmonics[0].relativeDb, 0) + " dBc, H3 "
                                                                                 + mbs::num (o.harmonics.harmonics[1].relativeDb, 0) + " dBc" : juce::String())
                                            : "No steady note captured";
            rows[2].second = "Not applicable to an instrument";
            rows[3].second = "Not measured for instruments";
            rows[4].second = "Sound starts " + mbs::num (o.noteOnsetMs, 1) + " ms after the key" + dot
                             + (o.noteReleaseMs < 5.0 ? "stops at once on release" : "rings for " + mbs::num (o.noteReleaseMs, 0) + " ms after release");
        }
        else
        {
            rows[0].second = o.responseSummary.ok
                               ? o.responseSummary.description + (std::abs (o.responseSummary.gainAt1kDb) >= 0.1
                                                                     ? dot + "level " + signedDb (o.responseSummary.gainAt1kDb) + " at 1 kHz" : juce::String())
                               : "No usable impulse response";
            rows[1].second = o.harmonics.ok
                               ? o.harmonics.character + (o.harmonics.harmonics.size() >= 2
                                                              ? dot + "H2 " + mbs::num (o.harmonics.harmonics[0].relativeDb, 0) + " dBc, H3 "
                                                                    + mbs::num (o.harmonics.harmonics[1].relativeDb, 0) + " dBc" : juce::String())
                               : "No steady tone captured";
            rows[2].second = o.transferAnalysis.ok ? o.transferAnalysis.description : "No level sweep captured";
            rows[3].second = o.alias.ok ? o.alias.description + dot + mbs::num (o.alias.worstDbc, 0) + " dBc"
                                              + (o.alias.worstDbc > -100.0 ? " at " + mbs::num (o.alias.worstHz, 0) + " Hz" : juce::String())
                                        : "Not measured";
            rows[4].second = o.tail.ok ? (o.tail.truncated ? "Still ringing after " + mbs::num (o.tail.tailMs, 0) + " ms: a reverb or long delay"
                                                             : o.tail.tailMs < 5.0 ? "Effectively none: the response dies within 5 ms"
                                                                                    : "Rings for " + mbs::num (o.tail.tailMs, 0) + " ms before falling 60 dB")
                                       : "No impulse response";
        }

        rows[5].second = o.levels.ok ? "Peak " + mbs::num (o.levels.peakDb, 1) + " dBFS" + dot + "RMS " + mbs::num (o.levels.rmsDb, 1) + " dBFS"
                                         + dot + "crest " + mbs::num (o.levels.crestDb, 1) + " dB"
                                         + (std::abs (o.levels.dcOffset) > 1.0e-4 ? dot + "DC offset " + mbs::num (o.levels.dcOffset, 5) : juce::String())
                                     : "No capture";

        waveform.setBuffer (o.captured, o.sampleRate);
        refreshTraces();
        repaint();
    }

    void refreshTraces()
    {
        if (outcome == nullptr)
            return;

        const auto& o = *outcome;
        const auto unused = o.instrument ? juce::String ("Not used for instruments: they are played, not fed audio") : juce::String();

        if (o.response.ok && ! o.instrument)
        {
            std::vector<GraphComponent::Trace> traces;
            const auto fraction = smoothing.getSelectedId() == 2 ? 1.0 / 12.0 : smoothing.getSelectedId() == 3 ? 1.0 / 6.0
                                : smoothing.getSelectedId() == 4 ? 1.0 / 3.0 : 0.0;

            if (fraction > 0.0)
            {
                const auto smooth = vibecheck::smoothResponse (o.response, fraction);
                traces.push_back ({ o.response.frequencyHz, o.response.magnitudeDb, 1.0f, "", mbs::theme().accent.withAlpha (0.28f), false, false, 0 });
                traces.push_back ({ smooth.frequencyHz, smooth.magnitudeDb, 1.0f, "Magnitude", juce::Colours::transparentBlack, false, true, 0 });
            }
            else
            {
                traces.push_back ({ o.response.frequencyHz, o.response.magnitudeDb, 1.0f, "Magnitude", juce::Colours::transparentBlack, false, true, 0 });
            }

            magnitude.setTraces (std::move (traces));
            phase.setTraces ({ { o.response.frequencyHz, o.response.phaseDegrees, 1.0f, "Phase, unwrapped", juce::Colours::transparentBlack, false, false, 1 } });
        }
        else
        {
            // Never leave the last plugin's curve on screen under a new plugin's name.
            magnitude.clearTraces();
            phase.clearTraces();

            if (unused.isNotEmpty())
            {
                magnitude.setPlaceholder (unused);
                phase.setPlaceholder (unused);
            }
        }

        std::vector<GraphComponent::Trace> spectrumTraces;

        if (o.harmonics.ok)
            spectrumTraces.push_back ({ o.harmonics.spectrum.frequencyHz, o.harmonics.spectrum.magnitudeDb, 1.0f, o.instrument ? "Held note" : "Harmonics", juce::Colours::transparentBlack, false, true, 0 });

        if (o.imd.ok && ! o.instrument)
            spectrumTraces.push_back ({ o.imd.spectrum.frequencyHz, o.imd.spectrum.magnitudeDb, 1.0f, "Intermodulation", juce::Colours::transparentBlack, true, false, 1 });

        if (spectrumTraces.empty())
            spectrum.clearTraces();
        else
            spectrum.setTraces (std::move (spectrumTraces));

        if (o.transfer.ok && ! o.instrument)
            transfer.setTraces ({ { o.transfer.risingInputDb, o.transfer.risingOutputDb, 1.0f, "Getting louder", juce::Colours::transparentBlack, false, false, 0 },
                                  { o.transfer.fallingInputDb, o.transfer.fallingOutputDb, 1.0f, "Getting quieter", juce::Colours::transparentBlack, true, false, 1 } });
        else
        {
            transfer.clearTraces();

            if (unused.isNotEmpty())
                transfer.setPlaceholder (unused);
        }

        if (o.sweepsRan && ! o.thdVsLevelDb.empty())
            thdLevel.setTraces ({ { o.sweepLevelDb, o.thdVsLevelDb, 1.0f, "THD", juce::Colours::transparentBlack, false, true, 0 } });
        else
            thdLevel.clearTraces();

        if (o.sweepsRan && ! o.thdVsFrequencyDb.empty())
            thdFrequency.setTraces ({ { o.sweepFrequencyHz, o.thdVsFrequencyDb, 1.0f, "THD at -6 dBFS", juce::Colours::transparentBlack, false, true, 1 } });
        else
            thdFrequency.clearTraces();
    }

    int heightFor (int viewportHeight) const
    {
        const auto fixed = tilesHeight + readingHeight + waveformHeight + 4 * mbs::gutter;
        return juce::jmax (viewportHeight, fixed + 3 * minGraphHeight + 2 * mbs::gutter - 16);
    }

    void paint (juce::Graphics& g) override
    {
        mbs::drawTitledCard (g, magnitudeCard, "Magnitude response", "hover for values");
        mbs::drawTitledCard (g, phaseCard, "Phase response");
        mbs::drawTitledCard (g, spectrumCard, "Distortion spectrum", "dBFS");
        mbs::drawTitledCard (g, transferCard, "Transfer curve", "level in against level out");
        mbs::drawTitledCard (g, thdLevelCard, "Distortion against level", "THD in dB, 1 kHz tone");
        mbs::drawTitledCard (g, thdFrequencyCard, "Distortion against frequency", "THD in dB, at -6 dBFS");
        mbs::drawTitledCard (g, waveformCard, "Captured output");

        auto inner = mbs::drawTitledCard (g, readingCard, "Reading", "what the measurements say");

        for (const auto& [caption, text] : rows)
        {
            auto row = inner.removeFromTop (rowHeight);
            g.setColour (mbs::theme().inkFaint);
            g.setFont (mbs::brandFont (10.5f, true));
            mbs::drawTracked (g, caption, row.removeFromLeft (92), 1.2f, juce::Justification::centredLeft);

            g.setColour (text.startsWith ("Not measured") || text.startsWith ("No ") ? mbs::theme().inkFaint : mbs::theme().ink);
            g.setFont (mbs::brandFont (13.0f));
            g.drawText (text, row, juce::Justification::centredLeft, true);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();

        auto tiles = area.removeFromTop (tilesHeight);
        const auto tileWidth = (tiles.getWidth() - 4 * mbs::gutter) / 5;

        for (auto* tile : { &thd, &imd, &gain, &latency, &noise })
        {
            tile->setBounds (tiles.removeFromLeft (tileWidth));
            tiles.removeFromLeft (mbs::gutter);
        }

        area.removeFromTop (mbs::gutter);
        readingCard = area.removeFromTop (readingHeight);
        area.removeFromTop (mbs::gutter);

        waveformCard = area.removeFromBottom (waveformHeight);
        area.removeFromBottom (mbs::gutter);

        const auto graphRowHeight = (area.getHeight() - 2 * mbs::gutter) / 3;
        auto top = area.removeFromTop (graphRowHeight);
        area.removeFromTop (mbs::gutter);
        auto middle = area.removeFromTop (graphRowHeight);
        area.removeFromTop (mbs::gutter);

        const auto half = (top.getWidth() - mbs::gutter) / 2;
        magnitudeCard = top.removeFromLeft (half);
        top.removeFromLeft (mbs::gutter);
        phaseCard = top;

        spectrumCard = middle.removeFromLeft (half);
        middle.removeFromLeft (mbs::gutter);
        transferCard = middle;

        thdLevelCard = area.removeFromLeft (half);
        area.removeFromLeft (mbs::gutter);
        thdFrequencyCard = area;

        const auto inside = [] (juce::Rectangle<int> card) { return card.withTrimmedTop (38).reduced (10, 8); };

        magnitude.setBounds (inside (magnitudeCard));
        phase.setBounds (inside (phaseCard));
        spectrum.setBounds (inside (spectrumCard));
        transfer.setBounds (inside (transferCard));
        thdLevel.setBounds (inside (thdLevelCard));
        thdFrequency.setBounds (inside (thdFrequencyCard));
        waveform.setBounds (inside (waveformCard));

        smoothing.setBounds (juce::Rectangle<int> (118, 26).withRightX (magnitudeCard.getRight() - 14)
                                                           .withY (magnitudeCard.getY() + 10));
    }

private:
    static constexpr int tilesHeight = 98, readingHeight = 218, waveformHeight = 150, minGraphHeight = 230, rowHeight = 27;

    mbs::StatTile thd { "THD" }, imd { "IMD" }, gain { "Gain at 1 kHz" }, latency { "Latency" }, noise { "Self-noise" };
    GraphComponent magnitude, phase, spectrum, transfer, thdLevel, thdFrequency;
    WaveformComponent waveform;
    juce::ComboBox smoothing;

    std::vector<std::pair<juce::String, juce::String>> rows { { "Response", {} }, { "Distortion", {} }, { "Dynamics", {} },
                                                               { "Aliasing", {} }, { "Tail", {} }, { "Output level", {} } };

    juce::Rectangle<int> magnitudeCard, phaseCard, spectrumCard, transferCard, thdLevelCard, thdFrequencyCard, waveformCard, readingCard;
    std::shared_ptr<const Outcome> outcome;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Results)
};

// --- The page -----------------------------------------------------------------------------------

AnalysisTab::AnalysisTab (PluginScanner& scanner)
    : pluginScanner (scanner), picker (scanner)
{
    addAndMakeVisible (picker);

    loadButton.onClick = [this] { loadSelectedPlugin(); };
    mbs::setButtonStyle (loadButton, mbs::ButtonStyle::primary);
    addAndMakeVisible (loadButton);

    editorButton.onClick = [this] { openEditor(); };
    addAndMakeVisible (editorButton);

    reportButton.onClick = [this] { copyReport(); };
    mbs::setButtonStyle (reportButton, mbs::ButtonStyle::ghost);
    addAndMakeVisible (reportButton);

    parametersButton.onClick = [this] { openParameters(); };
    addAndMakeVisible (parametersButton);

    imageButton.onClick = [this] { saveImage(); };
    mbs::setButtonStyle (imageButton, mbs::ButtonStyle::ghost);
    addAndMakeVisible (imageButton);

    loadedLabel.setFont (mbs::brandFont (12.5f));
    loadedLabel.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (loadedLabel);

    mbs::styleCaption (signalCaption, "Waveform shows");
    mbs::styleCaption (frequencyCaption, "Test tone");
    mbs::styleCaption (bufferCaption, "Buffer size");
    mbs::styleCaption (rateCaption, "Sample rate");

    for (auto* label : { &signalCaption, &frequencyCaption, &bufferCaption, &rateCaption })
        addAndMakeVisible (label);

    addAndMakeVisible (signalChooser);

    for (const auto type : { vibecheck::SignalType::sine, vibecheck::SignalType::dualSine, vibecheck::SignalType::impulse,
                             vibecheck::SignalType::ramp, vibecheck::SignalType::whiteNoise, vibecheck::SignalType::silence })
        signalChooser.addItem (vibecheck::toString (type), (int) type + 1);

    signalChooser.setSelectedId ((int) vibecheck::SignalType::sine + 1, juce::dontSendNotification);
    signalChooser.onChange = [this] { updateControls(); requestRender(); };

    addAndMakeVisible (frequencySlider);
    frequencySlider.setRange (20.0, 20000.0, 1.0);
    frequencySlider.setSkewFactorFromMidPoint (1000.0);
    frequencySlider.setValue (1000.0, juce::dontSendNotification);
    frequencySlider.setTextValueSuffix (" Hz");
    frequencySlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 78, 26);
    frequencySlider.onValueChange = [this] { requestRender(); };

    addAndMakeVisible (bufferSizeChooser);

    for (int size : { 32, 64, 128, 256, 512, 1024, 2048 })
        bufferSizeChooser.addItem (juce::String (size), size);

    bufferSizeChooser.setSelectedId (512, juce::dontSendNotification);
    bufferSizeChooser.onChange = [this] { requestRender(); };

    addAndMakeVisible (sampleRateChooser);

    for (int rate : { 44100, 48000, 88200, 96000, 192000 })
        sampleRateChooser.addItem (mbs::num (rate / 1000.0, 1) + " kHz", rate);

    sampleRateChooser.setSelectedId (48000, juce::dontSendNotification);
    sampleRateChooser.onChange = [this] { requestRender(); };

    renderButton.onClick = [this] { renderCurrentSignal(); };
    mbs::setButtonStyle (renderButton, mbs::ButtonStyle::primary);
    addAndMakeVisible (renderButton);

    liveToggle.setToggleState (true, juce::dontSendNotification);
    addAndMakeVisible (liveToggle);
    addAndMakeVisible (sweepToggle);

    statusLabel.setFont (mbs::monoFont (11.0f));
    statusLabel.setColour (juce::Label::textColourId, mbs::theme().inkFaint);
    statusLabel.setText ("Load a plugin to begin", juce::dontSendNotification);
    addAndMakeVisible (statusLabel);
    addAndMakeVisible (busyBar);

    log.setText ("Measurements run offline: no audio plays, and the same input always gives the same output.\n");
    addAndMakeVisible (log);

    results = std::make_unique<Results>();
    resultsViewport.setViewedComponent (results.get(), false);
    resultsViewport.setScrollBarsShown (true, false);
    resultsViewport.setScrollBarThickness (8);
    addAndMakeVisible (resultsViewport);

    picker.onSelectionChanged = [this] { updateControls(); };

    restyle();
    updateControls();
}

AnalysisTab::~AnalysisTab()
{
    stopTimer();
    pool.removeAllJobs (true, 20000);
    parameterWindow.reset();
    editorWindow.reset();
    plugin.reset();
}

void AnalysisTab::restyle()
{
    mbs::styleConsole (log);
    loadedLabel.setColour (juce::Label::textColourId, mbs::theme().inkMuted);
    statusLabel.setColour (juce::Label::textColourId, mbs::theme().inkFaint);

    for (auto* label : { &signalCaption, &frequencyCaption, &bufferCaption, &rateCaption })
        label->setColour (juce::Label::textColourId, mbs::theme().inkFaint);
}

void AnalysisTab::lookAndFeelChanged()
{
    restyle();

    if (results != nullptr && lastOutcome != nullptr)
        results->refreshTraces();
}

void AnalysisTab::updateControls()
{
    juce::PluginDescription chosen;
    loadButton.setEnabled (! busy && picker.getSelected (chosen));
    renderButton.setEnabled (! busy && plugin != nullptr);
    editorButton.setEnabled (! busy && plugin != nullptr);
    parametersButton.setEnabled (! busy && plugin != nullptr);
    reportButton.setEnabled (lastOutcome != nullptr);
    imageButton.setEnabled (lastOutcome != nullptr);
    picker.setEnabled (! busy);
    frequencySlider.setEnabled (signalChooser.getSelectedId() == (int) vibecheck::SignalType::sine + 1);
    frequencyCaption.setAlpha (frequencySlider.isEnabled() ? 1.0f : 0.45f);
}

vibecheck::SignalSpec AnalysisTab::currentSpec() const
{
    vibecheck::SignalSpec spec;
    spec.type = (vibecheck::SignalType) juce::jmax (0, signalChooser.getSelectedId() - 1);
    const double sampleRate = sampleRateChooser.getSelectedId() > 0 ? (double) sampleRateChooser.getSelectedId() : 48000.0;
    spec.sampleRate = sampleRate;
    spec.numSamples = (int) sampleRate * 2;
    spec.frequencyHz = frequencySlider.getValue();

    if (spec.type == vibecheck::SignalType::dualSine)
    {
        spec.frequencyHz = 60.0;
        spec.secondFrequencyHz = 7000.0;
    }

    if (spec.type == vibecheck::SignalType::impulse)
    {
        spec.amplitudeDb = 0.0;
        spec.numSamples = 16384;
    }

    if (spec.type == vibecheck::SignalType::ramp)
    {
        // Sweep all the way to full scale, otherwise a compressor's threshold is never reached
        // and the curve is a straight line that says nothing.
        spec.amplitudeDb = 0.0;
        spec.numSamples = (int) spec.sampleRate * 4;
    }

    return spec;
}

void AnalysisTab::setBusy (bool shouldBeBusy)
{
    busy = shouldBeBusy;
    busyBar.setActive (busy);
    updateControls();
}

void AnalysisTab::requestRender()
{
    if (plugin == nullptr || ! liveToggle.getToggleState())
        return;

    // Wait for the user to stop dragging before spending several seconds rendering.
    startTimer (250);
}

void AnalysisTab::timerCallback()
{
    stopTimer();
    renderCurrentSignal();
}

void AnalysisTab::appendLine (const juce::String& line)
{
    log.moveCaretToEnd();
    log.insertTextAtCaret (line + "\n");
}

void AnalysisTab::loadSelectedPlugin()
{
    juce::PluginDescription description;

    if (picker.getSelected (description))
        loadPlugin (description);
}

void AnalysisTab::loadPlugin (const juce::PluginDescription& description)
{
    stopTimer();
    pool.removeAllJobs (true, 5000);
    parameterWindow.reset();
    editorWindow.reset();
    plugin.reset();
    lastOutcome.reset();
    results->clear();
    pluginName = description.name;
    setBusy (true);

    loadedLabel.setText ("Loading " + description.name + "...", juce::dontSendNotification);
    statusLabel.setText ("Loading", juce::dontSendNotification);
    appendLine ("\nLoading " + description.name + " [" + description.pluginFormatName + "]");

    const auto blockSize = bufferSizeChooser.getSelectedId() > 0 ? bufferSizeChooser.getSelectedId() : 512;
    const auto sampleRate = sampleRateChooser.getSelectedId() > 0 ? (double) sampleRateChooser.getSelectedId() : 48000.0;

    pool.addJob ([this, description, blockSize, sampleRate, safe = juce::Component::SafePointer<AnalysisTab> (this)]
    {
        auto loaded = vibecheck::loadPlugin (pluginScanner.getFormatManager(), description, sampleRate, blockSize);
        auto* released = loaded.instance.release();
        const auto error = loaded.error;

        juce::MessageManager::callAsync ([safe, released, error, description]
        {
            std::unique_ptr<juce::AudioPluginInstance> owned (released);

            if (safe == nullptr)
                return;

            if (owned == nullptr)
            {
                safe->appendLine ("Failed: " + error);
                safe->loadedLabel.setText ("Could not load " + description.name, juce::dontSendNotification);
                safe->statusLabel.setText ("Load failed", juce::dontSendNotification);
            }
            else
            {
                safe->plugin = std::move (owned);
                const auto details = juce::String (safe->plugin->getTotalNumInputChannels()) + " in, "
                                     + juce::String (safe->plugin->getTotalNumOutputChannels()) + " out" + dot
                                     + juce::String (safe->plugin->getParameters().size()) + " parameters"
                                     + (safe->plugin->hasEditor() ? dot + "has its own UI" : dot + "no custom UI");
                safe->appendLine ("Loaded " + description.name + dot + details);
                safe->loadedLabel.setText (description.name + "\n" + details, juce::dontSendNotification);
                safe->statusLabel.setText ("Ready", juce::dontSendNotification);
            }

            safe->setBusy (false);

            if (auto next = std::exchange (safe->afterLoad, nullptr))
                next();
            else if (safe->plugin != nullptr && safe->liveToggle.getToggleState())
                safe->renderCurrentSignal();
        });
    });
}

void AnalysisTab::runDemo (const juce::String& pluginQuery, vibecheck::SignalType signalType)
{
    juce::PluginDescription description;

    if (! picker.selectByName (pluginQuery, description))
    {
        statusLabel.setText ("No plugin matching \"" + pluginQuery + "\"", juce::dontSendNotification);
        return;
    }

    signalChooser.setSelectedId ((int) signalType + 1, juce::dontSendNotification);
    updateControls();

    afterLoad = [this]
    {
        if (plugin != nullptr)
            renderCurrentSignal();
    };

    loadPlugin (description);
}

void AnalysisTab::renderCurrentSignal()
{
    if (plugin == nullptr)
        return;

    if (busy)
    {
        renderPending = true;
        return;
    }

    const auto spec = currentSpec();
    const auto blockSize = bufferSizeChooser.getSelectedId() > 0 ? bufferSizeChooser.getSelectedId() : 512;
    const auto name = pluginName;

    setBusy (true);
    statusLabel.setText ("Measuring", juce::dontSendNotification);
    appendLine ("\nRunning measurements" + dot + vibecheck::describe (spec) + dot + "buffer " + juce::String (blockSize));

    auto* instance = plugin.get();

    const auto sweeps = sweepToggle.getToggleState();

    pool.addJob ([instance, spec, blockSize, name, sweeps, safe = juce::Component::SafePointer<AnalysisTab> (this)]
    {
        const auto progress = [safe] (const juce::String& text)
        {
            juce::MessageManager::callAsync ([safe, text]
            {
                if (safe != nullptr)
                    safe->statusLabel.setText (text, juce::dontSendNotification);
            });
        };

        const auto rate = spec.sampleRate;

        vibecheck::SuiteOptions suiteOptions;
        suiteOptions.sampleRate = rate;
        suiteOptions.blockSize = blockSize;
        suiteOptions.toneHz = spec.frequencyHz;
        suiteOptions.sweeps = sweeps;

        auto suite = vibecheck::runSuite (*instance, suiteOptions, progress);

        auto outcome = std::make_shared<Outcome>();
        outcome->shown = spec.type;
        outcome->sampleRate = rate;
        outcome->response = suite.response;
        outcome->responseSummary = suite.responseSummary;
        outcome->harmonics = suite.harmonics;
        outcome->imd = suite.imd;
        outcome->transfer = suite.transfer;
        outcome->transferAnalysis = suite.transferAnalysis;
        outcome->tail = suite.tail;
        outcome->noise = suite.noise;
        outcome->latencySamples = suite.latencySamples;
        outcome->renderSeconds = suite.seconds;
        outcome->anyOk = suite.anyOk;
        outcome->alias = suite.alias;
        outcome->instrument = suite.instrument;
        outcome->noteHz = suite.noteHz;
        outcome->noteOnsetMs = suite.noteOnsetMs;
        outcome->noteReleaseMs = suite.noteReleaseMs;
        outcome->sweepsRan = suite.sweepsRan;
        outcome->sweepLevelDb = suite.sweepLevelDb;
        outcome->thdVsLevelDb = suite.thdVsLevelDb;
        outcome->sweepFrequencyHz = suite.sweepFrequencyHz;
        outcome->thdVsFrequencyDb = suite.thdVsFrequencyDb;

        // The capture on show is one of the suite's own renders; only white noise needs one more.
        juce::AudioBuffer<float> primary;

        if (suite.instrument)
            primary = suite.noteOut;
        else switch (spec.type)
        {
            case vibecheck::SignalType::impulse:    primary = suite.impulseOut; break;
            case vibecheck::SignalType::sine:       primary = suite.sineOut; break;
            case vibecheck::SignalType::dualSine:   primary = suite.dualOut; break;
            case vibecheck::SignalType::ramp:       primary = suite.rampOut; break;
            case vibecheck::SignalType::silence:    primary = suite.silenceOut; break;
            case vibecheck::SignalType::whiteNoise:
            {
                vibecheck::RenderOptions options;
                options.sampleRate = rate;
                options.blockSize = blockSize;
                auto noiseIn = vibecheck::generate (spec);
                auto noiseRender = vibecheck::OfflineRenderer::render (*instance, noiseIn, options);

                if (noiseRender.ok)
                    primary = std::move (noiseRender.output);

                break;
            }
        }

        if (primary.getNumSamples() > 0)
        {
            outcome->captured = primary;
            outcome->levels = vibecheck::measureLevels (primary, 0);
            outcome->console << "Captured " << primary.getNumSamples() << " samples, latency " << suite.describeLatency()
                             << ", " << mbs::num (suite.speedTimesRealtime, 1) << "x realtime";
        }

        if (outcome->harmonics.ok)   outcome->console << "\n\n" << outcome->harmonics.summary();
        if (outcome->imd.ok)         outcome->console << "\n\n" << outcome->imd.summary();
        if (outcome->transfer.ok)    outcome->console << "\n\n" << outcome->transfer.summary();

        outcome->status = outcome->anyOk ? "Done in " + mbs::num (outcome->renderSeconds, 1) + " s"
                                         : "Render failed: " + suite.error;

        // A report that can be pasted into a message or an issue.
        juce::String report;
        report << "VibeCheck measurement report\n" << name << "\n"
               << mbs::num (rate / 1000.0, 1) << " kHz, buffer " << blockSize << "\n\n";

        if (outcome->responseSummary.ok)
            report << "Response: " << outcome->responseSummary.description << ", gain at 1 kHz "
                   << mbs::num (outcome->responseSummary.gainAt1kDb, 2) << " dB, group delay "
                   << mbs::num (outcome->responseSummary.groupDelayMs, 2) << " ms\n";

        if (outcome->harmonics.ok)
            report << "THD " << mbs::num (outcome->harmonics.thdPercent, 4) << " %, THD+N "
                   << mbs::num (outcome->harmonics.thdPlusNPercent, 4) << " % at " << mbs::num (spec.frequencyHz, 0)
                   << " Hz. " << outcome->harmonics.character << "\n";

        if (outcome->imd.ok)
            report << "SMPTE IMD " << mbs::num (outcome->imd.imdPercent, 4) << " %\n";

        if (outcome->transferAnalysis.ok)
            report << "Dynamics: " << outcome->transferAnalysis.description << "\n";

        if (outcome->tail.ok)
            report << "Tail: " << mbs::num (outcome->tail.tailMs, 0) << " ms" << (outcome->tail.truncated ? " or more" : "") << "\n";

        if (outcome->alias.ok)
            report << "Aliasing: " << outcome->alias.description << " (" << mbs::num (outcome->alias.worstDbc, 0) << " dBc)\n";

        if (outcome->instrument)
            report << "Instrument: note at " << mbs::num (outcome->noteHz, 1) << " Hz, onset " << mbs::num (outcome->noteOnsetMs, 1)
                   << " ms, rings " << mbs::num (outcome->noteReleaseMs, 0) << " ms after release\n";

        report << "Latency: " << outcome->latencySamples << " samples\n";

        if (outcome->noise.ok)
            report << "Self-noise: " << mbs::num (outcome->noise.rmsDb, 1) << " dBFS RMS\n";

        outcome->report = report;

        juce::MessageManager::callAsync ([safe, outcome]
        {
            if (safe == nullptr)
                return;

            safe->setBusy (false);
            safe->showOutcome (*outcome);
            safe->lastOutcome = outcome;
            safe->results->setOutcome (outcome);
            safe->updateControls();

            if (std::exchange (safe->renderPending, false))
                safe->renderCurrentSignal();
        });
    });
}

void AnalysisTab::showOutcome (const Outcome& outcome)
{
    appendLine (outcome.console);
    statusLabel.setText (outcome.status, juce::dontSendNotification);
}

void AnalysisTab::copyReport()
{
    if (lastOutcome == nullptr)
        return;

    juce::SystemClipboard::copyTextToClipboard (lastOutcome->report);
    statusLabel.setText ("Report copied to the clipboard", juce::dontSendNotification);
}

void AnalysisTab::openParameters()
{
    if (plugin == nullptr)
        return;

    if (parameterWindow != nullptr)
    {
        parameterWindow->toFront (true);
        return;
    }

    parameterWindow = std::make_unique<ParameterWindow> (*plugin, [this] { requestRender(); }, [this] { parameterWindow.reset(); });
}

void AnalysisTab::openParametersAndSnapshot (const juce::File& destination, std::function<void()> whenDone)
{
    openParameters();

    juce::Timer::callAfterDelay (900, [safe = juce::Component::SafePointer<AnalysisTab> (this), destination, finished = std::move (whenDone)]
    {
        if (safe != nullptr && safe->parameterWindow != nullptr)
        {
            const auto image = safe->parameterWindow->getContentComponent()->createComponentSnapshot (
                safe->parameterWindow->getContentComponent()->getLocalBounds(), true, 2.0f);
            destination.deleteFile();

            if (juce::FileOutputStream stream (destination); stream.openedOk())
            {
                juce::PNGImageFormat png;
                png.writeImageToStream (image, stream);
            }
        }

        if (finished != nullptr)
            finished();
    });
}

void AnalysisTab::saveImage()
{
    if (lastOutcome == nullptr)
        return;

    const auto image = results->createComponentSnapshot (results->getLocalBounds(), true, 2.0f);
    const auto suggested = juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                               .getChildFile (juce::File::createLegalFileName (pluginName + " analysis") + ".png");

    fileChooser = std::make_shared<juce::FileChooser> ("Save the analysis as an image", suggested, "*.png");
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = juce::Component::SafePointer<AnalysisTab> (this), image] (const juce::FileChooser& chooser)
                              {
                                  auto file = chooser.getResult();

                                  if (safe == nullptr || file == juce::File())
                                      return;

                                  file = file.withFileExtension ("png");
                                  file.deleteFile();

                                  juce::FileOutputStream stream (file);
                                  juce::PNGImageFormat png;

                                  if (stream.openedOk() && png.writeImageToStream (image, stream))
                                      safe->statusLabel.setText ("Saved " + file.getFileName(), juce::dontSendNotification);
                                  else
                                      safe->statusLabel.setText ("Could not save the image", juce::dontSendNotification);
                              });
}

void AnalysisTab::openEditor()
{
    if (plugin == nullptr)
        return;

    if (editorWindow != nullptr)
    {
        editorWindow->toFront (true);
        return;
    }

    editorWindow = std::make_unique<EditorWindow> (*plugin, [this] { editorWindow.reset(); });
}

void AnalysisTab::paint (juce::Graphics& g)
{
    mbs::drawTitledCard (g, pluginCard, "Plugin");
    mbs::drawTitledCard (g, signalCard, "Measurement");
    mbs::drawTitledCard (g, consoleCard, "Console");
}

void AnalysisTab::resized()
{
    auto area = getLocalBounds();
    auto column = area.removeFromLeft (320);
    area.removeFromLeft (mbs::gutter);

    // --- Plugin card -----------------------------------------------------------------------
    pluginCard = column.removeFromTop (262);
    {
        auto inner = pluginCard.reduced (18, 14).withTrimmedTop (26);
        picker.setBounds (inner.removeFromTop (PluginPicker::preferredHeight));
        inner.removeFromTop (10);

        auto buttons = inner.removeFromTop (36);
        loadButton.setBounds (buttons.removeFromLeft (buttons.getWidth() * 5 / 9));
        buttons.removeFromLeft (8);
        editorButton.setBounds (buttons);

        inner.removeFromTop (8);
        loadedLabel.setBounds (inner.removeFromTop (38));
        auto tools = inner.removeFromTop (30);
        const auto third = tools.getWidth() / 3;
        parametersButton.setBounds (tools.removeFromLeft (third).reduced (0, 0).withTrimmedRight (4));
        reportButton.setBounds (tools.removeFromLeft (third).withTrimmedRight (4));
        imageButton.setBounds (tools);
    }

    column.removeFromTop (mbs::gutter);

    // --- Measurement card ------------------------------------------------------------------
    signalCard = column.removeFromTop (350);
    {
        auto inner = signalCard.reduced (18, 14).withTrimmedTop (26);

        signalCaption.setBounds (inner.removeFromTop (16));
        signalChooser.setBounds (inner.removeFromTop (34));
        inner.removeFromTop (10);

        frequencyCaption.setBounds (inner.removeFromTop (16));
        frequencySlider.setBounds (inner.removeFromTop (30));
        inner.removeFromTop (8);

        auto captions = inner.removeFromTop (16);
        bufferCaption.setBounds (captions.removeFromLeft (captions.getWidth() / 2 - 4));
        captions.removeFromLeft (8);
        rateCaption.setBounds (captions);

        auto combos = inner.removeFromTop (34);
        bufferSizeChooser.setBounds (combos.removeFromLeft (combos.getWidth() / 2 - 4));
        combos.removeFromLeft (8);
        sampleRateChooser.setBounds (combos);

        inner.removeFromTop (14);
        renderButton.setBounds (inner.removeFromTop (40));
        inner.removeFromTop (8);
        busyBar.setBounds (inner.removeFromTop (4));
        inner.removeFromTop (6);
        statusLabel.setBounds (inner.removeFromTop (18));
        liveToggle.setBounds (inner.removeFromTop (26));
        sweepToggle.setBounds (inner.removeFromTop (26));
    }

    column.removeFromTop (mbs::gutter);

    // --- Console fills what is left --------------------------------------------------------
    consoleCard = column;
    log.setVisible (consoleCard.getHeight() > 90);
    log.setBounds (consoleCard.reduced (14, 0).withTrimmedTop (40).withTrimmedBottom (14));

    // --- Results ---------------------------------------------------------------------------
    resultsViewport.setBounds (area);

    const auto needed = results->heightFor (area.getHeight());
    const auto width = needed > area.getHeight() ? area.getWidth() - 12 : area.getWidth();
    results->setSize (width, needed);
}
