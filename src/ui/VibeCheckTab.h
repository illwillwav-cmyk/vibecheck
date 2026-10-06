#pragma once

#include <JuceHeader.h>

#include "host/PluginScanner.h"
#include "ui/Widgets.h"
#include "vibecheck/BehaviourCache.h"
#include "vibecheck/Export.h"
#include "vibecheck/Heuristics.h"
#include "vibecheck/Labels.h"
#include "vibecheck/LibrarySweep.h"

/** The library, weighed.

    Every plugin is scored in the background and listed with its result, so the question the app
    asks can be answered about a whole collection rather than one plugin at a time. Picking a row
    shows the evidence behind that one. */
class VibeCheckTab final : public juce::Component,
                           private juce::TableListBoxModel,
                           private juce::Timer
{
public:
    explicit VibeCheckTab (PluginScanner& scanner);
    ~VibeCheckTab() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void lookAndFeelChanged() override;

    /** Weighs the whole library. Cached results make this cheap after the first run. */
    void sweepLibrary();

    /** Inspects one plugin by name or path, without anyone clicking. Used by --vibedemo. */
    void runDemo (const juce::String& query, bool alsoDeepCheck = false);

    /** Reads a plugin's source without the dialog, then shows the plugin. Used by --vibedemo --source=. */
    void runSourceDemo (const juce::String& query, const juce::String& folderOrAddress);

    bool isIdle() const;

private:
    class Detail;

    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics&, int row, int columnId, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRow) override;
    void sortOrderChanged (int newSortColumnId, bool isForwards) override;

    void applyFilter();
    void restoreSelection();
    void updateSummary();
    void inspectQuery (const juce::String& query);
    void inspect (juce::PluginDescription description);
    void show (const vibecheck::VibeReport& report, const juce::String& binaryDetails);
    void confirmUninstall();
    void runDeepCheck();
    void runSourceCheck();
    void startSourceJob (juce::PluginDescription, vibecheck::RepositoryRef, juce::File folder);
    void labelChanged();
    void exportLibrary();

    /** Brings the table row for a plugin in line with a fresh report, so the list and the detail
        panel can never disagree about the same plugin. */
    void applyReportToRow (const juce::PluginDescription&, const vibecheck::VibeReport&, bool deep);
    const vibecheck::SweepEntry* entryForRow (int row) const;

    void timerCallback() override;

    PluginScanner& pluginScanner;

    std::vector<vibecheck::SweepEntry> entries;
    juce::Array<int> visibleRows;
    vibecheck::SweepSummary summary;

    mbs::StatTile weighedTile { "Plugins weighed" }, flaggedTile { "Read as machine-made" },
                  averageTile { "Average vibe" }, unreadableTile { "Unreadable" };

    mbs::SearchField searchBox;
    mbs::IconTextButton rescanButton { "Rescan", mbs::Icon::refresh };
    mbs::IconTextButton updateButton { "Updates", mbs::Icon::external };
    mbs::IconTextButton uninstallButton { "Uninstall", mbs::Icon::trash };
    mbs::IconTextButton deepButton { "Deep check", mbs::Icon::gauge };
    mbs::IconTextButton exportButton { "Export", mbs::Icon::external };
    std::unique_ptr<juce::FileChooser> exportChooser;
    std::atomic<bool> deepCancel { false };
    bool deepRunning = false, deepAfterInspect = false;

    mbs::IconTextButton sourceButton { "Source check", mbs::Icon::search };
    std::unique_ptr<juce::FileChooser> sourceChooser;
    std::atomic<bool> sourceCancel { false };
    bool sourceRunning = false;
    juce::String sourceAfterInspect;

    juce::ComboBox labelBox;
    std::map<juce::String, vibecheck::Label> labels;
    mbs::BusyBar busyBar;
    juce::TableListBox table;
    juce::Viewport detailViewport;
    std::unique_ptr<Detail> detail;

    juce::PluginDescription currentDescription;
    bool hasCurrent = false;
    juce::String headerTitle, headerSub, headerFacts;

    vibecheck::LibrarySweep sweep;
    juce::ThreadPool pool { 1 };
    bool busy = false, sweeping = false, suppressSelection = false;
    float animatedListProgress = 1.0f;
    int sortColumn = 4;
    bool sortForwards = false;
    int sweepDone = 0, sweepTotal = 0;

    juce::Rectangle<int> tableCard, detailCard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VibeCheckTab)
};
