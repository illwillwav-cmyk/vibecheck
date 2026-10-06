#include "VibeCheckTab.h"

#include "ui/Theme.h"
#include "vibecheck/BinaryInspector.h"
#include "vibecheck/PluginLookup.h"

namespace
{
enum ColumnIds { nameColumn = 1, formatColumn, marksColumn, scoreColumn, confidenceColumn };

const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));

juce::Colour familyColour (vibecheck::Family family)
{
    const auto& p = mbs::theme();

    switch (family)
    {
        case vibecheck::Family::metadata:       return p.accent2;
        case vibecheck::Family::boilerplate:    return p.accent;
        case vibecheck::Family::dspNaivety:     return p.warn;
        case vibecheck::Family::stringArtifact: return p.bad;
        case vibecheck::Family::behaviour:      return p.warn.interpolatedWith (p.bad, 0.5f);
        case vibecheck::Family::source:         return p.accent2.interpolatedWith (p.bad, 0.45f);
    }

    return p.inkMuted;
}

juce::String familyName (vibecheck::Family family)
{
    switch (family)
    {
        case vibecheck::Family::metadata:       return "Metadata";
        case vibecheck::Family::boilerplate:    return "Boilerplate";
        case vibecheck::Family::dspNaivety:     return "DSP habits";
        case vibecheck::Family::stringArtifact: return "Stray text";
        case vibecheck::Family::behaviour:      return "Behaviour";
        case vibecheck::Family::source:         return "Source";
    }

    return "Other";
}

constexpr vibecheck::Family allFamilies[] = { vibecheck::Family::metadata, vibecheck::Family::boilerplate,
                                              vibecheck::Family::dspNaivety, vibecheck::Family::stringArtifact,
                                              vibecheck::Family::behaviour, vibecheck::Family::source };
} // namespace

// --- The detail column ---------------------------------------------------------------------------

/** Gauge, verdict, confidence, where the points came from, then the evidence itself. One painted
    surface laid out by a single function, so what is drawn and what is clickable never disagree. */
class VibeCheckTab::Detail final : public juce::Component, private juce::Timer
{
public:
    void setReport (vibecheck::VibeReport newReport)
    {
        report = std::move (newReport);
        hasReport = true;
        expanded.clearQuick();
        expanded.insertMultiple (0, false, (int) report.evidence.size());
        heights.clearQuick();
        heights.insertMultiple (0, (float) collapsedHeight, (int) report.evidence.size());
        hovered = -1;
        startTimerHz (60);
        updateSize();
        repaint();
    }

    void setMinimumHeight (int minimum)
    {
        minimumHeight = minimum;
        updateSize();
    }

    /** The width the owner has left for content, after its scrollbar. Taken from the owner because
        a viewport does not narrow what it shows when its scrollbar appears. */
    void setAvailableWidth (int width)
    {
        availableWidth = width;
        updateSize();
    }

    bool isAnimating() const { return isTimerRunning(); }

    bool showingReport() const { return hasReport; }

