#include "ui/ComparisonTab.h"

#include "host/PluginLoader.h"
#include "ui/Theme.h"

namespace
{
const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));

/** One row of the scorecard. */
struct Row
{
    juce::String metric;
    juce::String a, b;
    juce::String difference;
    int better = 0;   ///< -1 A, +1 B, 0 neither.

    explicit Row (juce::String name) : metric (std::move (name)) {}
};

/** Says how two positive quantities compare, for metrics where lower is better. */
juce::String ratioText (double a, double b, bool lowerIsBetter, int& better)
{
    better = 0;

    if (a <= 0.0 && b <= 0.0)
        return "Both zero";

    const auto small = juce::jmin (a, b), large = juce::jmax (a, b);
    const auto ratio = small > 0.0 ? large / small : 1000.0;

    if (ratio < 1.08)
        return "About the same";

    const auto aIsLower = a < b;
    better = (aIsLower == lowerIsBetter) ? -1 : 1;

    // Name the side that is lower when lower is good, and the side that is higher otherwise.
    const auto statedIsA = lowerIsBetter ? aIsLower : ! aIsLower;

    return juce::String (statedIsA ? "A" : "B") + " is "
           + (ratio >= 100.0 ? juce::String (">100") : mbs::num (ratio, ratio < 10.0 ? 1 : 0))
           + "x " + (lowerIsBetter ? "lower" : "higher");
}

juce::String shortName (const juce::String& name)
{
    return name.length() > 22 ? name.substring (0, 21) + "..." : name;
}
} // namespace

