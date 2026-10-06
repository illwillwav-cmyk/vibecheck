#include "ui/PerformanceTab.h"

#include "audio/OfflineRenderer.h"
#include "audio/TestSignal.h"
#include "host/PluginLoader.h"
#include "ui/Theme.h"

#ifdef __APPLE__
#include <mach/mach.h>
static size_t getMemoryUsageBytes()
{
    struct mach_task_basic_info info;
    mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;

    if (task_info (mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t) &info, &infoCount) != KERN_SUCCESS)
        return 0;

    return info.resident_size;
}
#else
static size_t getMemoryUsageBytes() { return 0; }
#endif

namespace
{
const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));

constexpr int numRates = 3, numBlocks = 6;
constexpr double rates[numRates] = { 44100.0, 48000.0, 96000.0 };
constexpr int blocks[numBlocks] = { 64, 128, 256, 512, 1024, 2048 };

/** A core running a DAW needs headroom for everything else on it. Past this a session starts to
    risk dropouts. */
constexpr double budgetPercent = 70.0;

int rateIndexFor (int id) { for (int i = 0; i < numRates; ++i) if ((int) rates[i] == id) return i; return 1; }
int blockIndexFor (int id) { for (int i = 0; i < numBlocks; ++i) if (blocks[i] == id) return i; return 3; }

/** Green under 10% of a core per instance, amber to 30%, red beyond. */
juce::Colour loadColour (double percent)
{
    const auto& p = mbs::theme();
    return percent < 10.0 ? p.good : percent < 30.0 ? p.warn : p.bad;
}

juce::String loadText (double percent)
{
    return mbs::num (percent, percent < 1.0 ? 2 : 1);
}

juce::String rateText (double rate) { return mbs::num (rate / 1000.0, 1) + " kHz"; }
} // namespace

/** Every measured combination, held on the message thread. */
struct PerformanceTab::Grid
{
    double cpu[numRates][numBlocks] = {};      ///< Per-instance percent of one core, best of several passes.
    double worstCpu[numRates][numBlocks] = {}; ///< Slowest pass, to show how steady the reading was.
    int latency[numRates][numBlocks] = {};
    bool done[numRates][numBlocks] = {};
    int doneCount = 0;
    int currentRate = -1, currentBlock = -1;
    bool finished = false, stopped = false;
    juce::String error;

    void clear() { *this = Grid(); }
    bool has (int r, int b) const { return done[r][b]; }

    bool noisy (int r, int b) const
    {
        return done[r][b] && cpu[r][b] > 0.0 && (worstCpu[r][b] - cpu[r][b]) / cpu[r][b] > 0.5;
    }

    int noisyCount() const
    {
        int n = 0;
        for (int r = 0; r < numRates; ++r) for (int b = 0; b < numBlocks; ++b) n += noisy (r, b) ? 1 : 0;
        return n;
    }
};

// --- The answer ---------------------------------------------------------------------------------

class PerformanceTab::Results final : public juce::Component
{
public:
    Results (Grid& gridToShow, const juce::String& nameToShow)
        : grid (gridToShow), name (nameToShow)
    {
        for (auto* tile : { &perInstance, &session, &capacity, &latency, &memory })
            addAndMakeVisible (tile);

        addAndMakeVisible (curve);
        curve.setAxes ({ 0.0, 20.0, false, "instances", {}, false }, { 0.0, 160.0, false, {}, " %", false });
        curve.setPlaceholder ("Run the benchmark to see how load grows with each instance you add");
        setMouseCursor (juce::MouseCursor::NormalCursor);
        refresh();
    }

    int rate = 1, block = 3, instances = 1;
    double memoryMb = 0.0;
    std::function<void (int, int)> onPick;

    int heightFor (int viewportHeight) const
    {
        return juce::jmax (viewportHeight, heroHeight + tilesHeight + heatHeight + 230 + 3 * mbs::gutter);
    }