    void paint (juce::Graphics& g) override
    {
        const auto& p = mbs::theme();

        if (! hasReport)
        {
            mbs::drawEmptyState (g, getLocalBounds(), mbs::Icon::sparkle, "Pick a plugin",
                                 "Nothing is loaded or run. VibeCheck reads the file on disk and shows the evidence behind its score.");
            return;
        }

        const auto L = computeLayout (getWidth());

        // --- Gauge ---------------------------------------------------------------------------
        {
            const auto area = L.gauge.toFloat();
            const auto radius = juce::jmin (area.getWidth() * 0.5f - 18.0f, area.getHeight() * 0.5f - 12.0f);
            const auto centre = area.getCentre().translated (0.0f, 8.0f);
            constexpr auto start = juce::MathConstants<float>::pi * 1.2f;
            constexpr auto end   = juce::MathConstants<float>::pi * 2.8f;

            juce::Path track;
            track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, end, true);
            g.setColour (p.line);
            g.strokePath (track, juce::PathStrokeType (12.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            if (report.conclusive)
            {
                const auto colour = mbs::scoreColour (shownScore);
                juce::Path value;
                value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start,
                                     start + (end - start) * (float) juce::jmax (0.012, shownScore / 100.0), true);

                g.setColour (colour.withAlpha (0.18f));
                g.strokePath (value, juce::PathStrokeType (20.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (colour);
                g.strokePath (value, juce::PathStrokeType (12.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                g.setColour (p.ink);
                g.setFont (mbs::brandFont (46.0f, true));
                g.drawText (mbs::num (shownScore, 0) + "%", juce::Rectangle<float> (radius * 1.6f, 54.0f).withCentre (centre.translated (0.0f, -6.0f)).toNearestInt(),
                            juce::Justification::centred);
            }
            else
            {
                g.setColour (p.inkFaint);
                g.setFont (mbs::brandFont (46.0f, true));
                g.drawText ("?", juce::Rectangle<float> (radius * 1.6f, 54.0f).withCentre (centre.translated (0.0f, -6.0f)).toNearestInt(),
                            juce::Justification::centred);
            }

            g.setColour (p.inkFaint);
            g.setFont (mbs::brandFont (10.0f, true));
            mbs::drawTracked (g, report.conclusive ? "Vibe score" : "No score", juce::Rectangle<int> ((int) centre.x - 60, (int) centre.y + 24, 120, 16),
                              1.6f, juce::Justification::centred);
        }

        // --- Verdict -------------------------------------------------------------------------
        {
            const auto colour = report.conclusive ? mbs::scoreColour (report.score) : p.inkFaint;
            const auto width = (float) juce::GlyphArrangement::getStringWidth (mbs::brandFont (12.0f, true), report.headline) + 36.0f;
            mbs::drawPill (g, report.headline, juce::Rectangle<float> (width, 28.0f).withCentre (L.verdict.toFloat().getCentre()), colour, false);
        }

        // --- Confidence ----------------------------------------------------------------------
        {
            auto area = L.confidence;
            auto head = area.removeFromTop (18);
            g.setColour (p.inkFaint);
            g.setFont (mbs::brandFont (10.5f, true));
            mbs::drawTracked (g, "Confidence", head, 1.4f, juce::Justification::centredLeft);
            g.setColour (p.ink);
            g.setFont (mbs::monoFont (12.0f));
            g.drawText (mbs::num (shownConfidence * 100.0, 0) + "%", head, juce::Justification::centredRight);

            area.removeFromTop (4);
            const auto bar = area.removeFromTop (6).toFloat();
            g.setColour (p.line);
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (p.accent);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) juce::jlimit (0.0, 1.0, shownConfidence)), 3.0f);

            area.removeFromTop (6);
            g.setColour (p.inkFaint);
            g.setFont (mbs::brandFont (12.0f));
            g.drawFittedText (report.conclusive ? "How much of this binary could be read. Low confidence means a low score proves little."
                                                : report.opacityReason,
                              area, juce::Justification::topLeft, 3);
        }

        // --- Callout for high scores ---------------------------------------------------------
        if (! L.callout.isEmpty())
        {
            const auto box = L.callout.toFloat();
            g.setColour (p.bad.withAlpha (p.isDark ? 0.14f : 0.08f));
            g.fillRoundedRectangle (box, 10.0f);
            g.setColour (p.bad.withAlpha (0.5f));
            g.drawRoundedRectangle (box.reduced (0.5f), 10.0f, 1.0f);

            g.setColour (p.ink);
            g.setFont (mbs::brandFont (12.5f));
            g.drawFittedText (calloutText(), L.callout.reduced (14, 8), juce::Justification::centredLeft, 3);
        }

        // --- Where the points came from -----------------------------------------------------
        if (! L.bars.isEmpty())
        {
            auto area = L.bars;
            g.setColour (p.inkFaint);
            g.setFont (mbs::brandFont (10.5f, true));
            mbs::drawTracked (g, "Where the points come from", area.removeFromTop (22), 1.4f, juce::Justification::centredLeft);

            for (const auto family : allFamilies)
            {
                auto row = area.removeFromTop (24);
                const auto points = report.pointsFor (family);

                g.setColour (p.inkMuted);
                g.setFont (mbs::brandFont (12.5f));
                g.drawText (familyName (family), row.removeFromLeft (92), juce::Justification::centredLeft);

                const auto pointsArea = row.removeFromRight (40);
                g.setColour (points > 0.0 ? p.ink : p.inkFaint);
                g.setFont (mbs::monoFont (12.0f));
                g.drawText (points > 0.0 ? "+" + mbs::num (points, 0) : "0", pointsArea, juce::Justification::centredRight);

                const auto bar = row.reduced (0, 9).toFloat();
                g.setColour (p.line);
                g.fillRoundedRectangle (bar, 2.5f);
                g.setColour (familyColour (family));
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) juce::jmin (1.0, points / 60.0)), 2.5f);
            }
        }

        // --- Evidence ------------------------------------------------------------------------
        {
            g.setColour (p.ink);
            g.setFont (mbs::brandFont (14.0f, true));
            // "None found" and "none could be looked for" are very different claims.
            const auto heading = ! report.conclusive ? juce::String ("Nothing could be read from this binary")
                               : report.evidence.empty() ? juce::String ("No fingerprints found")
                               : juce::String ((int) report.evidence.size()) + (report.evidence.size() == 1 ? " fingerprint found" : " fingerprints found");
            g.drawText (heading, L.evidenceHeading, juce::Justification::bottomLeft);
        }

        for (int i = 0; i < (int) report.evidence.size(); ++i)
        {
            const auto& item = report.evidence[(size_t) i];
            const auto colour = familyColour (item.family);
            const auto box = L.evidence[(size_t) i];
            const auto isHovered = hovered == i;

            g.setColour (isHovered ? p.cardHi.brighter (p.isDark ? 0.05f : -0.02f) : p.cardHi);
            g.fillRoundedRectangle (box.toFloat(), 10.0f);
            g.setColour (isHovered ? colour.withAlpha (0.7f) : p.line);
            g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 10.0f, 1.0f);

            // A stripe of the family colour down the left edge.
            g.setColour (colour);
            g.fillRoundedRectangle (juce::Rectangle<float> ((float) box.getX() + 8.0f, (float) box.getY() + 12.0f, 3.0f, (float) collapsedHeight - 24.0f), 1.5f);

            auto inner = box.reduced (20, 10);
            auto top = inner.removeFromTop (collapsedHeight - 20);
            const auto points = top.removeFromRight (40);

            g.setColour (isHovered ? colour : p.ink);
            g.setFont (mbs::monoFont (13.0f));
            g.drawText ("+" + mbs::num (item.points, 0), points, juce::Justification::centredRight);

            auto first = top.removeFromTop (18);
            g.setColour (colour);
            g.setFont (mbs::brandFont (9.5f, true));
            mbs::drawTracked (g, familyName (item.family), first.removeFromTop (12), 1.2f, juce::Justification::centredLeft);

            g.setColour (p.ink);
            g.setFont (mbs::brandFont (13.0f, true));
            g.drawText (item.finding, top.removeFromTop (18), juce::Justification::centredLeft, true);

            g.setColour (p.inkFaint);
            g.setFont (mbs::monoFont (11.0f));
            g.drawText (item.detail, top.removeFromTop (14), juce::Justification::centredLeft, true);

            const auto reveal = juce::jlimit (0.0f, 1.0f, (heights[i] - (float) collapsedHeight) / (float) (expandedHeight - collapsedHeight));

            if (reveal > 0.02f)
            {
                g.setColour (p.inkMuted.withAlpha (reveal));
                g.setFont (mbs::brandFont (12.5f));
                g.drawFittedText (item.explanation, inner.withTrimmedTop (4), juce::Justification::topLeft, 4);
            }
            else
            {
                // A small hint that the card opens.
                g.setColour (p.inkFaint.withAlpha (0.7f));
                g.setFont (mbs::brandFont (10.0f));
            }
        }

        const auto drawList = [&] (const juce::String& title, const juce::StringArray& lines, juce::Rectangle<int> area, mbs::Icon icon, juce::Colour colour)
        {
            if (lines.isEmpty())
                return;

            g.setColour (p.inkFaint);
            g.setFont (mbs::brandFont (10.5f, true));
            mbs::drawTracked (g, title, area.removeFromTop (24), 1.4f, juce::Justification::centredLeft);

            for (const auto& line : lines)
            {
                auto row = area.removeFromTop (line.length() > 80 ? 50 : 34);
                mbs::drawIcon (g, icon, row.removeFromLeft (20).removeFromTop (20).reduced (3).toFloat(), colour, 2.0f);
                g.setColour (p.inkMuted);
                g.setFont (mbs::brandFont (12.5f));
                g.drawFittedText (line, row.reduced (6, 0), juce::Justification::topLeft, 3);
            }
        };

        drawList ("Pointing the other way", report.humanSignals, L.humans, mbs::Icon::check, p.good);
        drawList ("Could not be judged", report.notEvaluated, L.unjudged, mbs::Icon::alert, p.warn);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto row = rowAt (e.getPosition());

        if (row != hovered)
        {
            hovered = row;
            setMouseCursor (row >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hovered != -1)
        {
            hovered = -1;
            repaint();
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto row = rowAt (e.getPosition());

        if (row >= 0)
        {
            expanded.set (row, ! expanded[row]);
            startTimerHz (60);
        }
    }

private:
    static constexpr int collapsedHeight = 66, expandedHeight = 134;

    struct Layout
    {
        juce::Rectangle<int> gauge, verdict, confidence, callout, bars, evidenceHeading, humans, unjudged;
        std::vector<juce::Rectangle<int>> evidence;
        int total = 0;
    };

    Layout computeLayout (int width) const
    {
        Layout L;
        auto area = juce::Rectangle<int> (0, 0, width, 100000).reduced (2, 0);
        int y = 0;

        L.gauge = area.removeFromTop (156);
        L.verdict = area.removeFromTop (36);
        area.removeFromTop (4);
        L.confidence = area.removeFromTop (68);
        y = area.getY();

        if (report.conclusive && report.score >= 75.0)
        {
            area.removeFromTop (6);
            L.callout = area.removeFromTop (66);
        }

        if (report.conclusive)
        {
            area.removeFromTop (8);
            L.bars = area.removeFromTop (22 + (int) std::size (allFamilies) * 24);
        }

        area.removeFromTop (10);
        L.evidenceHeading = area.removeFromTop (30);
        area.removeFromTop (6);

        for (int i = 0; i < (int) report.evidence.size(); ++i)
        {
            L.evidence.push_back (area.removeFromTop ((int) heights[i]));
            area.removeFromTop (8);
        }

        const auto listHeight = [] (const juce::StringArray& lines)
        {
            int h = lines.isEmpty() ? 0 : 24;

            for (const auto& line : lines)
                h += line.length() > 80 ? 50 : 34;

            return h;
        };

        area.removeFromTop (4);
        L.humans = area.removeFromTop (listHeight (report.humanSignals));
        if (L.humans.getHeight() > 0) area.removeFromTop (8);
        L.unjudged = area.removeFromTop (listHeight (report.notEvaluated));

        L.total = area.getY() + 16;
        juce::ignoreUnused (y);
        return L;
    }

    int rowAt (juce::Point<int> point) const
    {
        if (! hasReport)
            return -1;

        const auto L = computeLayout (getWidth());

        for (int i = 0; i < (int) L.evidence.size(); ++i)
            if (L.evidence[(size_t) i].contains (point))
                return i;

        return -1;
    }

    juce::String calloutText() const
    {
        int counts[std::size (allFamilies)] = {};

        for (const auto& e : report.evidence)
            ++counts[(int) e.family];

        juce::StringArray traits;

        if (counts[(int) vibecheck::Family::boilerplate])    traits.add ("generic scaffolding");
        if (counts[(int) vibecheck::Family::metadata])       traits.add ("placeholder metadata");
        if (counts[(int) vibecheck::Family::dspNaivety])     traits.add ("naive DSP habits");
        if (counts[(int) vibecheck::Family::stringArtifact]) traits.add ("chat-assistant text");
        if (counts[(int) vibecheck::Family::behaviour])      traits.add ("careless audio-thread behaviour");
        if (counts[(int) vibecheck::Family::source])         traits.add ("signs of AI assistance in its source");

        return "This binary carries the marks of an unedited generated project: "
               + (traits.isEmpty() ? juce::String ("several fingerprints") : traits.joinIntoString (", ")) + ".";
    }

    void updateSize()
    {
        const auto width = availableWidth > 0 ? availableWidth : getWidth();
        setSize (width, juce::jmax (minimumHeight, hasReport ? computeLayout (width).total : 0));
    }

    void timerCallback() override
    {
        bool moving = false;

        const auto ease = [&moving] (double& shown, double target, double threshold)
        {
            if (std::abs (shown - target) > threshold)
            {
                shown += (target - shown) * 0.16;
                moving = true;
            }
            else
            {
                shown = target;
            }
        };

        ease (shownScore, report.score, 0.4);
        ease (shownConfidence, report.confidence, 0.004);

        for (int i = 0; i < expanded.size(); ++i)
        {
            const auto target = expanded[i] ? (float) expandedHeight : (float) collapsedHeight;

            if (std::abs (heights[i] - target) > 0.6f)
            {
                heights.set (i, heights[i] + (target - heights[i]) * 0.25f);
                moving = true;
            }
            else
            {
                heights.set (i, target);
            }
        }

        updateSize();
        repaint();

        if (! moving)
            stopTimer();
    }

    vibecheck::VibeReport report;
    bool hasReport = false;
    juce::Array<bool> expanded;
    juce::Array<float> heights;
    int hovered = -1, minimumHeight = 0, availableWidth = 0;
    double shownScore = 0.0, shownConfidence = 0.0;
};

