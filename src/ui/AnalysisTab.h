#pragma once

#include <JuceHeader.h>

#include "audio/TestSignal.h"
#include "host/PluginScanner.h"
#include "ui/ParameterPanel.h"
#include "ui/PluginPicker.h"
#include "ui/Plots.h"
#include "ui/Widgets.h"

/** Loads a plugin, pushes test signals through it offline, and shows what came back: headline
    numbers, a plain-language reading, and the graphs behind them. */
class AnalysisTab final : public juce::Component, private juce::Timer
{
public:
    explicit AnalysisTab (PluginScanner& scanner);
    ~AnalysisTab() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void lookAndFeelChanged() override;

    /** Selects a plugin by name, loads it and renders one signal, without anyone clicking.
        Used by --demo to capture a populated window. */
    void runDemo (const juce::String& pluginQuery, vibecheck::SignalType signalType);

    /** Opens the parameter window, writes a picture of it, then calls back. Used by --paramshot. */
    void openParametersAndSnapshot (const juce::File& destination, std::function<void()> whenDone);

    /** Turns the distortion sweeps on, without anyone clicking. Used by --sweeps. */
    void setSweeps (bool on) { sweepToggle.setToggleState (on, juce::dontSendNotification); }

    /** False while a load or render is in flight. */
    bool isIdle() const { return ! busy; }

private:
    struct Outcome;
    class Results;

    void loadSelectedPlugin();
    void loadPlugin (const juce::PluginDescription& description);
    void requestRender();
    void renderCurrentSignal();
    void openEditor();
    void copyReport();
    void openParameters();
    void saveImage();
    void setBusy (bool shouldBeBusy);
    void timerCallback() override;
    void appendLine (const juce::String& line);
    void showOutcome (const Outcome& outcome);
    void updateControls();
    void restyle();
    vibecheck::SignalSpec currentSpec() const;

    PluginScanner& pluginScanner;

    PluginPicker picker;
    juce::ComboBox signalChooser, bufferSizeChooser, sampleRateChooser;
    mbs::IconTextButton loadButton { "Load", mbs::Icon::plug };
    mbs::IconTextButton renderButton { "Run measurements", mbs::Icon::play };
    juce::TextButton editorButton { "Plugin UI" }, reportButton { "Copy report" }, parametersButton { "Parameters" }, imageButton { "Save image" };
    juce::ToggleButton liveToggle { "Re-run when settings change" }, sweepToggle { "Also sweep distortion (slower)" };
    juce::Slider frequencySlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label signalCaption, frequencyCaption, bufferCaption, rateCaption;
    juce::Label loadedLabel, statusLabel;
    juce::TextEditor log;
    mbs::BusyBar busyBar;

    std::unique_ptr<Results> results;
    juce::Viewport resultsViewport;

    juce::Rectangle<int> pluginCard, signalCard, consoleCard;

    juce::ThreadPool pool { 1 };
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    std::unique_ptr<juce::DocumentWindow> editorWindow, parameterWindow;
    std::shared_ptr<juce::FileChooser> fileChooser;
    std::shared_ptr<Outcome> lastOutcome;
    std::function<void()> afterLoad;
    juce::String pluginName;
    bool busy = false, renderPending = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisTab)
};
