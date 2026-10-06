#pragma once

#include <JuceHeader.h>

namespace mbs
{
/** The VibeCheck palette.

    Ink and paper are the Make Believe Studios pair and still swap between the light and dark
    themes, tinted warm so the whole thing reads as graystone. On top of them sits a restrained
    accent family, used sparingly: a stone that marks what is selected or primary, a cool slate for
    second traces, and the status colours, which alone carry hue because they carry meaning. */
struct Palette
{
    // Neutrals.
    juce::Colour ink, inkMuted, inkFaint;
    juce::Colour paper;        ///< Window background.
    juce::Colour sidebar;      ///< Navigation rail.
    juce::Colour card;         ///< A panel sitting on the paper.
    juce::Colour cardHi;       ///< A control or row sitting on a card.
    juce::Colour line;         ///< Hairlines and outlines.

    // Accent family.
    juce::Colour accent;       ///< Primary actions, selection, first trace.
    juce::Colour accent2;      ///< Second trace and secondary highlights.
    juce::Colour good, warn, bad;
    juce::Colour onAccentColour;   ///< Text on top of the accent: dark on a light stone, white on a dark one.

    // Kept so older drawing code keeps compiling: the raised surface, and the three hues.
    juce::Colour paperRaised, red, green, blue;

    bool isDark = true;

    /** A step between paper and ink, 0 being paper. Used for grid lines and quiet fills. */
    juce::Colour tone (float amount) const { return paper.interpolatedWith (ink, juce::jlimit (0.0f, 1.0f, amount)); }

    /** Text colour that reads on top of the accent. */
    juce::Colour onAccent() const { return onAccentColour; }
};

Palette darkPalette();
Palette lightPalette();

/** The palette every component draws with. One global, because the theme is a property of the
    application rather than of any one view. */
const Palette& theme();
void setDarkTheme (bool shouldBeDark);
bool isDarkTheme();

/** Remembers the chosen theme between launches. */
void saveThemePreference (juce::PropertiesFile* settings);
bool loadThemePreference (juce::PropertiesFile* settings, bool fallbackDark);

/** Colour for a vibe score: green when it reads hand-written, amber in between, red when it reads
    as machine-generated. */
juce::Colour scoreColour (double score);
juce::String scoreVerdict (double score);

juce::Font brandFont (float height, bool bold = false);
juce::Font monoFont (float height);

/** Rounds to a number of decimal places and prints exactly that many, with no more.

    juce::String (double, 0) does not mean "no decimals" - zero asks JUCE for as many digits as it
    needs, so 20.04 prints as 20.04. Everything shown to a person goes through this instead. */
juce::String num (double value, int decimals = 0);

/** A percentage with sensible precision: small values keep decimals, large ones do not. */
juce::String percent (double value);

/** Spacing and radii shared by every page, so the whole app sits on one grid. */
constexpr int pagePadding = 24;
constexpr int gutter      = 16;
constexpr float cardRadius = 14.0f;
constexpr float controlRadius = 8.0f;
} // namespace mbs
