#pragma once

#include <JuceHeader.h>

#include "analysis/HealthSuite.h"
#include "host/PluginScanner.h"
#include "ui/PluginPicker.h"
#include "ui/Widgets.h"

/** Robustness testing for a plugin, in plain language: does it start, stay finite, behave at any
    buffer size, keep its promises about latency, survive random settings and save its state. */
class HealthTab final : public juce::Component
{
public:
    explicit HealthTab (PluginScanner& scanner);
    ~HealthTab() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void lookAndFeelChanged() override;

    bool isIdle() const { return ! busy && ! autoRun; }

    /** Loads a plugin by name and runs every test, without anyone clicking. Used by --healthdemo. */
    void runDemo (const juce::String& name);

private:
    class Results;

    void loadPlugin();
    void runOrStop();
    void startRun();
    void copyReport();
    void updateControls();
    void restyle();

    PluginScanner& pluginScanner;

    PluginPicker picker;
    mbs::IconTextButton loadButton { "Load", mbs::Icon::plug };
    mbs::IconTextButton runButton { "Run health check", mbs::Icon::play };
    juce::TextButton copyButton { "Copy report" };
    juce::Label loadedLabel, statusLabel, noteLabel;
    mbs::BusyBar busyBar;

    std::unique_ptr<Results> results;
    juce::Viewport viewport;

    juce::ThreadPool pool { 1 };
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::String pluginName;
    std::shared_ptr<std::atomic<bool>> cancelFlag { std::make_shared<std::atomic<bool>> (false) };
    bool busy = false, running = false, autoRun = false;

    juce::Rectangle<int> pluginCard, runCard, noteCard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HealthTab)
};