// --- The page -----------------------------------------------------------------------------------

VibeCheckTab::VibeCheckTab (PluginScanner& scanner)
    : pluginScanner (scanner)
{
    detail = std::make_unique<Detail>();

    for (auto* tile : { &weighedTile, &flaggedTile, &averageTile, &unreadableTile })
    {
        tile->set ("-", "Waiting for the library");
        addAndMakeVisible (tile);
    }

    searchBox.setTextToShowWhenEmpty ("Search by name, maker or format", mbs::theme().inkFaint);
    searchBox.onTextChange = [this] { applyFilter(); };
    addAndMakeVisible (searchBox);

    rescanButton.onClick = [this] { sweepLibrary(); };
    addAndMakeVisible (rescanButton);

    exportButton.setEnabled (false);
    exportButton.onClick = [this] { exportLibrary(); };
    addAndMakeVisible (exportButton);

    deepButton.setEnabled (false);
    deepButton.onClick = [this] { runDeepCheck(); };
    addAndMakeVisible (deepButton);

    sourceButton.setEnabled (false);
    sourceButton.setTooltip ("Read this plugin's source code and commit history, if they are public");
    sourceButton.onClick = [this] { runSourceCheck(); };
    addAndMakeVisible (sourceButton);

    // What you know about the plugin, as opposed to what the detector guesses.
    labelBox.addItem (vibecheck::describe (vibecheck::Label::none), 1);
    labelBox.addItem (vibecheck::describe (vibecheck::Label::vibeCoded), 2);
    labelBox.addItem (vibecheck::describe (vibecheck::Label::handWritten), 3);
    labelBox.setSelectedId (1, juce::dontSendNotification);
    labelBox.setTooltip ("Record what you know for certain about this plugin. Labels go into the export and are what the detector is measured against.");
    labelBox.onChange = [this] { labelChanged(); };
    addChildComponent (labelBox);

    labels = vibecheck::allLabels (pluginScanner.getSettings());

    updateButton.setEnabled (false);
    updateButton.onClick = [this]
    {
        if (hasCurrent)
            juce::URL ("https://www.google.com/search?q=" + juce::URL::addEscapeChars (currentDescription.name + " " + currentDescription.manufacturerName
                                                                                        + " audio plugin update", true))
                .launchInDefaultBrowser();
    };
    addAndMakeVisible (updateButton);

    uninstallButton.setEnabled (false);
    mbs::setButtonStyle (uninstallButton, mbs::ButtonStyle::danger);
    uninstallButton.onClick = [this] { confirmUninstall(); };
    addAndMakeVisible (uninstallButton);

    addAndMakeVisible (busyBar);

    table.setModel (this);
    table.setHeaderHeight (32);
    table.setRowHeight (48);
    table.setOutlineThickness (0);
    table.getViewport()->setScrollBarsShown (true, false);
    table.getViewport()->setScrollBarThickness (8);

    auto& header = table.getHeader();
    header.addColumn ("Plugin",     nameColumn,       280, 150, -1, juce::TableHeaderComponent::defaultFlags);
    header.addColumn ("Format",     formatColumn,      90,  70, -1, juce::TableHeaderComponent::defaultFlags);
    header.addColumn ("Marks",      marksColumn,       64,  54, -1, juce::TableHeaderComponent::defaultFlags);
    header.addColumn ("Vibe",       scoreColumn,      150, 110, -1, juce::TableHeaderComponent::defaultFlags);
    header.addColumn ("Confidence", confidenceColumn,  96,  80, -1, juce::TableHeaderComponent::defaultFlags);
    // Without this the columns keep their nominal widths and the last one is clipped.
    header.setStretchToFitActive (true);
    header.setSortColumnId (scoreColumn, false);
    addAndMakeVisible (table);

    detailViewport.setViewedComponent (detail.get(), false);
    detailViewport.setScrollBarsShown (true, false);
    detailViewport.setScrollBarThickness (8);
    addAndMakeVisible (detailViewport);

   #if VIBECHECK_AI_CHECK
    sweepLibrary();
   #endif   // with the page switched off, nothing reads the library in the background
}

