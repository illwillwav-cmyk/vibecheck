#include "Theme.h"

#include <cmath>

namespace mbs
{
namespace
{
Palette current = darkPalette();

constexpr const char* themeKey = "uiThemeDark";
}

Palette darkPalette()
{
    Palette p;
    p.isDark   = true;
    p.ink      = juce::Colour (0xfff2f1ee);
    p.inkMuted = juce::Colour (0xffb5b3ad);
    p.inkFaint = juce::Colour (0xff807e78);
    p.paper    = juce::Colour (0xff0d0d0c);
    p.sidebar  = juce::Colour (0xff121211);
    p.card     = juce::Colour (0xff191917);
    p.cardHi   = juce::Colour (0xff232321);
    p.line     = juce::Colour (0xff302f2c);
    p.accent   = juce::Colour (0xffcdc9bf);   // warm stone
    p.accent2  = juce::Colour (0xff8fa6b3);   // cool slate
    p.good     = juce::Colour (0xff4cc38a);
    p.warn     = juce::Colour (0xffe9b949);
    p.bad      = juce::Colour (0xffee7b6f);
    p.onAccentColour = juce::Colour (0xff141412);
    p.paperRaised = p.card;
    p.red = p.bad; p.green = p.good; p.blue = p.accent2;
    return p;
}

Palette lightPalette()
{
    Palette p;
    p.isDark   = false;
    p.ink      = juce::Colour (0xff1a1a18);
    p.inkMuted = juce::Colour (0xff595750);
    p.inkFaint = juce::Colour (0xff8c8a83);
    p.paper    = juce::Colour (0xfff1f0ed);
    p.sidebar  = juce::Colour (0xfffbfaf8);
    p.card     = juce::Colour (0xffffffff);
    p.cardHi   = juce::Colour (0xffeceae6);
    p.line     = juce::Colour (0xffdddbd5);
    p.accent   = juce::Colour (0xff4a4943);   // graphite
    p.accent2  = juce::Colour (0xff56707e);   // slate
    p.good     = juce::Colour (0xff0e8a5f);
    p.warn     = juce::Colour (0xffa86608);
    p.bad      = juce::Colour (0xffc43d3d);
    p.onAccentColour = juce::Colours::white;
    p.paperRaised = p.card;
    p.red = p.bad; p.green = p.good; p.blue = p.accent2;
    return p;
}

const Palette& theme()          { return current; }
bool isDarkTheme()              { return current.isDark; }
void setDarkTheme (bool dark)   { current = dark ? darkPalette() : lightPalette(); }

void saveThemePreference (juce::PropertiesFile* settings)
{
    if (settings == nullptr)
        return;

    settings->setValue (themeKey, current.isDark);
    settings->saveIfNeeded();
}

bool loadThemePreference (juce::PropertiesFile* settings, bool fallbackDark)
{
    return settings != nullptr ? settings->getBoolValue (themeKey, fallbackDark) : fallbackDark;
}

juce::Colour scoreColour (double score)
{
    const auto& p = theme();

    if (score < 10.0) return p.good;
    if (score < 30.0) return p.warn.interpolatedWith (p.good, 0.25f);
    if (score < 55.0) return p.warn.interpolatedWith (p.bad, 0.35f);
    return p.bad;
}

juce::String scoreVerdict (double score)
{
    if (score < 10.0) return "Hand-written";
    if (score < 30.0) return "Some template smell";
    if (score < 55.0) return "Substantial fingerprints";
    return "Machine-generated";
}

juce::Font brandFont (float height, bool bold)
{
       #if JUCE_MAC
    const auto family = "Helvetica Neue";
   #elif JUCE_WINDOWS
    const auto family = "Segoe UI";
   #else
    const auto family = juce::Font::getDefaultSansSerifFontName();
   #endif

    return juce::Font (juce::FontOptions (family, height, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font monoFont (float height)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
}

juce::String num (double value, int decimals)
{
    if (! std::isfinite (value))
        return "-";

    decimals = juce::jlimit (0, 8, decimals);

    // juce::String (double, n) with n of zero prints every significant digit, so the rounding is
    // done here and the result printed as an integer.
    if (decimals == 0)
        return juce::String ((juce::int64) std::llround (value));

    auto text = juce::String (value, decimals);

    if (text.startsWith ("-") && text.containsOnly ("-0."))
        text = text.substring (1);   // "-0.00" reads as noise

    return text;
}

juce::String percent (double value)
{
    if (value == 0.0)
        return "0%";

    if (std::abs (value) < 0.001)
        return "<0.001%";

    const auto magnitude = std::abs (value);
    const auto decimals = magnitude >= 100.0 ? 0 : magnitude >= 10.0 ? 1 : magnitude >= 1.0 ? 2 : 3;
    return num (value, decimals) + "%";
}
} // namespace mbs
