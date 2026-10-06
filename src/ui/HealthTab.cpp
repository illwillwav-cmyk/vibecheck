#include "ui/HealthTab.h"

#include "host/PluginLoader.h"
#include "ui/Theme.h"

namespace
{
const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));

juce::Colour colourFor (vibecheck::Verdict verdict)
{
    const auto& p = mbs::theme();

    switch (verdict)
    {
        case vibecheck::Verdict::pass:    return p.good;
        case vibecheck::Verdict::warn:    return p.warn;
        case vibecheck::Verdict::fail:    return p.bad;
        case vibecheck::Verdict::info:    return p.accent2;
        case vibecheck::Verdict::pending: return p.inkFaint;
    }

    return p.inkFaint;
}
} // namespace

/** The verdict, the counts, and one row per test. */
class HealthTab::Results final : public juce::Component
{
public:
    std::vector<vibecheck::HealthTest> rows;
    juce::String pluginName, current;
    bool finished = false, stopped = false, instrument = false, running = false;
    double seconds = 0.0;

    int count (vibecheck::Verdict verdict) const
    {
        int n = 0;

        for (const auto& row : rows)
            n += row.verdict == verdict ? 1 : 0;

        return n;
    }

    int heightFor (int viewportHeight) const
    {
        return juce::jmax (viewportHeight, heroHeight + mbs::gutter + 56 + (int) rows.size() * rowHeight + 20);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = mbs::theme();
        auto area = getLocalBounds();
        const auto hero = area.removeFromTop (heroHeight);
        area.removeFromTop (mbs::gutter);

        // --- The verdict ------------------------------------------------------------------------
        mbs::drawCard (g, hero.toFloat());
        auto inner = hero.reduced (22, 18);

        const auto passed = count (vibecheck::Verdict::pass), warned = count (vibecheck::Verdict::warn),
                   failed = count (vibecheck::Verdict::fail), noted = count (vibecheck::Verdict::info);
        const auto done = passed + warned + failed + noted;
        const auto have = done > 0;

        juce::String headline = "Ready when you are";
        juce::Colour headlineColour = p.ink;

        if (have && ! running)
        {
            headline = failed > 0 ? "Has problems" : warned > 1 ? "Mostly healthy" : "Healthy";
            headlineColour = failed > 0 ? p.bad : warned > 1 ? p.warn : p.good;
        }
        else if (running)
        {
            headline = "Checking...";
        }

        auto top = inner.removeFromTop (38);
        auto counts = top.removeFromRight (360);

        g.setColour (headlineColour);
        g.setFont (mbs::brandFont (28.0f, true));
        g.drawText (headline, top, juce::Justification::centredLeft, true);

        const auto stat = [&] (const juce::String& label, int value, juce::Colour colour)
        {
            auto cell = counts.removeFromLeft (90);
            g.setColour (value > 0 ? colour : p.inkFaint);
            g.setFont (mbs::brandFont (24.0f, true));
            g.drawText (juce::String (value), cell.removeFromTop (28), juce::Justification::centredLeft);
            g.setColour (p.inkFaint);
            g.setFont (mbs::brandFont (10.5f, true));
            mbs::drawTracked (g, label, cell.removeFromTop (12), 1.2f, juce::Justification::centredLeft);
        };

        stat ("Passed", passed, p.good);
        stat ("Warnings", warned, p.warn);
        stat ("Failed", failed, p.bad);
        stat ("Notes", noted, p.accent2);

        inner.removeFromTop (4);
        g.setColour (p.inkMuted);
        g.setFont (mbs::brandFont (13.5f));

        const auto subtitle = ! have && ! running
                                ? (pluginName.isEmpty() ? juce::String ("Load a plugin, then run the check. It takes a few seconds and changes nothing on disk.")
                                                        : pluginName + (instrument ? " is an instrument, so it is played with MIDI." : " is an effect, so it is fed test audio.") + " Press Run health check.")
                                : running ? "Now: " + current
                                          : (stopped ? "Stopped early. " : "") + pluginName + dot + juce::String (done) + " of " + juce::String ((int) rows.size()) + " tests run in "
                                                + mbs::num (seconds, 1) + " s";

        g.drawFittedText (subtitle, inner.removeFromTop (38), juce::Justification::topLeft, 2);

        // A bar made of the verdicts, so the balance reads before any number does.
        auto bar = juce::Rectangle<float> ((float) inner.getX(), (float) hero.getBottom() - 30.0f, (float) inner.getWidth(), 8.0f);
        g.setColour (p.line);
        g.fillRoundedRectangle (bar, 4.0f);

        if (! rows.empty())
        {
            auto x = bar.getX();
            const auto unit = bar.getWidth() / (float) rows.size();

            for (const auto verdict : { vibecheck::Verdict::pass, vibecheck::Verdict::info, vibecheck::Verdict::warn, vibecheck::Verdict::fail })
            {
                const auto n = count (verdict);

                if (n == 0)
                    continue;

                g.setColour (colourFor (verdict));
                g.fillRoundedRectangle (juce::Rectangle<float> (x, bar.getY(), unit * (float) n - 2.0f, bar.getHeight()), 3.0f);
                x += unit * (float) n;
            }
        }

        // --- The tests --------------------------------------------------------------------------
        mbs::drawCard (g, area.toFloat());
        auto list = area.reduced (18, 14);
        g.setColour (p.inkMuted);
        g.setFont (mbs::brandFont (11.0f, true));
        mbs::drawTracked (g, "What was checked", list.removeFromTop (20), 1.5f, juce::Justification::centredLeft);
        list.removeFromTop (6);

        if (rows.empty())
        {
            mbs::drawEmptyState (g, list, mbs::Icon::check, "Nothing checked yet", "Load a plugin and the tests that apply to it are listed here.");
            return;
        }

        for (const auto& row : rows)
        {
            auto line = list.removeFromTop (rowHeight);
            const auto colour = colourFor (row.verdict);

            g.setColour (p.line.withAlpha (0.55f));
            g.fillRect (line.getX(), line.getBottom() - 1, line.getWidth(), 1);

            // The mark: a tick, a triangle, or a plain "i", on a disc of the verdict's colour.
            const auto disc = juce::Rectangle<float> (28.0f, 28.0f).withPosition ((float) line.getX(), (float) line.getY() + 12.0f);
            g.setColour (colour.withAlpha (row.verdict == vibecheck::Verdict::pending ? 0.12f : 0.18f));
            g.fillEllipse (disc);

            if (row.verdict == vibecheck::Verdict::pass)
                mbs::drawIcon (g, mbs::Icon::check, disc.reduced (7.0f), colour, 2.4f);
            else if (row.verdict == vibecheck::Verdict::warn || row.verdict == vibecheck::Verdict::fail)
                mbs::drawIcon (g, mbs::Icon::alert, disc.reduced (7.0f), colour, 2.2f);
            else if (row.verdict == vibecheck::Verdict::info)
            {
                g.setColour (colour);
                g.setFont (mbs::brandFont (14.0f, true));
                g.drawText ("i", disc.toNearestInt(), juce::Justification::centred);
            }
            else if (running && row.title == current)
                mbs::drawIcon (g, mbs::Icon::clock, disc.reduced (7.0f), p.accent, 2.0f);

            auto text = line.withTrimmedLeft (44).withTrimmedRight (64).reduced (0, 8);

            g.setColour (p.ink);
            g.setFont (mbs::brandFont (14.0f, true));
            g.drawText (row.title, text.removeFromTop (20), juce::Justification::centredLeft, true);

            g.setColour (row.verdict == vibecheck::Verdict::pending ? p.inkFaint : (row.verdict == vibecheck::Verdict::pass ? p.inkMuted : p.ink));
            g.setFont (mbs::brandFont (12.5f));
            const auto resultText = row.verdict == vibecheck::Verdict::pending ? (running && row.title == current ? juce::String ("Running now") : juce::String ("Waiting")) : row.result;
            const auto wraps = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), resultText) > (float) text.getWidth();
            g.drawFittedText (resultText, text.removeFromTop (wraps ? 36 : 20), juce::Justification::topLeft, 2, 1.0f);

