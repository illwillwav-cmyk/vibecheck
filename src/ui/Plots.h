#pragma once

#include <JuceHeader.h>

#include <vector>

/** A line graph with log or linear axes, a "nice-number" grid, any number of traces, a hover
    read-out, and an animated reveal. It draws on a transparent background so it can sit inside a
    card; the card supplies the title. */
class GraphComponent final : public juce::Component,
                             private juce::Timer
{
public:
    GraphComponent();
    ~GraphComponent() override;

    struct Axis
    {
        double min = 0.0;
        double max = 1.0;
        bool logarithmic = false;
        juce::String label;
        juce::String suffix;

        /** When true the range is taken from the data instead of min and max. */
        bool automatic = false;
    };

    struct Trace
    {
        std::vector<double> x, y;

        float tone = 1.0f;          ///< Retained for callers; colour now comes from the series.
        juce::String name;

        /** Optional explicit colour. When transparent the trace takes its series colour. */
        juce::Colour color = juce::Colours::transparentBlack;

        /** Dashed rather than solid, so a second trace is told apart by more than hue. */
        bool dashed = false;

        /** Shade the area beneath the line. */
        bool filled = false;

        /** Which palette slot to use: 0 accent, 1 second accent, 2 good, 3 warn. Negative picks
            by position. */
        int series = -1;
    };

    void setAxes (Axis newX, Axis newY);
    void setTraces (std::vector<Trace> newTraces);
    void setPlaceholder (juce::String text);
    void clearTraces();
    bool hasTraces() const { return ! traces.empty(); }

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> plotArea() const;
    float xToPixel (double value, juce::Rectangle<float> area) const;
    float yToPixel (double value, juce::Rectangle<float> area) const;
    double pixelToX (float pixel, juce::Rectangle<float> area) const;
    void drawGrid (juce::Graphics&, juce::Rectangle<float> area);
    void drawTrace (juce::Graphics&, const Trace&, int index, juce::Rectangle<float> area);
    void drawLegend (juce::Graphics&, juce::Rectangle<float> area);
    void drawReadout (juce::Graphics&, juce::Rectangle<float> area);
    juce::Colour colourFor (const Trace&, int index) const;
    void resolveAutoRanges();
    void timerCallback() override;

    Axis x, y;
    std::vector<Trace> traces;
    juce::String placeholder { "No measurement yet" };
    float reveal { 1.0f };
    float hoverX { -1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GraphComponent)
};

/** Draws a captured buffer as a waveform envelope, one min/max pair per pixel column. */
class WaveformComponent final : public juce::Component
{
public:
    WaveformComponent() = default;

    void setBuffer (juce::AudioBuffer<float> newBuffer, double sampleRate);
    void paint (juce::Graphics&) override;

private:
    juce::AudioBuffer<float> buffer;
    double rate = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformComponent)
};
