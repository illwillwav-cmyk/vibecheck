#pragma once

#include <JuceHeader.h>

#include "ui/Widgets.h"

/** Every parameter of a loaded plugin as a slider, with search, reset and randomise.

    Without this the analyzer could only ever measure a plugin at its default settings, which for
    an equaliser or a compressor is the least interesting setting there is. Changes go straight to
    the plugin instance the measurements use. */
class ParameterPanel final : public juce::Component, private juce::Timer
{
public:
    ParameterPanel (juce::AudioPluginInstance& instance, std::function<void()> onChanged);
    ~ParameterPanel() override;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Row;

    void applyFilter();
    void layoutRows();
    void resetAll();
    void randomise();
    void timerCallback() override;

    juce::AudioPluginInstance& plugin;
    std::function<void()> changed;

    mbs::SearchField search;
    mbs::IconTextButton resetButton { "Reset all", mbs::Icon::refresh };
    juce::TextButton randomButton { "Randomise" };
    juce::Label countLabel;
    juce::Viewport viewport;
    juce::Component list;
    juce::OwnedArray<Row> rows;
    bool truncated = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterPanel)
};