class ComparisonTab::Results final : public juce::Component
{
public:
    Results()
    {
        for (auto* graph : { &magnitude, &phase, &spectrum, &transfer })
            addAndMakeVisible (graph);

        magnitude.setAxes ({ 20.0, 24000.0, true, "Hz", {}, false }, { -48.0, 12.0, false, {}, " dB", true });
        phase.setAxes     ({ 20.0, 24000.0, true, "Hz", {}, false }, { -180.0, 180.0, false, {}, juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")), true });
        spectrum.setAxes  ({ 20.0, 24000.0, true, "Hz", {}, false }, { -160.0, 0.0, false, {}, " dB", true });
        transfer.setAxes  ({ -72.0, 0.0, false, "dB in", " dB", false }, { -72.0, 0.0, false, {}, " dB", true });

        magnitude.setPlaceholder ("Load two plugins and press Compare");
        phase.setPlaceholder ("Phase of both plugins, overlaid");
        spectrum.setPlaceholder ("Harmonic content of both, overlaid");
        transfer.setPlaceholder ("Level in against level out for both");
    }

    void setResults (std::shared_ptr<vibecheck::SuiteResult> a, std::shared_ptr<vibecheck::SuiteResult> b,
                     const juce::String& nameA, const juce::String& nameB)
    {
        resultA = std::move (a);
        resultB = std::move (b);
        names = { nameA, nameB };
        rebuildRows();
        refreshTraces();
        repaint();
    }

    void refreshTraces()
    {
        if (resultA == nullptr || resultB == nullptr)
            return;

        const auto pair = [this] (auto&& pick, std::vector<GraphComponent::Trace>& out, const juce::String& suffix, bool filled)
        {
            const auto add = [&] (const vibecheck::SuiteResult& r, int index)
            {
                const auto trace = pick (r);

                if (trace.x.empty())
                    return;

                auto copy = trace;
                copy.name = juce::String (index == 0 ? "A" : "B") + suffix;
                copy.series = index;
                copy.dashed = index == 1;
                copy.filled = filled && index == 0;
                out.push_back (std::move (copy));
            };

            add (*resultA, 0);
            add (*resultB, 1);
        };

        std::vector<GraphComponent::Trace> traces;

        pair ([] (const vibecheck::SuiteResult& r) { GraphComponent::Trace t; if (r.response.ok) { t.x = r.response.frequencyHz; t.y = r.response.magnitudeDb; } return t; }, traces, "", true);
        magnitude.setTraces (std::move (traces));

        traces.clear();
        pair ([] (const vibecheck::SuiteResult& r) { GraphComponent::Trace t; if (r.response.ok) { t.x = r.response.frequencyHz; t.y = r.response.phaseDegrees; } return t; }, traces, "", false);
        phase.setTraces (std::move (traces));

        traces.clear();
        pair ([] (const vibecheck::SuiteResult& r) { GraphComponent::Trace t; if (r.harmonics.ok) { t.x = r.harmonics.spectrum.frequencyHz; t.y = r.harmonics.spectrum.magnitudeDb; } return t; }, traces, " harmonics", true);
        spectrum.setTraces (std::move (traces));

        traces.clear();
        pair ([] (const vibecheck::SuiteResult& r) { GraphComponent::Trace t; if (r.transfer.ok) { t.x = r.transfer.risingInputDb; t.y = r.transfer.risingOutputDb; } return t; }, traces, "", false);
        transfer.setTraces (std::move (traces));
    }

    int heightFor (int viewportHeight) const
    {
        return juce::jmax (viewportHeight, scoreHeight + 2 * minGraphHeight + 2 * mbs::gutter);
    }

    void paint (juce::Graphics& g) override
    {
        mbs::drawTitledCard (g, magnitudeCard, "Magnitude response", "hover for values");
        mbs::drawTitledCard (g, phaseCard, "Phase response");
        mbs::drawTitledCard (g, spectrumCard, "Distortion spectrum", "dBFS");
        mbs::drawTitledCard (g, transferCard, "Transfer curve");

        auto inner = mbs::drawTitledCard (g, scoreCard, "Scorecard", "the better value is highlighted");
        const auto& p = mbs::theme();

        const auto metricW = 150, valueW = juce::jmax (110, (inner.getWidth() - metricW) / 3);

        auto header = inner.removeFromTop (28);
        g.setFont (mbs::brandFont (10.5f, true));
        g.setColour (p.inkFaint);
        mbs::drawTracked (g, "Metric", header.removeFromLeft (metricW), 1.2f, juce::Justification::centredLeft);

        for (int i = 0; i < 2; ++i)
        {
            auto cell = header.removeFromLeft (valueW);
            g.setColour (i == 0 ? p.accent : p.accent2);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ (float) cell.getX() + 4.0f, (float) cell.getCentreY() }));
            g.setColour (p.inkFaint);
            mbs::drawTracked (g, juce::String (i == 0 ? "A  " : "B  ") + shortName (names[(size_t) i]), cell.withTrimmedLeft (16), 1.0f, juce::Justification::centredLeft);
        }

        mbs::drawTracked (g, "Difference", header, 1.2f, juce::Justification::centredLeft);

        g.setColour (p.line);
        g.fillRect (inner.getX(), inner.getY(), inner.getWidth(), 1);

        if (rows.empty())
        {
            mbs::drawEmptyState (g, inner, mbs::Icon::compare, "Nothing to compare yet",
                                 "Pick a plugin for A and for B, load them, then press Compare.");
            return;
        }