    /** Recomputes everything shown from the measured grid and the chosen session. */
    void refresh()
    {
        const auto& p = mbs::theme();
        const auto have = grid.has (rate, block);
        const auto load = have ? grid.cpu[rate][block] : 0.0;
        const auto total = load * instances;

        if (! have)
        {
            perInstance.set ("-", "CPU per instance");
            perInstance.setMeter (-1.0f);
            session.set ("-", "Whole session");
            session.setMeter (-1.0f);
            capacity.set ("-", "Copies per core");
            latency.set ("-", "Added delay");
        }
        else
        {
            perInstance.set (loadText (load) + "%", "of one core", loadColour (load));
            perInstance.setMeter ((float) juce::jmin (1.0, load / 100.0));

            const auto sessionColour = total < budgetPercent ? p.good : total < 100.0 ? p.warn : p.bad;
            session.set (mbs::num (total, total < 10.0 ? 1 : 0) + "%",
                         juce::String (instances) + (instances == 1 ? " copy" : " copies") + " together", sessionColour);
            session.setMeter ((float) juce::jmin (1.0, total / 100.0));

            const auto fit = load > 0.0 ? (int) std::floor (budgetPercent / load) : 0;
            capacity.set (fit >= 1000 ? "1000+" : mbs::num (fit), "stay under " + mbs::num (budgetPercent, 0) + "%",
                          fit >= instances ? p.good : fit >= 1 ? p.warn : p.bad);

            const auto samples = grid.latency[rate][block];
            const auto ms = 1000.0 * samples / rates[rate];
            latency.set (samples == 0 ? "None" : mbs::num (ms, 2) + " ms",
                         samples == 0 ? "reports none" : mbs::num (samples) + " samples");
        }

        memory.set (memoryMb > 0.05 ? "~" + mbs::num (memoryMb, 1) + " MB" : "< 0.1 MB", "added on load");

        // The curve: total load for 1, 2, 3... instances, against the budget and a full core.
        if (have && load > 0.0)
        {
            const auto xMax = (double) juce::jlimit (4, 400, (int) std::ceil (150.0 / load));
            curve.setAxes ({ 0.0, xMax, false, "instances", {}, false }, { 0.0, 160.0, false, {}, " %", false });

            GraphComponent::Trace line, budget, full, mine;
            line.name = "Load"; line.series = 0; line.filled = true;

            for (int n = 0; n <= 120; ++n)
            {
                const auto x = xMax * n / 120.0;
                line.x.push_back (x);
                line.y.push_back (juce::jmin (160.0, load * x));
            }

            budget.name = mbs::num (budgetPercent, 0) + "% budget"; budget.series = 3; budget.dashed = true;
            budget.x = { 0.0, xMax }; budget.y = { budgetPercent, budgetPercent };
            full.name = "One core"; full.series = 2; full.dashed = true; full.color = p.bad;
            full.x = { 0.0, xMax }; full.y = { 100.0, 100.0 };
            mine.name = "Your session"; mine.series = 1; mine.dashed = true;
            mine.x = { (double) instances, (double) instances };
            mine.y = { 0.0, juce::jmin (160.0, total) };

            curve.setTraces ({ line, budget, full, mine });
        }
        else
        {
            curve.clearTraces();
        }

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = mbs::theme();
        const auto have = grid.has (rate, block);
        const auto load = have ? grid.cpu[rate][block] : 0.0;
        const auto total = load * instances;

        paintHero (g, have, load, total);

        // --- Heat map --------------------------------------------------------------------------
        {
            auto inner = mbs::drawTitledCard (g, heatCard, "CPU load per instance", "click any cell to try that setup");
            const auto labelWidth = 74, headerHeight = 26, footerHeight = 52;
            const auto rowHeight = juce::jlimit (34, 52, (inner.getHeight() - headerHeight - footerHeight) / numRates);
            const auto cellWidth = (inner.getWidth() - labelWidth) / numBlocks;

            auto header = inner.removeFromTop (headerHeight).withTrimmedLeft (labelWidth);
            g.setColour (p.inkFaint);
            g.setFont (mbs::brandFont (10.5f, true));

            for (int b = 0; b < numBlocks; ++b)
                mbs::drawTracked (g, juce::String (blocks[b]), header.removeFromLeft (cellWidth), 1.0f, juce::Justification::centred);

            heatOrigin = inner.getPosition().translated (labelWidth, 0);
            heatCell = { cellWidth, rowHeight };

            for (int r = 0; r < numRates; ++r)
            {
                auto row = inner.removeFromTop (rowHeight);
                g.setColour (r == rate ? p.ink : p.inkMuted);
                g.setFont (mbs::brandFont (12.5f, r == rate));
                g.drawText (rateText (rates[r]), row.removeFromLeft (labelWidth), juce::Justification::centredLeft);

                for (int b = 0; b < numBlocks; ++b)
                    paintCell (g, row.removeFromLeft (cellWidth).reduced (3), r, b);
            }

            // Legend and the sentence about whichever cell is hovered or selected.
            auto footer = inner.removeFromTop (footerHeight).withTrimmedTop (8);
            auto legend = footer.removeFromTop (18);
            const auto swatch = [&] (juce::Colour c, const juce::String& text, int width)
            {
                auto box = legend.removeFromLeft (width);
                g.setColour (c.withAlpha (0.25f));
                g.fillRoundedRectangle (box.removeFromLeft (14).withSizeKeepingCentre (12, 12).toFloat(), 3.0f);
                g.setColour (c);
                g.drawRoundedRectangle (juce::Rectangle<float> ((float) box.getX() - 14.0f, (float) box.getCentreY() - 6.0f, 12.0f, 12.0f), 3.0f, 1.0f);
                g.setColour (p.inkMuted);
                g.setFont (mbs::brandFont (11.5f));
                g.drawText (text, box.withTrimmedLeft (4), juce::Justification::centredLeft);
            };

            swatch (p.good, "under 10%: light", 130);
            swatch (p.warn, "10 to 30%: moderate", 150);
            swatch (p.bad, "over 30%: heavy", 130);

            if (grid.noisyCount() > 0)
            {
                g.setColour (p.warn);
                g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ (float) legend.getX() + 8.0f, (float) legend.getCentreY() }));
                g.setColour (p.inkMuted);
                g.drawText ("noisy reading, treat as approximate", legend.withTrimmedLeft (20), juce::Justification::centredLeft);
            }

            g.setColour (p.inkFaint);
            g.setFont (mbs::brandFont (12.0f));
            g.drawFittedText (cellSentence(), footer, juce::Justification::topLeft, 2);
        }

        // --- Curve card ------------------------------------------------------------------------
        mbs::drawTitledCard (g, curveCard, "Load as instances grow", "one core, at your buffer size");

        // --- Latency card ----------------------------------------------------------------------
        {
            auto inner = mbs::drawTitledCard (g, latencyCard, "Latency");

            if (grid.doneCount == 0)
            {
                g.setColour (p.inkFaint);
                g.setFont (mbs::brandFont (13.0f));
                g.drawFittedText ("Latency is the delay a plugin adds, measured in samples. It appears here after the benchmark.",
                                  inner, juce::Justification::topLeft, 4);
                return;
            }

            // One row per sample rate, at the chosen buffer size.
            g.setColour (p.inkFaint);
            g.setFont (mbs::brandFont (10.5f, true));
            mbs::drawTracked (g, "Sample rate", inner.removeFromTop (20).removeFromLeft (110), 1.2f, juce::Justification::centredLeft);

            for (int r = 0; r < numRates; ++r)
            {
                auto row = inner.removeFromTop (30);
                const auto ok = grid.has (r, block);
                g.setColour (r == rate ? p.ink : p.inkMuted);
                g.setFont (mbs::brandFont (13.0f, r == rate));
                g.drawText (rateText (rates[r]), row.removeFromLeft (76), juce::Justification::centredLeft);
                g.setFont (mbs::monoFont (12.0f));
                g.setColour (p.ink);
                g.drawText (ok ? mbs::num (grid.latency[r][block]) + " smp" : juce::String ("-"), row.removeFromLeft (74), juce::Justification::centredLeft);
                g.setColour (p.inkFaint);
                g.drawText (ok ? mbs::num (1000.0 * grid.latency[r][block] / rates[r], 2) + " ms" : juce::String(), row, juce::Justification::centredLeft);
            }

            // Does it depend on the buffer size?
            int smallest = 1 << 30, largest = 0;

            for (int b = 0; b < numBlocks; ++b)
                if (grid.has (rate, b)) { smallest = juce::jmin (smallest, grid.latency[rate][b]); largest = juce::jmax (largest, grid.latency[rate][b]); }

            inner.removeFromTop (8);
            g.setColour (p.inkMuted);
            g.setFont (mbs::brandFont (12.5f));
            g.drawFittedText (largest == 0 ? "This plugin adds no delay of its own, so your DAW has nothing to compensate for."
                              : smallest == largest ? "The same at every buffer size. A DAW delays the other tracks by this much to keep them lined up."
                              : "It changes with buffer size, from " + mbs::num (smallest) + " to " + mbs::num (largest)
                                    + " samples. A DAW delays the other tracks by this much to keep them lined up.",
                              inner, juce::Justification::topLeft, 5);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        heroCard = area.removeFromTop (heroHeight);
        area.removeFromTop (mbs::gutter);

        auto tiles = area.removeFromTop (tilesHeight);
        const auto tileWidth = (tiles.getWidth() - 4 * mbs::gutter) / 5;

        for (auto* tile : { &perInstance, &session, &capacity, &latency, &memory })
        {
            tile->setBounds (tiles.removeFromLeft (tileWidth));
            tiles.removeFromLeft (mbs::gutter);
        }

        area.removeFromTop (mbs::gutter);
        heatCard = area.removeFromTop (heatHeight);
        area.removeFromTop (mbs::gutter);

        latencyCard = area.removeFromRight (juce::jmin (300, area.getWidth() / 3));
        area.removeFromRight (mbs::gutter);
        curveCard = area;
        curve.setBounds (curveCard.withTrimmedTop (38).reduced (10, 8));
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto cell = cellAt (e.getPosition());

        if (cell != hover)
        {
            hover = cell;
            setMouseCursor (cell >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint (heatCard);
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hover != -1)
        {
            hover = -1;
            repaint (heatCard);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto cell = cellAt (e.getPosition());

        if (cell >= 0 && onPick != nullptr)
            onPick (cell / numBlocks, cell % numBlocks);
    }

private:
    int cellAt (juce::Point<int> point) const
    {
        const auto local = point - heatOrigin;

        if (local.x < 0 || local.y < 0 || heatCell.x <= 0)
            return -1;

        const auto column = local.x / heatCell.x, row = local.y / heatCell.y;
        return (column < numBlocks && row < numRates) ? row * numBlocks + column : -1;
    }

    void paintCell (juce::Graphics& g, juce::Rectangle<int> box, int r, int b) const
    {
        const auto& p = mbs::theme();
        const auto isSelected = r == rate && b == block;
        const auto isHovered = hover == r * numBlocks + b;
        const auto bounds = box.toFloat();

        if (! grid.has (r, b))
        {
            const auto measuring = r == grid.currentRate && b == grid.currentBlock;
            g.setColour (p.cardHi.withAlpha (0.6f));
            g.fillRoundedRectangle (bounds, 8.0f);
            g.setColour (measuring ? p.accent : p.line);
            g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, measuring ? 2.0f : 1.0f);
            g.setColour (measuring ? p.accent : p.inkFaint);
            g.setFont (mbs::brandFont (11.0f));
            g.drawText (measuring ? "measuring" : "-", box, juce::Justification::centred);
            return;
        }

        const auto colour = loadColour (grid.cpu[r][b]);
        g.setColour (colour.withAlpha (isSelected ? 0.30f : isHovered ? 0.24f : 0.15f));
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (isSelected ? p.ink : colour.withAlpha (isHovered ? 0.9f : 0.5f));
        g.drawRoundedRectangle (bounds.reduced (isSelected ? 1.0f : 0.5f), 8.0f, isSelected ? 2.0f : 1.0f);

        g.setColour (p.ink);
        g.setFont (mbs::monoFont (13.5f));
        g.drawText (loadText (grid.cpu[r][b]), box, juce::Justification::centred);

        if (grid.noisy (r, b))
        {
            g.setColour (p.warn);
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ bounds.getRight() - 9.0f, bounds.getY() + 9.0f }));
        }
    }

    juce::String cellSentence() const
    {
        const auto r = hover >= 0 ? hover / numBlocks : rate;
        const auto b = hover >= 0 ? hover % numBlocks : block;

        if (! grid.has (r, b))
            return grid.doneCount == 0 ? "Each cell is one measured setup: a sample rate down the side, a buffer size across the top."
                                       : "Not measured yet.";

        const auto bufferMs = 1000.0 * blocks[b] / rates[r];
        auto text = rateText (rates[r]) + " with " + juce::String (blocks[b]) + " samples (" + mbs::num (bufferMs, 1) + " ms of audio per buffer): "
                    + loadText (grid.cpu[r][b]) + "% of a core per instance";

        if (grid.noisy (r, b))
            text += ". The slowest pass was " + loadText (grid.worstCpu[r][b]) + "%, so treat this as approximate";

        return text + ".";
    }

    void paintHero (juce::Graphics& g, bool have, double load, double total) const
    {
        const auto& p = mbs::theme();
        mbs::drawCard (g, heroCard.toFloat());
        auto inner = heroCard.reduced (22, 18);

        // Before there is data, say what will happen rather than show empty numbers.
        if (! have)
        {
            g.setColour (p.ink);
            g.setFont (mbs::brandFont (20.0f, true));
            g.drawText (grid.doneCount > 0 ? "Measuring..." : "How heavy is this plugin?", inner.removeFromTop (30), juce::Justification::centredLeft);
            inner.removeFromTop (6);
            g.setColour (p.inkMuted);
            g.setFont (mbs::brandFont (14.0f));
            g.drawFittedText (grid.doneCount > 0
                                  ? "Every combination of buffer size and sample rate is being timed. The answer fills in as soon as your setup has been measured."
                                  : "Choose a plugin, tell VibeCheck about your session on the left, and run the benchmark. The answer appears here in plain words, with the numbers behind it underneath.",
                              inner.removeFromTop (60), juce::Justification::topLeft, 3);
            return;
        }

        const auto colour = loadColour (load);
        const auto fit = load > 0.0 ? (int) std::floor (budgetPercent / load) : 0;

        const auto word = load < 10.0 ? "Light" : load < 30.0 ? "Moderate" : "Heavy";
        auto head = inner.removeFromTop (34);
        mbs::drawPill (g, word, juce::Rectangle<float> (96.0f, 28.0f).withPosition ((float) head.getX(), (float) head.getY() + 3.0f), colour, false);
        g.setColour (p.ink);
        g.setFont (mbs::brandFont (20.0f, true));
        g.drawText (name + " uses " + loadText (load) + "% of a core per instance", head.withTrimmedLeft (112), juce::Justification::centredLeft, true);

        inner.removeFromTop (6);

        juce::String body = "At " + juce::String (blocks[block]) + " samples and " + rateText (rates[rate]) + ", ";

        if (instances == 1)
            body += fit >= 2 ? "about " + juce::String (fit) + " of these fit on one core before it reaches " + mbs::num (budgetPercent, 0) + "%."
                             : load < budgetPercent ? "a single instance fits, with little room for anything else on that core."
                                                    : "even one instance is more than a core can comfortably run.";
        else
            body += juce::String (instances) + " instances add up to " + mbs::num (total, total < 10.0 ? 1 : 0) + "% of one core"
                    + (total < budgetPercent ? ", which leaves plenty of headroom."
                       : total < 100.0 ? ", which is cutting it fine."
                                       : ". That is about " + juce::String ((int) std::ceil (total / budgetPercent)) + " cores' worth of work, so it only runs if your DAW spreads the tracks across cores.");

        // The one change that would help most: the next buffer size up, when it saves real load.
        if (b1 (block) < numBlocks && grid.has (rate, b1 (block)) && grid.cpu[rate][b1 (block)] < load * 0.8 && load >= 5.0)
            body += " A buffer of " + juce::String (blocks[b1 (block)]) + " samples would cut that to " + loadText (grid.cpu[rate][b1 (block)]) + "%.";

        g.setColour (p.inkMuted);
        g.setFont (mbs::brandFont (14.0f));
        const auto bodyArea = inner.removeFromTop (juce::jmax (40, inner.getHeight() - 42));
        g.drawFittedText (body, bodyArea, juce::Justification::topLeft, 3);

        // The headroom bar: this session against one whole core, with the budget marked.
        auto bar = inner.removeFromBottom (12).toFloat();
        g.setColour (p.line);
        g.fillRoundedRectangle (bar, 6.0f);

        const auto fraction = (float) juce::jmin (1.0, total / 100.0);
        const auto sessionColour = total < budgetPercent ? p.good : total < 100.0 ? p.warn : p.bad;
        g.setColour (sessionColour);
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (12.0f, bar.getWidth() * fraction)), 6.0f);

        const auto budgetX = bar.getX() + bar.getWidth() * (float) (budgetPercent / 100.0);
        g.setColour (p.ink.withAlpha (0.8f));
        g.fillRect (budgetX - 1.0f, bar.getY() - 4.0f, 2.0f, bar.getHeight() + 8.0f);

        g.setColour (p.inkFaint);
        g.setFont (mbs::monoFont (10.5f));
        g.drawText (juce::String (instances) + (instances == 1 ? " instance: " : " instances: ") + mbs::num (total, total < 10.0 ? 1 : 0) + "% of one core",
                    juce::Rectangle<int> ((int) bar.getX(), (int) bar.getBottom() + 3, 300, 14), juce::Justification::centredLeft);
        g.drawText (mbs::num (budgetPercent, 0) + "% budget", juce::Rectangle<int> ((int) budgetX - 60, (int) bar.getY() - 19, 120, 13), juce::Justification::centred);
        g.drawText ("one core", juce::Rectangle<int> ((int) bar.getRight() - 80, (int) bar.getBottom() + 3, 80, 14), juce::Justification::centredRight);
    }

    static int b1 (int block) { return block + 1; }

    static constexpr int heroHeight = 156, tilesHeight = 98, heatHeight = 232;

    Grid& grid;
    const juce::String& name;

    mbs::StatTile perInstance { "Per instance" }, session { "Your session" }, capacity { "Fit per core" },
                  latency { "Latency" }, memory { "Memory" };
    GraphComponent curve;

    juce::Rectangle<int> heroCard, heatCard, curveCard, latencyCard;
    juce::Point<int> heatOrigin, heatCell;
    int hover = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Results)
};