            g.setColour (p.inkFaint);
            g.setFont (mbs::brandFont (11.5f));
            g.drawFittedText (row.explain, text.removeFromTop (rowHeight - 70), juce::Justification::topLeft, 2, 1.0f);

            if (row.verdict != vibecheck::Verdict::pending)
            {
                g.setColour (p.inkFaint);
                g.setFont (mbs::monoFont (10.5f));
                g.drawText (mbs::num (row.milliseconds, row.milliseconds < 100.0 ? 0 : 0) + " ms", line.removeFromRight (60), juce::Justification::centredRight);
            }
        }
    }

private:
    static constexpr int heroHeight = 150, rowHeight = 92;
};

HealthTab::HealthTab (PluginScanner& scanner)
    : pluginScanner (scanner), picker (scanner)
{
    results = std::make_unique<Results>();

    addAndMakeVisible (picker);

    loadButton.onClick = [this] { loadPlugin(); };
    mbs::setButtonStyle (loadButton, mbs::ButtonStyle::primary);
    addAndMakeVisible (loadButton);

    loadedLabel.setFont (mbs::brandFont (12.5f));
    loadedLabel.setJustificationType (juce::Justification::centredLeft);
    loadedLabel.setText ("Nothing loaded", juce::dontSendNotification);
    addAndMakeVisible (loadedLabel);

    runButton.onClick = [this] { runOrStop(); };
    mbs::setButtonStyle (runButton, mbs::ButtonStyle::primary);
    addAndMakeVisible (runButton);

    copyButton.onClick = [this] { copyReport(); };
    mbs::setButtonStyle (copyButton, mbs::ButtonStyle::ghost);
    addAndMakeVisible (copyButton);

    statusLabel.setFont (mbs::monoFont (11.5f));
    statusLabel.setText ("Load a plugin to begin", juce::dontSendNotification);
    addAndMakeVisible (statusLabel);
    addAndMakeVisible (busyBar);

    noteLabel.setFont (mbs::brandFont (12.0f));
    noteLabel.setJustificationType (juce::Justification::topLeft);
    noteLabel.setText ("These tests change the plugin's parameters while they run, then put them back. They measure robustness, not how it sounds. "
                       "Plugins run inside VibeCheck, so one that crashes will close it.",
                       juce::dontSendNotification);
    addAndMakeVisible (noteLabel);

    viewport.setViewedComponent (results.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    picker.onSelectionChanged = [this] { updateControls(); };

    restyle();
    updateControls();
}

HealthTab::~HealthTab()
{
    cancelFlag->store (true);
    pool.removeAllJobs (true, 60000);
    plugin.reset();
}

void HealthTab::restyle()
{
    loadedLabel.setColour (juce::Label::textColourId, mbs::theme().inkMuted);
    statusLabel.setColour (juce::Label::textColourId, mbs::theme().inkFaint);
    noteLabel.setColour (juce::Label::textColourId, mbs::theme().inkFaint);
}

void HealthTab::lookAndFeelChanged()
{
    restyle();
}

void HealthTab::updateControls()
{
    juce::PluginDescription chosen;
    loadButton.setEnabled (! busy && picker.getSelected (chosen));
    runButton.setEnabled (plugin != nullptr && (running || ! busy));
    runButton.setButtonText (running ? "Stop" : "Run health check");
    runButton.icon = running ? mbs::Icon::refresh : mbs::Icon::play;
    mbs::setButtonStyle (runButton, running ? mbs::ButtonStyle::danger : mbs::ButtonStyle::primary);
    copyButton.setEnabled (results->count (vibecheck::Verdict::pass) + results->count (vibecheck::Verdict::warn)
                               + results->count (vibecheck::Verdict::fail) + results->count (vibecheck::Verdict::info) > 0);
    picker.setEnabled (! busy);
    busyBar.setActive (busy);
    results->running = running;
    results->repaint();
}

void HealthTab::runDemo (const juce::String& name)
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

void HealthTab::loadPlugin()
{
    juce::PluginDescription description;

    if (! picker.getSelected (description))
        return;

    pool.removeAllJobs (true, 5000);
    plugin.reset();
    results->rows.clear();
    results->finished = results->stopped = false;
    busy = true;
    loadedLabel.setText ("Loading " + description.name + "...", juce::dontSendNotification);
    statusLabel.setText ("Loading", juce::dontSendNotification);
    updateControls();

    pool.addJob ([this, description, safe = juce::Component::SafePointer<HealthTab> (this)]
    {
        auto loaded = vibecheck::loadPlugin (pluginScanner.getFormatManager(), description, 48000.0, 512, 15000);
        auto* released = loaded.instance.release();
        const auto error = loaded.error;

        juce::MessageManager::callAsync ([safe, released, error, description]
        {
            std::unique_ptr<juce::AudioPluginInstance> owned (released);

            if (safe == nullptr)
                return;

            safe->busy = false;
            safe->plugin = std::move (owned);

            if (safe->plugin != nullptr)
            {
                safe->pluginName = description.name;
                safe->results->pluginName = description.name;
                safe->results->instrument = vibecheck::looksLikeInstrument (*safe->plugin);
                safe->results->rows = vibecheck::plannedHealthTests (safe->results->instrument);
                safe->loadedLabel.setText (description.name + "\n" + description.pluginFormatName + dot + description.manufacturerName
                                               + (safe->results->instrument ? dot + "instrument" : juce::String()),
                                           juce::dontSendNotification);
                safe->statusLabel.setText ("Loaded. Press Run health check.", juce::dontSendNotification);
            }
            else
            {
                safe->loadedLabel.setText ("Could not load " + description.name, juce::dontSendNotification);
                safe->statusLabel.setText (error, juce::dontSendNotification);
            }

            safe->resized();
            safe->updateControls();

            if (std::exchange (safe->autoRun, false) && safe->plugin != nullptr)
                safe->startRun();
        });
    });
}

void HealthTab::runOrStop()
{
    if (running)
    {
        cancelFlag->store (true);
        statusLabel.setText ("Stopping after this test", juce::dontSendNotification);
        return;
    }

    startRun();
}

void HealthTab::startRun()
{
    if (plugin == nullptr || busy)
        return;

    results->rows = vibecheck::plannedHealthTests (results->instrument);
    results->finished = results->stopped = false;
    results->seconds = 0.0;

    busy = true;
    running = true;
    cancelFlag = std::make_shared<std::atomic<bool>> (false);
    statusLabel.setText ("Running", juce::dontSendNotification);
    updateControls();

    auto* instance = plugin.get();
    auto cancel = cancelFlag;

    pool.addJob ([instance, cancel, safe = juce::Component::SafePointer<HealthTab> (this)]
    {
        const auto report = vibecheck::runHealthSuite (*instance, *cancel,
            [safe] (int index, int total, const vibecheck::HealthTest& test)
            {
                juce::MessageManager::callAsync ([safe, index, total, test]
                {
                    if (safe == nullptr)
                        return;

                    if (juce::isPositiveAndBelow (index, (int) safe->results->rows.size()))
                        safe->results->rows[(size_t) index] = test;

                    safe->statusLabel.setText (juce::String (index + 1) + " of " + juce::String (total) + " done", juce::dontSendNotification);
                    safe->updateControls();
                });
            },
            [safe] (const juce::String& next)
            {
                juce::MessageManager::callAsync ([safe, next]
                {
                    if (safe != nullptr)
                    {
                        safe->results->current = next;
                        safe->results->repaint();
                    }
                });
            });

        juce::MessageManager::callAsync ([safe, report]
        {
            if (safe == nullptr)
                return;

            safe->busy = false;
            safe->running = false;
            safe->results->finished = true;
            safe->results->stopped = report.stopped;
            safe->results->seconds = report.seconds;
            safe->statusLabel.setText (report.stopped ? "Stopped." : "Done in " + mbs::num (report.seconds, 1) + " s", juce::dontSendNotification);
            safe->updateControls();
        });
    });
}

void HealthTab::copyReport()
{
    vibecheck::HealthReport report;
    report.instrument = results->instrument;
    report.seconds = results->seconds;

    for (const auto& row : results->rows)
        if (row.verdict != vibecheck::Verdict::pending)
            report.tests.push_back (row);

    juce::SystemClipboard::copyTextToClipboard (report.toText (pluginName));
    statusLabel.setText ("Report copied to the clipboard", juce::dontSendNotification);
}

void HealthTab::paint (juce::Graphics& g)
{
    const auto& p = mbs::theme();

    const auto step = [&] (juce::Rectangle<int> card, int number, const juce::String& title, bool active)
    {
        mbs::drawCard (g, card.toFloat());
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
    step (runCard, 2, "Run the checks", plugin != nullptr && ! results->finished);

    mbs::drawCard (g, noteCard.toFloat());
    const auto head = noteCard.reduced (18, 14).removeFromTop (20);
    g.setColour (p.inkMuted);
    g.setFont (mbs::brandFont (11.0f, true));
    mbs::drawTracked (g, "Good to know", head, 1.5f, juce::Justification::centredLeft);
}

void HealthTab::resized()
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

    runCard = column.removeFromTop (178);
    {
        auto inner = runCard.reduced (18, 14).withTrimmedTop (30);
        runButton.setBounds (inner.removeFromTop (40));
        inner.removeFromTop (8);
        busyBar.setBounds (inner.removeFromTop (4));
        inner.removeFromTop (6);
        statusLabel.setBounds (inner.removeFromTop (20));
        inner.removeFromTop (4);
        copyButton.setBounds (inner.removeFromTop (30).removeFromLeft (120));
    }

    column.removeFromTop (mbs::gutter);

    noteCard = column.removeFromTop (juce::jmin (column.getHeight(), 168));
    noteLabel.setBounds (noteCard.reduced (18, 14).withTrimmedTop (28));

    viewport.setBounds (area);
    const auto needed = results->heightFor (area.getHeight());
    results->setSize (needed > area.getHeight() ? area.getWidth() - 12 : area.getWidth(), needed);
}