VibeCheckTab::~VibeCheckTab()
{
    stopTimer();
    deepCancel = true;
    sourceCancel = true;
    sweep.cancel();
    pool.removeAllJobs (true, 10000);
    table.setModel (nullptr);
}

bool VibeCheckTab::isIdle() const
{
    return ! busy && ! sweeping && ! deepRunning && ! sourceRunning && animatedListProgress >= 1.0f && ! detail->isAnimating();
}

void VibeCheckTab::lookAndFeelChanged()
{
    searchBox.setTextToShowWhenEmpty ("Search by name, maker or format", mbs::theme().inkFaint);
    table.repaint();
}

void VibeCheckTab::updateSummary()
{
    const auto& p = mbs::theme();

    if (summary.total == 0)
    {
        for (auto* tile : { &weighedTile, &flaggedTile, &averageTile, &unreadableTile })
            tile->set ("-", "No plugins found yet");

        return;
    }

    weighedTile.set (mbs::num (summary.judged), "of " + mbs::num (summary.total) + " plugins could be read");
    weighedTile.setMeter (summary.total > 0 ? (float) summary.judged / (float) summary.total : 0.0f);

    const auto flagged = summary.flaggedPercent();
    flaggedTile.set (mbs::percent (flagged), mbs::num (summary.flagged) + " of " + mbs::num (summary.judged)
                                                 + " score " + mbs::num (vibecheck::flagThreshold, 0) + " or more",
                     flagged < 5.0 ? p.good : flagged < 20.0 ? p.warn : p.bad);

    averageTile.set (mbs::num (summary.averageScore, 0) + "%", "Mean score of the readable plugins",
                     mbs::scoreColour (summary.averageScore));

    const auto opaque = summary.total - summary.judged;
    unreadableTile.set (mbs::num (opaque), opaque > 0 ? "Encrypted, stripped or copy-protected" : "Every binary could be read",
                        opaque > 0 ? p.warn : p.good);
}

void VibeCheckTab::sweepLibrary()
{
    const auto types = pluginScanner.getKnownPluginList().getTypes();

    if (types.isEmpty())
    {
        summary = {};
        updateSummary();
        return;
    }

    sweeping = true;
    sweepDone = 0;
    sweepTotal = types.size();
    rescanButton.setEnabled (false);
    exportButton.setEnabled (false);
    busyBar.setActive (true);
    weighedTile.set ("0", "Weighing " + mbs::num (types.size()) + " plugins");

    sweep.start (types,
                 pluginScanner.getSettings(),
                 [safe = juce::Component::SafePointer<VibeCheckTab> (this)] (int done, int total, const std::vector<vibecheck::SweepEntry>& current)
                 {
                     if (safe == nullptr)
                         return;

                     safe->sweepDone = done;
                     safe->entries = current;
                     safe->weighedTile.set (mbs::num (done), "of " + mbs::num (total) + " weighed so far");
                     safe->weighedTile.setMeter (total > 0 ? (float) done / (float) total : 0.0f);
                     safe->sortOrderChanged (safe->sortColumn, safe->sortForwards);
                 },
                 [safe = juce::Component::SafePointer<VibeCheckTab> (this)] (std::vector<vibecheck::SweepEntry> results)
                 {
                     if (safe == nullptr)
                         return;

                     safe->entries = std::move (results);
                     safe->summary = vibecheck::summarise (safe->entries);
                     safe->updateSummary();
                     safe->sweeping = false;
                     safe->rescanButton.setEnabled (true);
                     safe->exportButton.setEnabled (! safe->entries.empty());
                     safe->busyBar.setActive (false);

                     // Bars grow in once the final results land.
                     safe->animatedListProgress = 0.0f;
                     safe->startTimerHz (60);

                     safe->sortOrderChanged (safe->sortColumn, safe->sortForwards);
                 });
}

void VibeCheckTab::timerCallback()
{
    animatedListProgress = juce::jmin (1.0f, animatedListProgress + 0.06f);

    if (animatedListProgress >= 1.0f)
        stopTimer();

    table.repaint();
}