// --- The page -----------------------------------------------------------------------------------

PerformanceTab::PerformanceTab (PluginScanner& scanner)
    : pluginScanner (scanner), picker (scanner), grid (std::make_unique<Grid>())
{
    results = std::make_unique<Results> (*grid, pluginName);
    results->onPick = [this] (int r, int b) { pickCell (r, b); };

    addAndMakeVisible (picker);

    loadButton.onClick = [this] { loadPlugin(); };
    mbs::setButtonStyle (loadButton, mbs::ButtonStyle::primary);
    addAndMakeVisible (loadButton);

    loadedLabel.setFont (mbs::brandFont (12.5f));
    loadedLabel.setText ("Nothing loaded", juce::dontSendNotification);
    addAndMakeVisible (loadedLabel);

    mbs::styleCaption (blockCaption, "Buffer size");
    mbs::styleCaption (rateCaption, "Sample rate");
    mbs::styleCaption (instanceCaption, "Copies of the plugin");

    for (auto* label : { &blockCaption, &rateCaption, &instanceCaption })
        addAndMakeVisible (label);

    for (const auto block : blocks)
        blockChooser.addItem (juce::String (block) + " samples", block);

    for (const auto rate : rates)
        rateChooser.addItem (rateText (rate), (int) rate);

    for (int count : { 1, 2, 5, 10, 20, 50, 100 })
        instanceChooser.addItem (count == 1 ? "1 instance" : juce::String (count) + " instances", count);

    blockChooser.setSelectedId (512, juce::dontSendNotification);
    rateChooser.setSelectedId (48000, juce::dontSendNotification);
    instanceChooser.setSelectedId (1, juce::dontSendNotification);

    for (auto* box : { &blockChooser, &rateChooser, &instanceChooser })
    {
        box->onChange = [this] { scenarioChanged(); };
        addAndMakeVisible (box);
    }

    sessionHint.setFont (mbs::brandFont (12.0f));
    sessionHint.setText ("Your DAW's buffer size is in its audio settings. Change these any time: the answer updates without running again.",
                         juce::dontSendNotification);
    addAndMakeVisible (sessionHint);

    runButton.onClick = [this] { runOrStop(); };
    mbs::setButtonStyle (runButton, mbs::ButtonStyle::primary);
    addAndMakeVisible (runButton);

    statusLabel.setFont (mbs::monoFont (11.5f));
    statusLabel.setText ("Load a plugin to begin", juce::dontSendNotification);
    addAndMakeVisible (statusLabel);
    addAndMakeVisible (busyBar);

    resultsViewport.setViewedComponent (results.get(), false);
    resultsViewport.setScrollBarsShown (true, false);
    resultsViewport.setScrollBarThickness (8);
    addAndMakeVisible (resultsViewport);

    picker.onSelectionChanged = [this] { updateControls(); };

    restyle();
    scenarioChanged();
    updateControls();
}

