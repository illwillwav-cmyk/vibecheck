#pragma once

#include <JuceHeader.h>

#include "host/PluginScanner.h"
#include "ui/PluginPicker.h"
#include "ui/Plots.h"
#include "ui/Widgets.h"

#include <atomic>

/** Benchmarks a plugin's CPU load and latency, then explains the result in terms of the session it
    will be used in.

    The page is a walk-through: choose a plugin, describe the session (buffer size, sample rate,
    how many instances), and read the answer. Every combination of buffer size and sample rate is
    measured once, so changing the session afterwards updates the answer instantly. */
class PerformanceTab final : public juce::Component
{
public:
    explicit PerformanceTab (PluginScanner& scanner);
    ~PerformanceTab() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void lookAndFeelChanged() override;

    bool isIdle() const { return ! busy && ! autoRun; }

    /** Loads a plugin by name and benchmarks it, without anyone clicking. Used by --perfdemo. */
    void runDemo (const juce::String& name);

private:
    struct Grid;
    class Results;

    void loadPlugin();
    void runOrStop();
    void startBenchmark();
    void scenarioChanged();
    void pickCell (int rateIndex, int blockIndex);
    void updateControls();
    void restyle();

    PluginScanner& pluginScanner;

    PluginPicker picker;
    mbs::IconTextButton loadButton { "Load", mbs::Icon::plug };
    mbs::IconTextButton runButton { "Run benchmark", mbs::Icon::play };
    juce::ComboBox blockChooser, rateChooser, instanceChooser;
    juce::Label blockCaption, rateCaption, instanceCaption, loadedLabel, statusLabel, sessionHint;
    mbs::BusyBar busyBar;

    std::unique_ptr<Grid> grid;
    std::unique_ptr<Results> results;
    juce::Viewport resultsViewport;

    juce::ThreadPool pool { 1 };
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::String pluginName;
    std::shared_ptr<std::atomic<bool>> cancelFlag { std::make_shared<std::atomic<bool>> (false) };
    bool busy = false, running = false, autoRun = false;
    double ramFootprintMb = 0.0;

    juce::Rectangle<int> pluginCard, sessionCard, runCard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PerformanceTab)
};