void VibeCheckTab::applyFilter()
{
    const auto filter = searchBox.getText().trim();

    visibleRows.clearQuick();

    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& description = entries[(std::size_t) i].description;

        if (filter.isEmpty()
            || description.name.containsIgnoreCase (filter)
            || description.manufacturerName.containsIgnoreCase (filter)
            || description.pluginFormatName.containsIgnoreCase (filter)
            || description.category.containsIgnoreCase (filter))
            visibleRows.add (i);
    }

    table.updateContent();
    restoreSelection();
    table.repaint();
}

void VibeCheckTab::restoreSelection()
{
    // Sorting and filtering move rows around; the plugin on show must stay selected without being
    // inspected all over again.
    suppressSelection = true;

    int found = -1;

    if (hasCurrent)
        for (int row = 0; row < visibleRows.size(); ++row)
        {
            const auto& d = entries[(std::size_t) visibleRows[row]].description;

            if (d.fileOrIdentifier == currentDescription.fileOrIdentifier && d.name == currentDescription.name
                && d.pluginFormatName == currentDescription.pluginFormatName)
            {
                found = row;
                break;
            }
        }

    if (found >= 0)
        table.selectRow (found, true, false);
    else
        table.deselectAllRows();

    suppressSelection = false;
}

void VibeCheckTab::sortOrderChanged (int newSortColumnId, bool isForwards)
{
    sortColumn = newSortColumnId;
    sortForwards = isForwards;

    std::stable_sort (entries.begin(), entries.end(),
                      [newSortColumnId, isForwards] (const vibecheck::SweepEntry& a, const vibecheck::SweepEntry& b)
    {
        const auto ascending = [isForwards] (bool less) { return isForwards ? less : ! less; };

        switch (newSortColumnId)
        {
            case formatColumn:     return ascending (a.description.pluginFormatName < b.description.pluginFormatName);
            case marksColumn:      return ascending (a.fingerprints < b.fingerprints);
            case confidenceColumn: return ascending (a.confidence < b.confidence);
            case scoreColumn:
                // Plugins that could not be read have no score, so they sit at the end rather
                // than mixing in among the ones that scored zero honestly.
                if (a.conclusive != b.conclusive)
                    return a.conclusive;

                return ascending (a.score < b.score);

            default: return ascending (a.description.name.compareIgnoreCase (b.description.name) < 0);
        }
    });

    applyFilter();
}

const vibecheck::SweepEntry* VibeCheckTab::entryForRow (int row) const
{
    if (! juce::isPositiveAndBelow (row, visibleRows.size()))
        return nullptr;

    const auto index = visibleRows[row];
    return juce::isPositiveAndBelow (index, (int) entries.size()) ? &entries[(std::size_t) index] : nullptr;
}

int VibeCheckTab::getNumRows() { return visibleRows.size(); }

void VibeCheckTab::paintRowBackground (juce::Graphics& g, int, int width, int height, bool selected)
{
    const auto& p = mbs::theme();

    if (selected)
    {
        g.setColour (p.accent.withAlpha (p.isDark ? 0.16f : 0.10f));
        g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 1.0f, (float) width, (float) height - 2.0f), 8.0f);
        g.setColour (p.accent.withAlpha (0.55f));
        g.drawRoundedRectangle (juce::Rectangle<float> (0.5f, 1.5f, (float) width - 1.0f, (float) height - 3.0f), 8.0f, 1.0f);
        return;
    }

    g.setColour (p.line.withAlpha (0.55f));
    g.fillRect (8, height - 1, width - 16, 1);
}

void VibeCheckTab::paintCell (juce::Graphics& g, int row, int columnId, int width, int height, bool)
{
    const auto* entry = entryForRow (row);

    if (entry == nullptr)
        return;

    const auto& p = mbs::theme();
    const juce::Rectangle<int> area (10, 0, width - 20, height);

    switch (columnId)
    {
        case nameColumn:
        {
            auto text = area;
            g.setColour (p.ink);
            g.setFont (mbs::brandFont (13.5f, true));
            g.drawText (entry->description.name, text.removeFromTop (height / 2 + 2).withTrimmedTop (6), juce::Justification::bottomLeft, true);
            g.setColour (p.inkFaint);
            g.setFont (mbs::brandFont (11.5f));
            auto maker = entry->description.manufacturerName.isEmpty() ? juce::String ("Unknown maker") : entry->description.manufacturerName;

            // What the person knows, shown beside what the detector thinks.
            if (const auto known = labels.find (vibecheck::labelKey (entry->description)); known != labels.end())
                maker << dot << (known->second == vibecheck::Label::vibeCoded ? "you know: vibe-coded" : "you know: hand-written");

            g.drawText (maker, text, juce::Justification::topLeft, true);
            break;
        }

        case formatColumn:
            g.setColour (p.inkMuted);
            g.setFont (mbs::brandFont (12.5f));
            g.drawText (entry->description.pluginFormatName, area, juce::Justification::centredLeft, true);
            break;

        case marksColumn:
            g.setColour (entry->fingerprints > 0 ? p.ink : p.inkFaint);
            g.setFont (mbs::monoFont (12.5f));
            g.drawText (entry->conclusive ? mbs::num (entry->fingerprints) : juce::String ("-"), area, juce::Justification::centredLeft, false);
            break;

        case scoreColumn:
        {
            if (! entry->conclusive)
            {
                mbs::drawPill (g, "Unreadable", juce::Rectangle<float> (84.0f, 22.0f).withCentre ({ (float) area.getX() + 42.0f, (float) height * 0.5f }),
                               p.inkFaint, false);
                break;
            }

            const auto colour = mbs::scoreColour (entry->score);
            auto bar = area.withTrimmedLeft (46).reduced (0, height / 2 - 3).toFloat();

            g.setColour (p.line);
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (colour);
            g.fillRoundedRectangle (bar.withWidth (juce::jmax (3.0f, bar.getWidth() * (float) (entry->score / 100.0) * animatedListProgress)), 3.0f);

            g.setColour (p.ink);
            g.setFont (mbs::monoFont (12.5f));
            g.drawText (mbs::num (entry->score * animatedListProgress, 0) + "%", area.withWidth (42), juce::Justification::centredLeft, false);
            break;
        }

        case confidenceColumn:
            g.setColour (p.inkMuted);
            g.setFont (mbs::monoFont (12.5f));
            g.drawText (mbs::num (entry->confidence * 100.0, 0) + "%", area, juce::Justification::centredLeft, false);
            break;

        default:
            break;
    }
}

void VibeCheckTab::selectedRowsChanged (int lastRow)
{
    if (suppressSelection)
        return;

    if (const auto* entry = entryForRow (lastRow))
        inspect (entry->description);
}