PerformanceTab::~PerformanceTab()
{
    cancelFlag->store (true);
    pool.removeAllJobs (true, 60000);
    plugin.reset();
}

void PerformanceTab::restyle()
{
    loadedLabel.setColour (juce::Label::textColourId, mbs::theme().inkMuted);
    statusLabel.setColour (juce::Label::textColourId, mbs::theme().inkFaint);
    sessionHint.setColour (juce::Label::textColourId, mbs::theme().inkFaint);

    for (auto* label : { &blockCaption, &rateCaption, &instanceCaption })
        label->setColour (juce::Label::textColourId, mbs::theme().inkFaint);
}

void PerformanceTab::lookAndFeelChanged()
{
    restyle();

    if (results != nullptr)
        results->refresh();
}

void PerformanceTab::updateControls()
{
    juce::PluginDescription chosen;
    loadButton.setEnabled (! busy && picker.getSelected (chosen));
    runButton.setEnabled (plugin != nullptr && (running || ! busy));
    runButton.setButtonText (running ? "Stop" : "Run benchmark");
    runButton.icon = running ? mbs::Icon::refresh : mbs::Icon::play;
    mbs::setButtonStyle (runButton, running ? mbs::ButtonStyle::danger : mbs::ButtonStyle::primary);
    picker.setEnabled (! busy);
    busyBar.setActive (busy);
}

