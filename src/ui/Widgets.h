#pragma once

#include <JuceHeader.h>

#include "Theme.h"

/** Small building blocks shared by every page, so the whole app speaks one visual language. */
namespace mbs
{
enum class Icon
{
    library, analyzer, sparkle, compare, gauge, sun, moon, search, refresh, play, trash,
    external, plug, check, alert, clock
};

/** Strokes a line icon into a box. Icons are authored on a 24-unit grid and scaled to fit. */
void drawIcon (juce::Graphics&, Icon, juce::Rectangle<float> box, juce::Colour, float strokeWidth = 1.7f);

/** A panel: rounded, raised off the paper, hairline border. */
void drawCard (juce::Graphics&, juce::Rectangle<float> bounds, bool highlighted = false);

/** A panel with a small caption and an optional right-aligned note along its top edge. Returns
    the rectangle left inside for content. */
juce::Rectangle<int> drawTitledCard (juce::Graphics&, juce::Rectangle<int> bounds,
                                     const juce::String& title, const juce::String& note = {});

/** Capitals with air between the letters. */
void drawTracked (juce::Graphics&, const juce::String& text, juce::Rectangle<int> area,
                  float tracking, juce::Justification);

/** A small rounded tag. */
void drawPill (juce::Graphics&, const juce::String& text, juce::Rectangle<float> bounds,
               juce::Colour colour, bool filled = false);

/** Icon, headline and a line of explanation, centred: what a panel shows before it has data. */
void drawEmptyState (juce::Graphics&, juce::Rectangle<int> area, Icon, const juce::String& title,
                     const juce::String& body);

/** Which look a button takes. Outline is the default. */
enum class ButtonStyle { outline, primary, danger, ghost };
void setButtonStyle (juce::Button&, ButtonStyle);
ButtonStyle getButtonStyle (const juce::Button&);

/** Makes a label look like a field caption: small, bold, quiet. */
void styleCaption (juce::Label&, const juce::String& text);

/** Makes a read-only text editor look like a quiet console. */
void styleConsole (juce::TextEditor&);

/** One headline number: a caption, a large value and a line of context. */
class StatTile final : public juce::Component, private juce::Timer
{
public:
    StatTile() = default;
    explicit StatTile (juce::String captionText) : caption (std::move (captionText)) {}

    void set (const juce::String& newValue, const juce::String& newNote = {},
              juce::Colour colour = juce::Colours::transparentBlack);

    /** A thin meter along the bottom, 0 to 1. Negative hides it. */
    void setMeter (float fraction);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    juce::String caption, value { "-" }, note;
    juce::Colour valueColour { juce::Colours::transparentBlack };
    float meterTarget = -1.0f, meterShown = 0.0f;
};

/** An indeterminate progress bar for work of unknown length. Invisible when idle. */
class BusyBar final : public juce::Component, private juce::Timer
{
public:
    BusyBar() { setInterceptsMouseClicks (false, false); }
    void setActive (bool shouldBeActive);
    bool isActive() const { return active; }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    bool active = false;
    float phase = 0.0f;
};

/** A search box with a magnifier inside it. */
class SearchField final : public juce::TextEditor
{
public:
    SearchField();
    void paintOverChildren (juce::Graphics&) override;
};

/** A button with an icon to the left of its label. */
class IconTextButton final : public juce::TextButton
{
public:
    explicit IconTextButton (const juce::String& text, Icon iconToShow)
        : juce::TextButton (text), icon (iconToShow) {}

    Icon icon;
};

/** One entry in the navigation rail. */
class NavButton final : public juce::Button, private juce::Timer
{
public:
    NavButton (const juce::String& text, Icon iconToShow, const juce::String& shortcutText);
    void paintButton (juce::Graphics&, bool isOver, bool isDown) override;
    void setSelected (bool isSelected);

private:
    void timerCallback() override;
    Icon icon;
    juce::String shortcut;
    float shown = 0.0f;
    bool selected = false, hovered = false;
};
} // namespace mbs