        for (const auto& row : rows)
        {
            auto line = inner.removeFromTop (rowHeight);
            g.setColour (p.line.withAlpha (0.5f));
            g.fillRect (line.getX(), line.getBottom() - 1, line.getWidth(), 1);

            g.setColour (p.inkMuted);
            g.setFont (mbs::brandFont (13.0f));
            g.drawText (row.metric, line.removeFromLeft (metricW), juce::Justification::centredLeft, true);

            const auto value = [&] (const juce::String& text, bool better, juce::Colour accent)
            {
                auto cell = line.removeFromLeft (valueW).withTrimmedLeft (16);
                g.setColour (better ? p.good : p.ink);
                g.setFont (mbs::brandFont (13.5f, better));
                g.drawText (text, cell, juce::Justification::centredLeft, true);
                juce::ignoreUnused (accent);
            };

            value (row.a, row.better < 0, p.accent);
            value (row.b, row.better > 0, p.accent2);

            g.setColour (p.inkFaint);
            g.setFont (mbs::brandFont (12.5f));
            g.drawText (row.difference, line, juce::Justification::centredLeft, true);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        scoreCard = area.removeFromTop (scoreHeight);
        area.removeFromTop (mbs::gutter);

        auto top = area.removeFromTop ((area.getHeight() - mbs::gutter) / 2);
        area.removeFromTop (mbs::gutter);

        const auto half = (top.getWidth() - mbs::gutter) / 2;
        magnitudeCard = top.removeFromLeft (half);
        top.removeFromLeft (mbs::gutter);
        phaseCard = top;
        spectrumCard = area.removeFromLeft (half);
        area.removeFromLeft (mbs::gutter);
        transferCard = area;

        const auto inside = [] (juce::Rectangle<int> card) { return card.withTrimmedTop (38).reduced (10, 8); };
        magnitude.setBounds (inside (magnitudeCard));
        phase.setBounds (inside (phaseCard));
        spectrum.setBounds (inside (spectrumCard));
        transfer.setBounds (inside (transferCard));
    }

private:
    void rebuildRows()
    {
        rows.clear();

        if (resultA == nullptr || resultB == nullptr)
            return;

        const auto& a = *resultA;
        const auto& b = *resultB;

        {
            Row r { "THD at 1 kHz" };
            r.a = a.harmonics.ok ? mbs::percent (a.harmonics.thdPercent) : "-";
            r.b = b.harmonics.ok ? mbs::percent (b.harmonics.thdPercent) : "-";

            if (a.harmonics.ok && b.harmonics.ok)
                r.difference = ratioText (a.harmonics.thdPercent, b.harmonics.thdPercent, true, r.better);

            rows.push_back (r);
        }
        {
            Row r { "Intermodulation" };
            r.a = a.imd.ok ? mbs::percent (a.imd.imdPercent) : "-";
            r.b = b.imd.ok ? mbs::percent (b.imd.imdPercent) : "-";

            if (a.imd.ok && b.imd.ok)
                r.difference = ratioText (a.imd.imdPercent, b.imd.imdPercent, true, r.better);

            rows.push_back (r);
        }
        {
            Row r { "Gain at 1 kHz" };
            r.a = a.responseSummary.ok ? mbs::num (a.responseSummary.gainAt1kDb, 2) + " dB" : "-";
            r.b = b.responseSummary.ok ? mbs::num (b.responseSummary.gainAt1kDb, 2) + " dB" : "-";

            if (a.responseSummary.ok && b.responseSummary.ok)
                r.difference = "B is " + mbs::num (std::abs (b.responseSummary.gainAt1kDb - a.responseSummary.gainAt1kDb), 2) + " dB "
                               + (b.responseSummary.gainAt1kDb >= a.responseSummary.gainAt1kDb ? "louder" : "quieter");

            rows.push_back (r);
        }
        {
            Row r { "Tone" };
            r.a = a.responseSummary.ok ? a.responseSummary.description.upToFirstOccurrenceOf (";", false, false) : "-";
            r.b = b.responseSummary.ok ? b.responseSummary.description.upToFirstOccurrenceOf (";", false, false) : "-";
            rows.push_back (r);
        }
        {
            Row r { "Harmonic character" };
            r.a = a.harmonics.ok ? a.harmonics.character.upToFirstOccurrenceOf (":", false, false) : "-";
            r.b = b.harmonics.ok ? b.harmonics.character.upToFirstOccurrenceOf (":", false, false) : "-";
            rows.push_back (r);
        }
        {
            Row r { "Latency" };
            r.a = mbs::num (1000.0 * a.latencySamples / juce::jmax (1.0, a.sampleRate), 2) + " ms";
            r.b = mbs::num (1000.0 * b.latencySamples / juce::jmax (1.0, b.sampleRate), 2) + " ms";
            r.difference = ratioText ((double) a.latencySamples, (double) b.latencySamples, true, r.better);

            if (a.latencySamples == 0 && b.latencySamples == 0)
                r.difference = "Both zero-latency";

            rows.push_back (r);
        }
        {
            Row r { "Speed" };
            r.a = a.speedTimesRealtime > 0.0 ? mbs::num (a.speedTimesRealtime, 0) + "x realtime" : "-";
            r.b = b.speedTimesRealtime > 0.0 ? mbs::num (b.speedTimesRealtime, 0) + "x realtime" : "-";

            if (a.speedTimesRealtime > 0.0 && b.speedTimesRealtime > 0.0)
                r.difference = ratioText (a.speedTimesRealtime, b.speedTimesRealtime, false, r.better);

            rows.push_back (r);
        }
        {
            Row r { "Self-noise" };
            r.a = a.noise.ok ? (a.noise.rmsDb <= -150.0 ? "Silent" : mbs::num (a.noise.rmsDb, 1) + " dBFS") : "-";
            r.b = b.noise.ok ? (b.noise.rmsDb <= -150.0 ? "Silent" : mbs::num (b.noise.rmsDb, 1) + " dBFS") : "-";

            if (a.noise.ok && b.noise.ok)
            {
                const auto delta = b.noise.rmsDb - a.noise.rmsDb;
                r.better = std::abs (delta) < 1.0 ? 0 : (delta > 0 ? -1 : 1);
                r.difference = std::abs (delta) < 1.0 ? "About the same" : juce::String (delta > 0 ? "A" : "B") + " is " + mbs::num (std::abs (delta), 0) + " dB quieter";
            }

            rows.push_back (r);
        }
        {
            Row r { "Dynamics" };
            r.a = a.transferAnalysis.ok ? (a.transferAnalysis.engages ? "Engages near " + mbs::num (a.transferAnalysis.thresholdDb, 0) + " dB" : "Linear") : "-";
            r.b = b.transferAnalysis.ok ? (b.transferAnalysis.engages ? "Engages near " + mbs::num (b.transferAnalysis.thresholdDb, 0) + " dB" : "Linear") : "-";
            rows.push_back (r);
        }
    }