void PerformanceTab::scenarioChanged()
{
    results->rate = rateIndexFor (rateChooser.getSelectedId());
    results->block = blockIndexFor (blockChooser.getSelectedId());
    results->instances = juce::jmax (1, instanceChooser.getSelectedId());
    results->refresh();
}

void PerformanceTab::pickCell (int rateIndex, int blockIndex)
{
    rateChooser.setSelectedId ((int) rates[rateIndex], juce::dontSendNotification);
    blockChooser.setSelectedId (blocks[blockIndex], juce::dontSendNotification);
    scenarioChanged();
}

void PerformanceTab::runDemo (const juce::String& name)
{
    juce::PluginDescription description;

    if (! picker.selectByName (name, description))
    {
        statusLabel.setText ("No plugin matching \"" + name + "\"", juce::dontSendNotification);
        return;
    }

    autoRun = true;
    loadPlugin();
}

void PerformanceTab::loadPlugin()
{
    juce::PluginDescription description;

    if (! picker.getSelected (description))
        return;

    pool.removeAllJobs (true, 5000);
    plugin.reset();
    grid->clear();
    results->refresh();
    busy = true;
    loadedLabel.setText ("Loading " + description.name + "...", juce::dontSendNotification);
    statusLabel.setText ("Loading", juce::dontSendNotification);
    updateControls();

    pool.addJob ([this, description, safe = juce::Component::SafePointer<PerformanceTab> (this)]
    {
        const auto before = getMemoryUsageBytes();
        auto loaded = vibecheck::loadPlugin (pluginScanner.getFormatManager(), description, 48000.0, 512, 15000);
        const auto after = getMemoryUsageBytes();
        const auto megabytes = after > before ? (double) (after - before) / (1024.0 * 1024.0) : 0.0;

        auto* released = loaded.instance.release();
        const auto error = loaded.error;

        juce::MessageManager::callAsync ([safe, released, error, megabytes, description]
        {
            std::unique_ptr<juce::AudioPluginInstance> owned (released);

            if (safe == nullptr)
                return;

            safe->busy = false;
            safe->plugin = std::move (owned);

            if (safe->plugin != nullptr)
            {
                safe->pluginName = description.name;
                safe->ramFootprintMb = megabytes;
                safe->results->memoryMb = megabytes;
                safe->loadedLabel.setText (description.name + "\n" + description.pluginFormatName + dot + description.manufacturerName,
                                           juce::dontSendNotification);
                safe->statusLabel.setText ("Loaded. Describe your session, then run the benchmark.", juce::dontSendNotification);
                safe->scenarioChanged();
            }
            else
            {
                safe->loadedLabel.setText ("Could not load " + description.name, juce::dontSendNotification);
                safe->statusLabel.setText (error, juce::dontSendNotification);
            }

            safe->updateControls();

            if (std::exchange (safe->autoRun, false) && safe->plugin != nullptr)
                safe->startBenchmark();
        });
    });
}

