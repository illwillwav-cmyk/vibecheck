#pragma once

#include <JuceHeader.h>

#include "analysis/MeasureSuite.h"
#include "host/PluginScanner.h"
#include "ui/PluginPicker.h"
#include "ui/Plots.h"
#include "ui/Widgets.h"

/** Two plugins, measured the same way at the same time, with the differences spelled out. */
class ComparisonTab final : public juce::Component
{
public:
    explicit ComparisonTab (PluginScanner& scanner);
    ~ComparisonTab() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void lookAndFeelChanged() override;

    bool isIdle() const { return ! slots[0].busy && ! slots[1].busy && ! comparing && ! autoCompare; }

    /** Loads two plugins by name and compares them, without anyone clicking. Used by --comparedemo. */
    void runDemo (const juce::String& nameA, const juce::String& nameB);

private:
    class Results;

    struct Slot
    {
        std::unique_ptr<PluginPicker> picker;
        mbs::IconTextButton loadButton { "Load", mbs::Icon::plug };
        juce::Label loaded;
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        juce::String name;
        bool busy = false;
    };

    void load (int which);
    void compare();
    void updateControls();
    void restyle();

    PluginScanner& pluginScanner;

    std::array<Slot, 2> slots;
    mbs::IconTextButton compareButton { "Compare", mbs::Icon::compare };
    juce::Label statusLabel;
    mbs::BusyBar busyBar;

    std::unique_ptr<Results> results;
    juce::Viewport resultsViewport;

    juce::ThreadPool pool { 2 };
    std::array<juce::Rectangle<int>, 2> slotCards;
    juce::Rectangle<int> actionBar;
    bool comparing = false, autoCompare = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ComparisonTab)
};