    static constexpr int scoreHeight = 40 + 28 + 9 * 31 + 20, minGraphHeight = 230, rowHeight = 31;

    GraphComponent magnitude, phase, spectrum, transfer;
    juce::Rectangle<int> magnitudeCard, phaseCard, spectrumCard, transferCard, scoreCard;
    std::shared_ptr<vibecheck::SuiteResult> resultA, resultB;
    std::array<juce::String, 2> names;
    std::vector<Row> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Results)
};

ComparisonTab::ComparisonTab (PluginScanner& scanner)
    : pluginScanner (scanner)
{
    for (int i = 0; i < 2; ++i)
    {
        auto& slot = slots[(size_t) i];
        slot.picker = std::make_unique<PluginPicker> (scanner, i == 0 ? "Select plugin A" : "Select plugin B");
        addAndMakeVisible (*slot.picker);

        slot.loadButton.setButtonText (i == 0 ? "Load A" : "Load B");
        slot.loadButton.onClick = [this, i] { load (i); };
        mbs::setButtonStyle (slot.loadButton, mbs::ButtonStyle::primary);
        addAndMakeVisible (slot.loadButton);

        slot.loaded.setFont (mbs::brandFont (12.5f));
        slot.loaded.setText ("Nothing loaded", juce::dontSendNotification);
        addAndMakeVisible (slot.loaded);

        slot.picker->onSelectionChanged = [this] { updateControls(); };
    }

    compareButton.onClick = [this] { compare(); };
    mbs::setButtonStyle (compareButton, mbs::ButtonStyle::primary);
    addAndMakeVisible (compareButton);

    statusLabel.setFont (mbs::monoFont (11.5f));
    statusLabel.setText ("Load a plugin into each slot", juce::dontSendNotification);
    addAndMakeVisible (statusLabel);
    addAndMakeVisible (busyBar);

    results = std::make_unique<Results>();
    resultsViewport.setViewedComponent (results.get(), false);
    resultsViewport.setScrollBarsShown (true, false);
    resultsViewport.setScrollBarThickness (8);
    addAndMakeVisible (resultsViewport);

    restyle();
    updateControls();
}