void PerformanceTab::runOrStop()
{
    if (running)
    {
        cancelFlag->store (true);
        statusLabel.setText ("Stopping", juce::dontSendNotification);
        return;
    }

    startBenchmark();
}

void PerformanceTab::startBenchmark()
{
    if (plugin == nullptr || busy)
        return;

    grid->clear();
    results->refresh();

    busy = true;
    running = true;
    cancelFlag = std::make_shared<std::atomic<bool>> (false);
    statusLabel.setText ("Warming up", juce::dontSendNotification);
    updateControls();

    auto* instance = plugin.get();
    auto cancel = cancelFlag;

    pool.addJob ([instance, cancel, safe = juce::Component::SafePointer<PerformanceTab> (this)]
    {
        const auto post = [safe] (std::function<void (PerformanceTab&)> change)
        {
            juce::MessageManager::callAsync ([safe, action = std::move (change)]
            {
                if (safe != nullptr)
                    action (*safe);
            });
        };

        // A quick timing at the middle of the grid decides how long each pass can afford to be: a
        // light plugin gets long, steady passes, and a very heavy one short ones, so the whole
        // benchmark stays within a minute or so either way.
        double seconds = 3.0;
        {
            vibecheck::SignalSpec spec;
            spec.type = vibecheck::SignalType::whiteNoise;
            spec.sampleRate = 48000.0;
            spec.numSamples = 48000;
            spec.amplitudeDb = -18.0;

            vibecheck::RenderOptions options;
            options.sampleRate = 48000.0;
            options.blockSize = 512;

            const auto calibration = vibecheck::OfflineRenderer::render (*instance, vibecheck::generate (spec), options);
            const auto speed = calibration.ok ? calibration.timesFasterThanRealtime : 0.0;
            seconds = speed > 60.0 ? 3.0 : speed > 10.0 ? 2.0 : 1.0;
        }

        juce::String error;
        bool stopped = false;

        for (int r = 0; r < numRates && ! stopped; ++r)
        {
            // Noise rather than silence: a plugin that notices it has nothing to do and skips its
            // work would look far lighter than it is in a real session.
            vibecheck::SignalSpec spec;
            spec.type = vibecheck::SignalType::whiteNoise;
            spec.sampleRate = rates[r];
            spec.numSamples = (int) (rates[r] * seconds);
            spec.amplitudeDb = -18.0;
            const auto noise = vibecheck::generate (spec);

            for (int b = 0; b < numBlocks; ++b)
            {
                if (cancel->load())
                {
                    stopped = true;
                    break;
                }

                const auto step = r * numBlocks + b + 1;
                post ([r, b, step] (PerformanceTab& tab)
                {
                    tab.grid->currentRate = r;
                    tab.grid->currentBlock = b;
                    tab.statusLabel.setText ("Measuring " + juce::String (step) + " of " + juce::String (numRates * numBlocks) + ": "
                                                 + rateText (rates[r]) + ", " + juce::String (blocks[b]) + " samples",
                                             juce::dontSendNotification);
                    tab.results->refresh();
                });

                vibecheck::RenderOptions options;
                options.sampleRate = rates[r];
                options.blockSize = blocks[b];

                // The first pass warms caches and is thrown away. Of the rest, the fastest is
                // what the plugin costs in steady state and the slowest shows how steady that was.
                const auto warmup = vibecheck::OfflineRenderer::render (*instance, noise, options);

                if (! warmup.ok)
                {
                    error = warmup.error;
                    stopped = true;
                    break;
                }

                const auto passes = warmup.timesFasterThanRealtime > 30.0 ? 3 : warmup.timesFasterThanRealtime > 6.0 ? 2 : 1;
                double fastest = 0.0, slowest = 1.0e12;
                int latency = warmup.reportedLatencySamples;

                for (int pass = 0; pass < passes; ++pass)
                {
                    const auto result = vibecheck::OfflineRenderer::render (*instance, noise, options);

                    if (! result.ok || result.timesFasterThanRealtime <= 0.0)
                        continue;

                    fastest = juce::jmax (fastest, result.timesFasterThanRealtime);
                    slowest = juce::jmin (slowest, result.timesFasterThanRealtime);
                    latency = result.reportedLatencySamples;
                }

                if (fastest <= 0.0)
                {
                    fastest = warmup.timesFasterThanRealtime;
                    slowest = fastest;
                }

                const auto best = 100.0 / fastest, worst = 100.0 / slowest;

                post ([r, b, best, worst, latency] (PerformanceTab& tab)
                {
                    tab.grid->cpu[r][b] = best;
                    tab.grid->worstCpu[r][b] = worst;
                    tab.grid->latency[r][b] = latency;
                    tab.grid->done[r][b] = true;
                    ++tab.grid->doneCount;
                    tab.results->refresh();
                });
            }
        }

        post ([stopped, error] (PerformanceTab& tab)
        {
            tab.busy = false;
            tab.running = false;
            tab.grid->currentRate = tab.grid->currentBlock = -1;
            tab.grid->finished = ! stopped;
            tab.grid->stopped = stopped;
            tab.grid->error = error;
            tab.results->refresh();

            const auto noisy = tab.grid->noisyCount();
            tab.statusLabel.setText (error.isNotEmpty() ? "Render failed: " + error
                                     : stopped ? "Stopped. Cells measured so far are shown."
                                     : noisy > 0 ? "Done. " + juce::String (noisy) + (noisy == 1 ? " reading was" : " readings were")
                                                       + " noisy: close other apps and run again for steadier numbers."
                                                 : "Done. Click any cell to try another setup.",
                                     juce::dontSendNotification);
            tab.updateControls();
        });
    });
}