void VibeCheckTab::runDemo (const juce::String& query, bool alsoDeepCheck)
{
    deepAfterInspect = alsoDeepCheck;
    inspectQuery (query);
}

/** Resolves a name or a path first, then inspects whatever it found. */
void VibeCheckTab::inspectQuery (const juce::String& query)
{
    busy = true;
    headerTitle = "Looking for " + query;
    repaint();

    pool.addJob ([this, query, safe = juce::Component::SafePointer<VibeCheckTab> (this)]
    {
        juce::PluginDescription description;
        const auto found = vibecheck::lookupPlugin (pluginScanner.getFormatManager(),
                                                    pluginScanner.getKnownPluginList(), query, description);

        juce::MessageManager::callAsync ([safe, query, description, found]
        {
            if (safe == nullptr)
                return;

            safe->busy = false;

            if (found)
            {
                safe->inspect (description);

                if (std::exchange (safe->deepAfterInspect, false))
                    safe->runDeepCheck();

                if (const auto source = std::exchange (safe->sourceAfterInspect, {}); source.isNotEmpty())
                {
                    const auto repository = vibecheck::parseRepository (source);
                    safe->startSourceJob (description, repository, repository.isValid() ? juce::File() : juce::File (source));
                }
            }
            else
            {
                safe->headerTitle = "Nothing found matching \"" + query + "\"";
                safe->repaint();
            }
        });
    });
}

void VibeCheckTab::inspect (juce::PluginDescription description)
{
    busy = true;
    busyBar.setActive (true);
    currentDescription = description;
    hasCurrent = true;
    updateButton.setEnabled (true);
    uninstallButton.setEnabled (true);
    deepButton.setEnabled (! deepRunning);
    sourceButton.setEnabled (! sourceRunning);

    const auto known = labels.find (vibecheck::labelKey (description));
    const auto label = known != labels.end() ? known->second : vibecheck::Label::none;
    labelBox.setSelectedId (label == vibecheck::Label::vibeCoded ? 2 : label == vibecheck::Label::handWritten ? 3 : 1, juce::dontSendNotification);
    labelBox.setVisible (true);

    headerTitle = description.name;
    headerSub = description.pluginFormatName + dot + (description.manufacturerName.isEmpty() ? juce::String ("unknown maker") : description.manufacturerName);
    headerFacts = "Reading the binary...";
    repaint();

    pool.addJob ([description, settings = pluginScanner.getSettings(), safe = juce::Component::SafePointer<VibeCheckTab> (this)]
    {
        auto inspector = vibecheck::createBinaryInspector();
        const auto facts = inspector->inspect (description);

        // A plugin that has had a deep check is scored with what it measured, here and in the
        // list alike.
        const auto bundle = inspector->locate (description);
        const auto modified = bundle.exists() ? bundle.getLastModificationTime().toMilliseconds() : juce::int64 (0);
        const auto behaviour = vibecheck::findBehaviour (settings, description, modified);
        const auto source = vibecheck::findSource (settings, description, modified);
        const auto report = vibecheck::assessVibe (facts, description, behaviour.has_value() ? &*behaviour : nullptr,
                                                   source.has_value() ? &*source : nullptr);
        const auto deep = behaviour.has_value();

        juce::String details;

        if (facts.ok)
        {
            juce::StringArray parts;
            parts.add (juce::File::descriptionOfSizeInBytes (facts.sizeInBytes));
            parts.add (mbs::num (facts.definedSymbols.size()) + " symbols");
            parts.add (mbs::num (facts.strings.size()) + " strings");

            if (facts.paceWrapped) parts.add ("copy protected");
            if (facts.stripped)    parts.add ("stripped");

            details = parts.joinIntoString (dot);
        }
        else
        {
            details = facts.error;
        }

        juce::MessageManager::callAsync ([safe, report, details, description, deep]
        {
            if (safe == nullptr)
                return;

            safe->applyReportToRow (description, report, deep);

            // The user may have clicked another row while this was reading.
            if (safe->currentDescription.fileOrIdentifier != description.fileOrIdentifier || safe->currentDescription.name != description.name)
                return;

            safe->show (report, details);
            safe->busy = false;
            safe->busyBar.setActive (safe->sweeping);
        });
    });
}

void VibeCheckTab::applyReportToRow (const juce::PluginDescription& description, const vibecheck::VibeReport& report, bool deep)
{
    for (auto& entry : entries)
    {
        if (entry.description.fileOrIdentifier != description.fileOrIdentifier || entry.description.name != description.name
            || entry.description.pluginFormatName != description.pluginFormatName)
            continue;

        if (std::abs (entry.score - report.score) < 0.01 && entry.deep == deep && entry.conclusive == report.conclusive)
            return;

        entry.score = report.score;
        entry.confidence = report.confidence;
        entry.conclusive = report.conclusive;
        entry.headline = report.headline;
        entry.fingerprints = (int) report.evidence.size();
        entry.deep = deep;
        entry.findings.clear();

        for (const auto& item : report.evidence)
            entry.findings.push_back ({ vibecheck::toString (item.family), item.finding, item.points });

        summary = vibecheck::summarise (entries);
        updateSummary();
        sortOrderChanged (sortColumn, sortForwards);
        return;
    }
}

void VibeCheckTab::runDeepCheck()
{
    if (! hasCurrent || deepRunning)
        return;

    deepRunning = true;
    deepCancel = false;
    deepButton.setEnabled (false);
    busy = true;
    busyBar.setActive (true);

    const auto description = currentDescription;
    headerFacts = "Running " + description.name + " for a few seconds and watching its audio thread...";
    repaint();

    pool.addJob ([description, safe = juce::Component::SafePointer<VibeCheckTab> (this), &cancel = deepCancel]
    {
        const auto result = vibecheck::measureInChildProcess (description, 120000, cancel);

        juce::MessageManager::callAsync ([safe, description, result]
        {
            if (safe == nullptr)
                return;

            safe->deepRunning = false;
            safe->busy = false;
            safe->busyBar.setActive (safe->sweeping);
            safe->deepButton.setEnabled (safe->hasCurrent);

            if (! result.ok)
            {
                safe->headerFacts = "Deep check did not finish: " + result.error;
                safe->repaint();
                return;
            }

            auto inspector = vibecheck::createBinaryInspector();
            const auto bundle = inspector->locate (description);
            const auto modified = bundle.exists() ? bundle.getLastModificationTime().toMilliseconds() : juce::int64 (0);
            vibecheck::storeBehaviour (safe->pluginScanner.getSettings(), description, modified, result);

            // Read it back through the normal path, so the detail and the list get the same score.
            if (safe->currentDescription.fileOrIdentifier == description.fileOrIdentifier && safe->currentDescription.name == description.name)
                safe->inspect (description);
        });
    });
}