ComparisonTab::~ComparisonTab()
{
    pool.removeAllJobs (true, 30000);
}

void ComparisonTab::restyle()
{
    statusLabel.setColour (juce::Label::textColourId, mbs::theme().inkFaint);

    for (auto& slot : slots)
        slot.loaded.setColour (juce::Label::textColourId, mbs::theme().inkMuted);
}

void ComparisonTab::lookAndFeelChanged()
{
    restyle();

    if (results != nullptr)
        results->refreshTraces();
}

void ComparisonTab::updateControls()
{
    const auto anyBusy = slots[0].busy || slots[1].busy || comparing;

    for (auto& slot : slots)
    {
        juce::PluginDescription chosen;
        slot.loadButton.setEnabled (! anyBusy && slot.picker->getSelected (chosen));
        slot.picker->setEnabled (! anyBusy);
    }

    compareButton.setEnabled (! anyBusy && slots[0].plugin != nullptr && slots[1].plugin != nullptr);
    busyBar.setActive (anyBusy);
}

void ComparisonTab::runDemo (const juce::String& nameA, const juce::String& nameB)
{
    juce::PluginDescription description;

    if (! slots[0].picker->selectByName (nameA, description) || ! slots[1].picker->selectByName (nameB, description))
    {
        statusLabel.setText ("No plugin matching \"" + nameA + "\" or \"" + nameB + "\"", juce::dontSendNotification);
        return;
    }

    autoCompare = true;
    load (0);
    load (1);
}

void ComparisonTab::load (int which)
{
    juce::PluginDescription description;

    if (! slots[(size_t) which].picker->getSelected (description))
        return;

    auto& slot = slots[(size_t) which];
    slot.plugin.reset();
    slot.busy = true;
    slot.loaded.setText ("Loading " + description.name + "...", juce::dontSendNotification);
    statusLabel.setText (juce::String (which == 0 ? "Loading A" : "Loading B"), juce::dontSendNotification);
    updateControls();

    pool.addJob ([this, which, description, safe = juce::Component::SafePointer<ComparisonTab> (this)]
    {
        auto loaded = vibecheck::loadPlugin (pluginScanner.getFormatManager(), description, 48000.0, 512);
        auto* released = loaded.instance.release();
        const auto error = loaded.error;

        juce::MessageManager::callAsync ([safe, which, released, error, description]
        {
            std::unique_ptr<juce::AudioPluginInstance> owned (released);

            if (safe == nullptr)
                return;

            auto& target = safe->slots[(size_t) which];
            target.busy = false;
            target.plugin = std::move (owned);
            target.name = description.name;

            if (target.plugin != nullptr)
            {
                target.loaded.setText (description.name + "\n" + juce::String (target.plugin->getTotalNumInputChannels()) + " in, "
                                           + juce::String (target.plugin->getTotalNumOutputChannels()) + " out" + dot
                                           + juce::String (target.plugin->getParameters().size()) + " parameters",
                                       juce::dontSendNotification);
                safe->statusLabel.setText (safe->slots[0].plugin != nullptr && safe->slots[1].plugin != nullptr
                                               ? "Both loaded: press Compare" : "Loaded " + description.name,
                                           juce::dontSendNotification);
            }
            else
            {
                target.loaded.setText ("Could not load " + description.name, juce::dontSendNotification);
                safe->statusLabel.setText (error, juce::dontSendNotification);
            }

            safe->updateControls();

            if (safe->autoCompare && safe->slots[0].plugin != nullptr && safe->slots[1].plugin != nullptr)
            {
                safe->autoCompare = false;
                safe->compare();
            }
        });
    });
}