void PerformanceTab::paint (juce::Graphics& g)
{
    const auto& p = mbs::theme();

    const auto step = [&] (juce::Rectangle<int> card, int number, const juce::String& title, bool active)
    {
        mbs::drawCard (g, card.toFloat(), false);
        const auto inner = card.reduced (18, 14);
        const auto badge = juce::Rectangle<float> (22.0f, 22.0f).withPosition ((float) inner.getX(), (float) inner.getY() - 1.0f);

        g.setColour (active ? p.accent : p.cardHi);
        g.fillEllipse (badge);
        g.setColour (active ? p.onAccent() : p.inkMuted);
        g.setFont (mbs::brandFont (12.0f, true));
        g.drawText (juce::String (number), badge.toNearestInt(), juce::Justification::centred);

        g.setColour (p.ink);
        g.setFont (mbs::brandFont (14.0f, true));
        g.drawText (title, inner.getX() + 32, inner.getY() - 2, inner.getWidth() - 32, 24, juce::Justification::centredLeft);
    };

    step (pluginCard, 1, "Choose a plugin", plugin == nullptr);
    step (sessionCard, 2, "Describe your session", plugin != nullptr && ! grid->finished && grid->doneCount == 0);
    step (runCard, 3, "Run it", plugin != nullptr && (running || grid->doneCount == 0));
}

