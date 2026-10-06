#pragma once

#include <JuceHeader.h>

#include "Theme.h"

/** Rounds off JUCE's defaults and repaints them with the VibeCheck palette.

    Buttons come in four styles (see mbs::ButtonStyle); everything else follows one rule: a quiet
    raised surface, a hairline border, and the accent colour only where something is focused,
    selected or primary. */
class MbsLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    MbsLookAndFeel();

    void refreshColours();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool, int, int, int, int, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                        bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override;

    void drawTableHeaderColumn (juce::Graphics&, juce::TableHeaderComponent&, const juce::String& columnName,
                                int columnId, int width, int height, bool isMouseOver, bool isMouseDown,
                                int columnFlags) override;

    void drawProgressBar (juce::Graphics&, juce::ProgressBar&, int width, int height,
                          double progress, const juce::String& textToShow) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool isOver, bool isDown) override;
};
