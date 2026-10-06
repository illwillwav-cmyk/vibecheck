#pragma once

#include <JuceHeader.h>

#include "host/LibraryScan.h"
#include "host/PluginScanner.h"
#include "ui/AnalysisTab.h"
#include "ui/ComparisonTab.h"
#include "ui/HealthTab.h"
#include "ui/PerformanceTab.h"
#include "ui/PluginBrowserTab.h"
#include "ui/SplashScreen.h"
#include "ui/VibeCheckTab.h"
#include "ui/Widgets.h"

/** The application shell: a navigation rail, a page header, and one page at a time. */
class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    explicit MainComponent (PluginScanner& scanner);
    ~MainComponent() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

    void selectTab (const juce::String& name);

    AnalysisTab& getAnalysisTab()   { return analysisTab; }
    ComparisonTab& getComparisonTab() { return comparisonTab; }
    PerformanceTab& getPerformanceTab() { return performanceTab; }
    HealthTab& getHealthTab() { return healthTab; }
    VibeCheckTab& getVibeCheckTab() { return vibeCheckTab; }

    /** True when nothing has work in flight. */
    bool isIdle() const { return ! scanning && analysisTab.isIdle() && vibeCheckTab.isIdle() && comparisonTab.isIdle() && performanceTab.isIdle() && healthTab.isIdle(); }

    /** Sends the splash away without waiting for a click; used by the automation switches. */
    void skipSplash();

private:
    class ThemeToggle;

    void toggleTheme();
    void setView (int index);
    void timerCallback() override;
    juce::Component* pageAt (int index);

    PluginScanner& pluginScanner;

    PluginBrowserTab pluginManagerTab;
    AnalysisTab analysisTab;
    HealthTab healthTab;
    VibeCheckTab vibeCheckTab;
    ComparisonTab comparisonTab;
    PerformanceTab performanceTab;

    std::array<std::unique_ptr<mbs::NavButton>, 6> nav;
    std::unique_ptr<ThemeToggle> themeToggle;

    mbs::IconTextButton manualButton { "Manual", mbs::Icon::external };
    mbs::IconTextButton updateNotice { "Update available", mbs::Icon::sparkle };
    juce::String updatePage;

    int currentIndex = -1;

    vibecheck::BackgroundLibraryScan libraryScan;
    bool scanning = false;
    juce::String scanText { "Checking library" };
    float pulse = 0.0f;

    std::unique_ptr<VibeSplashScreen> splash;

    juce::Rectangle<int> railBounds, headerBounds, pageBounds, chipBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