void PerformanceTab::resized()
{
    auto area = getLocalBounds();
    auto column = area.removeFromLeft (318);
    area.removeFromLeft (mbs::gutter);

    pluginCard = column.removeFromTop (232);
    {
        auto inner = pluginCard.reduced (18, 14).withTrimmedTop (30);
        picker.setBounds (inner.removeFromTop (PluginPicker::preferredHeight));
        inner.removeFromTop (10);
        auto row = inner.removeFromTop (40);
        loadButton.setBounds (row.removeFromLeft (92).withSizeKeepingCentre (92, 34));
        row.removeFromLeft (10);
        loadedLabel.setBounds (row);
    }

    column.removeFromTop (mbs::gutter);

    sessionCard = column.removeFromTop (300);
    {
        auto inner = sessionCard.reduced (18, 14).withTrimmedTop (30);
        const auto field = [&inner] (juce::Label& caption, juce::ComboBox& box)
        {
            caption.setBounds (inner.removeFromTop (16));
            box.setBounds (inner.removeFromTop (34));
            inner.removeFromTop (10);
        };

        field (rateCaption, rateChooser);
        field (blockCaption, blockChooser);
        field (instanceCaption, instanceChooser);
        sessionHint.setBounds (inner.removeFromTop (46));
    }

    column.removeFromTop (mbs::gutter);

    runCard = column.removeFromTop (juce::jmin (column.getHeight(), 142));
    {
        auto inner = runCard.reduced (18, 14).withTrimmedTop (30);
        runButton.setBounds (inner.removeFromTop (40));
        inner.removeFromTop (8);
        busyBar.setBounds (inner.removeFromTop (4));
        inner.removeFromTop (6);
        statusLabel.setBounds (inner.removeFromTop (34));
    }

    resultsViewport.setBounds (area);
    const auto needed = results->heightFor (area.getHeight());
    results->setSize (needed > area.getHeight() ? area.getWidth() - 12 : area.getWidth(), needed);
}