void VibeCheckTab::exportLibrary()
{
    if (entries.empty() || sweeping)
        return;

    const auto name = "VibeCheck-" + juce::Time::getCurrentTime().formatted ("%Y-%m-%d") + ".json";
    exportChooser = std::make_unique<juce::FileChooser> ("Export your library's results",
                                                         juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile (name),
                                                         "*.json");

    exportChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                    | juce::FileBrowserComponent::warnAboutOverwriting,
                                [safe = juce::Component::SafePointer<VibeCheckTab> (this)] (const juce::FileChooser& chooser)
                                {
                                    const auto file = chooser.getResult();

                                    if (safe == nullptr || file == juce::File())
                                        return;

                                    const auto written = vibecheck::writeExport (file, safe->entries, JUCEApplication::getInstance()->getApplicationVersion(), safe->labels);

                                    juce::AlertWindow::showMessageBoxAsync (
                                        written.wasOk() ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                                        written.wasOk() ? "Exported " + mbs::num ((int) safe->entries.size()) + " plugins" : "Could not export",
                                        written.wasOk() ? "Saved to " + file.getFullPathName()
                                                              + "\n\nIt holds each plugin's name, maker, version, score, the fingerprints behind it, and any labels you gave. "
                                                                "It contains no file paths, user names or anything about this computer."
                                                        : written.getErrorMessage());
                                });
}

void VibeCheckTab::labelChanged()
{
    if (! hasCurrent)
        return;

    const auto id = labelBox.getSelectedId();
    const auto label = id == 2 ? vibecheck::Label::vibeCoded : id == 3 ? vibecheck::Label::handWritten : vibecheck::Label::none;

    vibecheck::setLabel (pluginScanner.getSettings(), currentDescription, label);
    labels = vibecheck::allLabels (pluginScanner.getSettings());
    table.repaint();
}

void VibeCheckTab::runSourceCheck()
{
    if (! hasCurrent || sourceRunning)
        return;

    const auto description = currentDescription;

    // Many open-source plugins carry their own repository address; offer it rather than ask for it.
    pool.addJob ([description, safe = juce::Component::SafePointer<VibeCheckTab> (this)]
    {
        auto inspector = vibecheck::createBinaryInspector();
        const auto found = vibecheck::findRepository (inspector->inspect (description).strings);

        juce::MessageManager::callAsync ([safe, description, found]
        {
            if (safe == nullptr)
                return;

            auto* window = new juce::AlertWindow ("Read the source code of " + description.name,
                                                  found.isValid() ? "This plugin names a public repository. VibeCheck can download it from GitHub and read the code and "
                                                                    "its recent commit history. Nothing is sent but the request for those files."
                                                                  : "If this plugin's code is public, paste its GitHub address. If you have the code on this "
                                                                    "computer, choose its folder instead.",
                                                  juce::MessageBoxIconType::QuestionIcon, safe.getComponent());
            window->addTextEditor ("address", found.isValid() ? "https://" + found.display() : juce::String(), "GitHub address");
            window->addButton ("Read from GitHub", 1, juce::KeyPress (juce::KeyPress::returnKey));
            window->addButton ("Choose a folder...", 2);
            window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

            window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window, description] (int result)
            {
                if (safe == nullptr || result == 0)
                    return;

                if (result == 1)
                {
                    const auto repository = vibecheck::parseRepository (window->getTextEditorContents ("address"));

                    if (repository.isValid())
                        safe->startSourceJob (description, repository, {});
                    else
                    {
                        safe->headerFacts = "That is not a GitHub address. It should look like https://github.com/owner/name";
                        safe->repaint();
                    }

                    return;
                }

                safe->sourceChooser = std::make_unique<juce::FileChooser> ("Choose the folder that holds " + description.name + "'s source code",
                                                                           juce::File::getSpecialLocation (juce::File::userHomeDirectory));
                safe->sourceChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                                  [safe, description] (const juce::FileChooser& chooser)
                                                  {
                                                      if (safe != nullptr && chooser.getResult().isDirectory())
                                                          safe->startSourceJob (description, {}, chooser.getResult());
                                                  });
            }), true);
        });
    });
}

void VibeCheckTab::startSourceJob (juce::PluginDescription description, vibecheck::RepositoryRef repository, juce::File folder)
{
    if (sourceRunning)
        return;

    sourceRunning = true;
    sourceCancel = false;
    sourceButton.setEnabled (false);
    busy = true;
    busyBar.setActive (true);
    headerFacts = repository.isValid() ? "Fetching " + repository.display() + "..." : juce::String ("Reading the source folder...");
    repaint();

    // The AudioUnit and VST3 builds of a plugin come from the same source, so both get the result.
    juce::Array<juce::PluginDescription> builds;

    for (const auto& entry : entries)
        if (vibecheck::labelKey (entry.description) == vibecheck::labelKey (description))
            builds.add (entry.description);

    if (builds.isEmpty())
        builds.add (description);

    pool.addJob ([description, repository, folder, builds, settings = pluginScanner.getSettings(),
                  safe = juce::Component::SafePointer<VibeCheckTab> (this), &cancel = sourceCancel]
    {
        const auto report = repository.isValid()
                                ? vibecheck::inspectRepository (repository, cancel, [safe] (const juce::String& text)
                                                                {
                                                                    juce::MessageManager::callAsync ([safe, text]
                                                                    {
                                                                        if (safe != nullptr && safe->sourceRunning)
                                                                        {
                                                                            safe->headerFacts = text;
                                                                            safe->repaint();
                                                                        }
                                                                    });
                                                                })
                                : vibecheck::inspectSourceFolder (folder);

        // Where each build's file sits decides the key its result is stored under.
        std::vector<std::pair<juce::PluginDescription, juce::int64>> targets;

        if (report.ok)
        {
            auto inspector = vibecheck::createBinaryInspector();

            for (const auto& build : builds)
            {
                const auto bundle = inspector->locate (build);
                targets.emplace_back (build, bundle.exists() ? bundle.getLastModificationTime().toMilliseconds() : juce::int64 (0));
            }
        }

        juce::MessageManager::callAsync ([safe, description, report, targets, settings]
        {
            if (safe == nullptr)
                return;

            safe->sourceRunning = false;
            safe->busy = false;
            safe->busyBar.setActive (safe->sweeping);
            safe->sourceButton.setEnabled (safe->hasCurrent);

            if (! report.ok)
            {
                safe->headerFacts = "Source check did not finish: " + report.error;
                safe->repaint();
                return;
            }

            for (const auto& [build, modified] : targets)
                vibecheck::storeSource (settings, build, modified, report);

            // Weigh the library again so every build of this plugin shows the new score in the list;
            // everything else comes straight from the cache.
            safe->sweepLibrary();

            if (safe->currentDescription.fileOrIdentifier == description.fileOrIdentifier && safe->currentDescription.name == description.name)
                safe->inspect (description);
        });
    });
}