void ComparisonTab::compare()
{
    if (slots[0].plugin == nullptr || slots[1].plugin == nullptr)
        return;

    comparing = true;
    statusLabel.setText ("Measuring both plugins at once", juce::dontSendNotification);
    updateControls();

    struct Shared
    {
        std::array<std::shared_ptr<vibecheck::SuiteResult>, 2> results;
        std::atomic<int> remaining { 2 };
    };

    auto shared = std::make_shared<Shared>();
    const auto names = std::array<juce::String, 2> { slots[0].name, slots[1].name };

    // The same plugin loaded in both slots would be two instances, so the two jobs never share
    // one; each runs on its own thread, halving the wait.
    for (int i = 0; i < 2; ++i)
    {
        auto* instance = slots[(size_t) i].plugin.get();

        pool.addJob ([instance, i, shared, names, safe = juce::Component::SafePointer<ComparisonTab> (this)]
        {
            vibecheck::SuiteOptions options;
            options.sampleRate = 48000.0;
            options.blockSize = 512;

            shared->results[(size_t) i] = std::make_shared<vibecheck::SuiteResult> (vibecheck::runSuite (*instance, options));

            if (shared->remaining.fetch_sub (1) != 1)
                return;

            juce::MessageManager::callAsync ([safe, shared, names]
            {
                if (safe == nullptr)
                    return;

                safe->comparing = false;
                safe->results->setResults (shared->results[0], shared->results[1], names[0], names[1]);

                const auto failed = ! shared->results[0]->anyOk || ! shared->results[1]->anyOk;
                safe->statusLabel.setText (failed ? "One plugin could not be rendered" : "Compared in "
                                                       + mbs::num (juce::jmax (shared->results[0]->seconds, shared->results[1]->seconds), 1) + " s",
                                           juce::dontSendNotification);
                safe->updateControls();
            });
        });
    }
}

void ComparisonTab::paint (juce::Graphics& g)
{
    const auto& p = mbs::theme();

    for (int i = 0; i < 2; ++i)
    {
        const auto inner = mbs::drawTitledCard (g, slotCards[(size_t) i], i == 0 ? "Plugin A" : "Plugin B");
        juce::ignoreUnused (inner);

        const auto badge = juce::Rectangle<float> (22.0f, 22.0f).withPosition ((float) slotCards[(size_t) i].getRight() - 40.0f,
                                                                               (float) slotCards[(size_t) i].getY() + 10.0f);
        g.setColour ((i == 0 ? p.accent : p.accent2).withAlpha (0.18f));
        g.fillEllipse (badge);
        g.setColour (i == 0 ? p.accent : p.accent2);
        g.setFont (mbs::brandFont (12.0f, true));
        g.drawText (i == 0 ? "A" : "B", badge.toNearestInt(), juce::Justification::centred);
    }
}

void ComparisonTab::resized()
{
    auto area = getLocalBounds();

    auto cards = area.removeFromTop (196);
    const auto half = (cards.getWidth() - mbs::gutter) / 2;

    for (int i = 0; i < 2; ++i)
    {
        slotCards[(size_t) i] = cards.removeFromLeft (half);
        cards.removeFromLeft (mbs::gutter);

        auto inner = slotCards[(size_t) i].reduced (18, 14).withTrimmedTop (26);
        auto& slot = slots[(size_t) i];
        slot.picker->setBounds (inner.removeFromTop (PluginPicker::preferredHeight));
        inner.removeFromTop (10);

        auto row = inner.removeFromTop (40);
        slot.loadButton.setBounds (row.removeFromLeft (112).withSizeKeepingCentre (112, 34));
        row.removeFromLeft (12);
        slot.loaded.setBounds (row);
    }

    area.removeFromTop (mbs::gutter);
    actionBar = area.removeFromTop (40);
    compareButton.setBounds (actionBar.removeFromLeft (170));
    actionBar.removeFromLeft (16);
    busyBar.setBounds (actionBar.removeFromRight (160).withSizeKeepingCentre (160, 4));
    statusLabel.setBounds (actionBar);

    area.removeFromTop (mbs::gutter);
    resultsViewport.setBounds (area);

    const auto needed = results->heightFor (area.getHeight());
    results->setSize (needed > area.getHeight() ? area.getWidth() - 12 : area.getWidth(), needed);
}