void VibeCheckTab::runSourceDemo (const juce::String& query, const juce::String& folderOrAddress)
{
    sourceAfterInspect = folderOrAddress;
    inspectQuery (query);
}

void VibeCheckTab::show (const vibecheck::VibeReport& report, const juce::String& binaryDetails)
{
    headerFacts = binaryDetails.replaceCharacter ('\n', ' ');
    detail->setReport (report);
    detailViewport.setViewPosition (0, 0);
    repaint();
}

void VibeCheckTab::confirmUninstall()
{
    if (! hasCurrent)
        return;

    const auto description = currentDescription;
    const juce::File file (description.fileOrIdentifier);

    const auto where = file.exists() ? file.getFullPathName() : description.fileOrIdentifier;

    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle ("Move " + description.name + " to the Trash?")
                                      .withMessage ("This moves the plugin file to the Trash:\n\n" + where
                                                    + "\n\nYou can put it back from the Trash. Projects that use it will report it as missing.")
                                      .withButton ("Move to Trash")
                                      .withButton ("Cancel"),
                                  [safe = juce::Component::SafePointer<VibeCheckTab> (this), description, file] (int result)
                                  {
                                      if (safe == nullptr || result != 1)
                                          return;

                                      if (file.exists() && file.moveToTrash())
                                      {
                                          safe->pluginScanner.getKnownPluginList().removeType (description);
                                          safe->hasCurrent = false;
                                          safe->updateButton.setEnabled (false);
                                          safe->uninstallButton.setEnabled (false);
                                          safe->deepButton.setEnabled (false);
                                          safe->sourceButton.setEnabled (false);
                                          safe->labelBox.setVisible (false);
                                          safe->headerTitle = description.name + " moved to the Trash";
                                          safe->headerSub = {};
                                          safe->headerFacts = {};
                                          safe->sweepLibrary();
                                      }
                                      else
                                      {
                                          safe->headerFacts = "Could not move it. It may be in a protected folder, or it is not a file on disk.";
                                      }

                                      safe->repaint();
                                  });
}

void VibeCheckTab::paint (juce::Graphics& g)
{
    const auto& p = mbs::theme();

    mbs::drawCard (g, tableCard.toFloat());
    mbs::drawCard (g, detailCard.toFloat());

    // Detail header: which plugin this is.
    auto head = detailCard.reduced (18, 14).removeFromTop (58);

    if (headerTitle.isEmpty())
    {
        g.setColour (p.inkMuted);
        g.setFont (mbs::brandFont (11.0f, true));
        mbs::drawTracked (g, "Evidence", head.removeFromTop (20), 1.5f, juce::Justification::centredLeft);
        return;
    }

    // The label menu sits at the top right, so the title and maker stop short of it.
    const auto roomForLabel = labelBox.isVisible() ? labelBox.getWidth() + 10 : 0;

    g.setColour (p.ink);
    g.setFont (mbs::brandFont (17.0f, true));
    g.drawText (headerTitle, head.removeFromTop (24).withTrimmedRight (roomForLabel), juce::Justification::centredLeft, true);
    g.setColour (p.inkMuted);
    g.setFont (mbs::brandFont (12.5f));
    g.drawText (headerSub, head.removeFromTop (18).withTrimmedRight (roomForLabel), juce::Justification::centredLeft, true);
    g.setColour (p.inkFaint);
    g.setFont (mbs::monoFont (10.5f));
    g.drawText (headerFacts, head, juce::Justification::centredLeft, true);

    g.setColour (p.line);
    g.fillRect (detailCard.getX() + 18, detailCard.getY() + 14 + 62, detailCard.getWidth() - 36, 1);
}

void VibeCheckTab::resized()
{
    auto area = getLocalBounds();

    auto tiles = area.removeFromTop (98);
    const auto tileWidth = (tiles.getWidth() - 3 * mbs::gutter) / 4;

    for (auto* tile : { &weighedTile, &flaggedTile, &averageTile, &unreadableTile })
    {
        tile->setBounds (tiles.removeFromLeft (tileWidth));
        tiles.removeFromLeft (mbs::gutter);
    }

    area.removeFromTop (mbs::gutter);

    detailCard = area.removeFromRight (juce::jlimit (380, 470, area.getWidth() * 36 / 100));
    area.removeFromRight (mbs::gutter);
    tableCard = area;

    // --- Library card -----------------------------------------------------------------------
    {
        auto inner = tableCard.reduced (14, 14);
        auto top = inner.removeFromTop (36);
        rescanButton.setBounds (top.removeFromRight (112));
        top.removeFromRight (8);
        exportButton.setBounds (top.removeFromRight (104));
        top.removeFromRight (10);
        searchBox.setBounds (top);

        inner.removeFromTop (8);
        busyBar.setBounds (inner.removeFromTop (3));
        inner.removeFromTop (6);
        table.setBounds (inner);
    }

    // --- Detail card ------------------------------------------------------------------------
    {
        auto inner = detailCard.reduced (18, 14);
        inner.removeFromTop (58 + 10);

        // Two rows: the two checks that add evidence, then the two housekeeping buttons.
        auto housekeeping = inner.removeFromBottom (36);
        uninstallButton.setBounds (housekeeping.removeFromRight (housekeeping.getWidth() / 2 - 4));
        housekeeping.removeFromRight (8);
        updateButton.setBounds (housekeeping);

        inner.removeFromBottom (8);
        auto checks = inner.removeFromBottom (36);
        deepButton.setBounds (checks.removeFromRight (checks.getWidth() / 2 - 4));
        checks.removeFromRight (8);
        sourceButton.setBounds (checks);

        labelBox.setBounds (detailCard.getRight() - 18 - 172, detailCard.getY() + 14, 172, 28);

        inner.removeFromBottom (8);
        detailViewport.setBounds (inner.expanded (6, 0));
        detail->setAvailableWidth (detailViewport.getWidth() - detailViewport.getScrollBarThickness() - 6);
        detail->setMinimumHeight (inner.getHeight());
    }
}
