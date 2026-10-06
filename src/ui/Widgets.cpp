#include "Widgets.h"

namespace mbs
{
namespace
{
const char* svgFor (Icon icon)
{
    switch (icon)
    {
        case Icon::library:  return "M3 3h7v7H3z M14 3h7v7h-7z M14 14h7v7h-7z M3 14h7v7H3z";
        case Icon::analyzer: return "M22 12h-4l-3 9L9 3l-3 9H2";
        case Icon::sparkle:  return "M12 3l1.9 5.6 5.6 1.9-5.6 1.9L12 18l-1.9-5.6-5.6-1.9 5.6-1.9z M19 3v4 M17 5h4";
        case Icon::compare:  return "M17 1l4 4-4 4 M3 11V9a4 4 0 0 1 4-4h14 M7 23l-4-4 4-4 M21 13v2a4 4 0 0 1-4 4H3";
        case Icon::gauge:    return "M12 14l4-4 M3.3 18a10 10 0 1 1 17.4 0";
        case Icon::sun:      return "M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8z M12 2v2 M12 20v2 M4.9 4.9l1.4 1.4 M17.7 17.7l1.4 1.4 "
                                    "M2 12h2 M20 12h2 M4.9 19.1l1.4-1.4 M17.7 6.3l1.4-1.4";
        case Icon::moon:     return "M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z";
        case Icon::search:   return "M11 4a7 7 0 1 0 0 14 7 7 0 0 0 0-14z M21 21l-4.3-4.3";
        case Icon::refresh:  return "M23 4v6h-6 M1 20v-6h6 M3.5 9a9 9 0 0 1 14.8-3.4L23 10 M1 14l4.7 4.4A9 9 0 0 0 20.5 15";
        case Icon::play:     return "M7 4l13 8-13 8z";
        case Icon::trash:    return "M3 6h18 M8 6V4h8v2 M19 6l-1 14H6L5 6 M10 11v6 M14 11v6";
        case Icon::external: return "M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6 M15 3h6v6 M10 14L21 3";
        case Icon::plug:     return "M9 2v6 M15 2v6 M6 8h12v4a6 6 0 0 1-12 0z M12 18v4";
        case Icon::check:    return "M20 6L9 17l-5-5";
        case Icon::alert:    return "M12 3l10 18H2z M12 10v5 M12 18v.5";
        case Icon::clock:    return "M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18z M12 7v5l3 2";
    }

    return "";
}
} // namespace

void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> box, juce::Colour colour, float strokeWidth)
{
    auto path = juce::Drawable::parseSVGPath (svgFor (icon));

    if (path.isEmpty())
        return;

    path.applyTransform (path.getTransformToScaleToFit (box.getX(), box.getY(), box.getWidth(), box.getHeight(),
                                                        true, juce::Justification::centred));

    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (strokeWidth * box.getWidth() / 20.0f,
                                              juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void drawCard (juce::Graphics& g, juce::Rectangle<float> bounds, bool highlighted)
{
    const auto& p = theme();

    if (p.isDark)
    {
        // A faint top light, so panels read as raised without a heavy shadow.
        g.setGradientFill (juce::ColourGradient (p.card.brighter (0.06f), 0.0f, bounds.getY(),
                                                 p.card, 0.0f, bounds.getBottom(), false));
    }
    else
    {
        g.setColour (juce::Colours::black.withAlpha (0.045f));
        g.fillRoundedRectangle (bounds.translated (0.0f, 1.5f), cardRadius);
        g.setColour (p.card);
    }

    g.fillRoundedRectangle (bounds, cardRadius);

    g.setColour (highlighted ? p.accent.withAlpha (0.65f) : p.line);
    g.drawRoundedRectangle (bounds.reduced (0.5f), cardRadius, 1.0f);
}

juce::Rectangle<int> drawTitledCard (juce::Graphics& g, juce::Rectangle<int> bounds,
                                     const juce::String& title, const juce::String& note)
{
    drawCard (g, bounds.toFloat());

    auto inner = bounds.reduced (18, 14);
    auto head = inner.removeFromTop (20);

    g.setColour (theme().inkMuted);
    g.setFont (brandFont (11.0f, true));
    drawTracked (g, title, head, 1.5f, juce::Justification::centredLeft);

    if (note.isNotEmpty())
    {
        g.setColour (theme().inkFaint);
        g.setFont (monoFont (11.0f));
        g.drawText (note, head, juce::Justification::centredRight, true);
    }

    inner.removeFromTop (6);
    return inner;
}

void drawTracked (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                  float tracking, juce::Justification justification)
{
    if (text.isEmpty())
        return;

    const auto upper = text.toUpperCase();
    const auto font = g.getCurrentFont();

    float total = 0.0f;

    for (int i = 0; i < upper.length(); ++i)
        total += juce::GlyphArrangement::getStringWidth (font, upper.substring (i, i + 1)) + tracking;

    total -= tracking;

    auto x = (float) area.getX();

    if (justification.testFlags (juce::Justification::horizontallyCentred))
        x = (float) area.getCentreX() - total * 0.5f;
    else if (justification.testFlags (juce::Justification::right))
        x = (float) area.getRight() - total;

    const auto baseline = (float) area.getCentreY() + font.getHeight() * 0.34f;

    for (int i = 0; i < upper.length(); ++i)
    {
        const auto glyph = upper.substring (i, i + 1);
        g.drawSingleLineText (glyph, juce::roundToInt (x), juce::roundToInt (baseline));
        x += juce::GlyphArrangement::getStringWidth (font, glyph) + tracking;
    }
}

void drawPill (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> bounds,
               juce::Colour colour, bool filled)
{
    const auto radius = bounds.getHeight() * 0.5f;

    if (filled)
    {
        g.setColour (colour);
        g.fillRoundedRectangle (bounds, radius);
        g.setColour (colour.getBrightness() > 0.6f ? juce::Colours::black.withAlpha (0.85f) : juce::Colours::white);
    }
    else
    {
        g.setColour (colour.withAlpha (0.14f));
        g.fillRoundedRectangle (bounds, radius);
        g.setColour (colour.withAlpha (0.55f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
        g.setColour (colour);
    }

    g.setFont (brandFont (juce::jmin (12.0f, bounds.getHeight() * 0.56f), true));
    g.drawText (text, bounds.toNearestInt(), juce::Justification::centred, false);
}

void drawEmptyState (juce::Graphics& g, juce::Rectangle<int> area, Icon icon, const juce::String& title,
                     const juce::String& body)
{
    const auto& p = theme();
    const auto centre = area.getCentre();

    const auto disc = juce::Rectangle<float> (56.0f, 56.0f).withCentre ({ (float) centre.x, (float) centre.y - 40.0f });
    g.setColour (p.accent.withAlpha (0.12f));
    g.fillEllipse (disc);
    drawIcon (g, icon, disc.reduced (16.0f), p.accent, 1.8f);

    g.setColour (p.ink);
    g.setFont (brandFont (16.0f, true));
    g.drawText (title, juce::Rectangle<int> (area.getWidth() - 40, 24).withCentre ({ centre.x, centre.y + 10 }),
                juce::Justification::centred);

    g.setColour (p.inkFaint);
    g.setFont (brandFont (13.0f));
    g.drawFittedText (body, juce::Rectangle<int> (juce::jmin (380, area.getWidth() - 40), 60)
                                .withCentre ({ centre.x, centre.y + 52 }),
                      juce::Justification::centredTop, 3);
}

void setButtonStyle (juce::Button& button, ButtonStyle style)
{
    button.getProperties().set ("style", (int) style);
    button.repaint();
}

ButtonStyle getButtonStyle (const juce::Button& button)
{
    return (ButtonStyle) (int) button.getProperties().getWithDefault ("style", (int) ButtonStyle::outline);
}

void styleCaption (juce::Label& label, const juce::String& text)
{
    label.setText (text.toUpperCase(), juce::dontSendNotification);
    label.setFont (brandFont (10.5f, true));
    label.setColour (juce::Label::textColourId, theme().inkFaint);
    label.setBorderSize ({ 0, 1, 0, 0 });
    label.setJustificationType (juce::Justification::bottomLeft);
    label.setInterceptsMouseClicks (false, false);
}

void styleConsole (juce::TextEditor& editor)
{
    editor.setMultiLine (true);
    editor.setReadOnly (true);
    editor.setCaretVisible (false);
    editor.setScrollbarsShown (true);
    editor.setFont (monoFont (11.5f));
    editor.setIndents (10, 8);
    editor.setBorder ({ 0, 0, 0, 0 });
    editor.setColour (juce::TextEditor::backgroundColourId, theme().cardHi.withAlpha (theme().isDark ? 0.55f : 1.0f));
    editor.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    editor.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    editor.setColour (juce::TextEditor::textColourId, theme().inkMuted);
}

// --- StatTile -----------------------------------------------------------------------------------

void StatTile::set (const juce::String& newValue, const juce::String& newNote, juce::Colour colour)
{
    value = newValue;
    note = newNote;
    valueColour = colour;
    repaint();
}

void StatTile::setMeter (float fraction)
{
    meterTarget = fraction;

    if (fraction >= 0.0f)
        startTimerHz (60);

    repaint();
}

void StatTile::timerCallback()
{
    const auto target = juce::jmax (0.0f, meterTarget);
    meterShown += (target - meterShown) * 0.2f;

    if (std::abs (target - meterShown) < 0.002f)
    {
        meterShown = target;
        stopTimer();
    }

    repaint();
}

void StatTile::paint (juce::Graphics& g)
{
    const auto& p = theme();
    auto area = getLocalBounds();

    drawCard (g, area.toFloat());
    area = area.reduced (16, 13);

    g.setColour (p.inkFaint);
    g.setFont (brandFont (10.5f, true));
    drawTracked (g, caption, area.removeFromTop (14), 1.4f, juce::Justification::centredLeft);

    area.removeFromTop (3);
    g.setColour (valueColour.isTransparent() ? p.ink : valueColour);
    g.setFont (brandFont (26.0f, true));

    // Long values shrink to fit rather than being cut off with an ellipsis.
    g.drawFittedText (value, area.removeFromTop (34), juce::Justification::centredLeft, 1, 0.55f);

    if (meterTarget >= 0.0f)
    {
        auto meter = juce::Rectangle<float> ((float) area.getX(), (float) getHeight() - 14.0f, (float) area.getWidth(), 3.0f);
        g.setColour (p.line);
        g.fillRoundedRectangle (meter, 1.5f);
        g.setColour (valueColour.isTransparent() ? p.accent : valueColour);
        g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * juce::jlimit (0.0f, 1.0f, meterShown)), 1.5f);
        area.removeFromBottom (8);
    }

    g.setColour (p.inkMuted);
    g.setFont (brandFont (12.0f));
    g.drawText (note, area.removeFromTop (18), juce::Justification::centredLeft, true);
}

// --- BusyBar ------------------------------------------------------------------------------------

void BusyBar::setActive (bool shouldBeActive)
{
    if (active == shouldBeActive)
        return;

    active = shouldBeActive;

    if (active)
        startTimerHz (60);
    else
        stopTimer();

    repaint();
}

void BusyBar::timerCallback()
{
    phase += 0.012f;

    if (phase > 1.0f)
        phase -= 1.0f;

    repaint();
}

void BusyBar::paint (juce::Graphics& g)
{
    if (! active)
        return;

    const auto& p = theme();
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (p.line.withAlpha (0.6f));
    g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);

    // A bar that eases across, so it reads as motion even when the work is one long call.
    const auto eased = 0.5f - 0.5f * std::cos (phase * juce::MathConstants<float>::twoPi);
    const auto width = bounds.getWidth() * 0.32f;
    const auto x = bounds.getX() + (bounds.getWidth() - width) * eased;

    g.setGradientFill (juce::ColourGradient (p.accent, x, 0.0f, p.accent2, x + width, 0.0f, false));
    g.fillRoundedRectangle (juce::Rectangle<float> (x, bounds.getY(), width, bounds.getHeight()), bounds.getHeight() * 0.5f);
}

// --- SearchField --------------------------------------------------------------------------------

SearchField::SearchField()
{
    setFont (brandFont (13.5f));
    setIndents (34, 0);
    setBorder ({ 0, 0, 0, 0 });
    setJustification (juce::Justification::centredLeft);
    setSelectAllWhenFocused (true);
    setTextToShowWhenEmpty ("Search", theme().inkFaint);
    setColour (juce::TextEditor::backgroundColourId, theme().cardHi);
    setColour (juce::TextEditor::outlineColourId, theme().line);
    setColour (juce::TextEditor::focusedOutlineColourId, theme().accent);
    setColour (juce::TextEditor::textColourId, theme().ink);
}

void SearchField::paintOverChildren (juce::Graphics& g)
{
    // The base class draws the placeholder text and the outline.
    juce::TextEditor::paintOverChildren (g);

    drawIcon (g, Icon::search, juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ 17.0f, (float) getHeight() * 0.5f }),
              hasKeyboardFocus (true) ? theme().accent : theme().inkFaint, 1.8f);
}

// --- NavButton ----------------------------------------------------------------------------------

NavButton::NavButton (const juce::String& text, Icon iconToShow, const juce::String& shortcutText)
    : juce::Button (text), icon (iconToShow), shortcut (shortcutText)
{
    setClickingTogglesState (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void NavButton::setSelected (bool isSelected)
{
    selected = isSelected;
    setToggleState (isSelected, juce::dontSendNotification);
    startTimerHz (60);
}

void NavButton::timerCallback()
{
    const auto target = selected ? 1.0f : (hovered ? 0.35f : 0.0f);
    shown += (target - shown) * 0.25f;

    if (std::abs (target - shown) < 0.01f)
    {
        shown = target;
        stopTimer();
    }

    repaint();
}

void NavButton::paintButton (juce::Graphics& g, bool isOver, bool)
{
    const auto& p = theme();

    if (hovered != isOver)
    {
        hovered = isOver;
        startTimerHz (60);
    }

    auto area = getLocalBounds().toFloat().reduced (10.0f, 2.0f);

    g.setColour (p.accent.withAlpha (0.14f * juce::jmin (1.0f, shown * 1.4f)));
    g.fillRoundedRectangle (area, 9.0f);

    // The accent bar on the left edge says which page is open.
    if (shown > 0.4f)
    {
        g.setColour (p.accent.withAlpha (juce::jlimit (0.0f, 1.0f, (shown - 0.4f) / 0.6f)));
        g.fillRoundedRectangle ({ 2.0f, area.getCentreY() - 10.0f * shown, 3.0f, 20.0f * shown }, 1.5f);
    }

    const auto text = p.inkFaint.interpolatedWith (p.ink, juce::jmin (1.0f, shown * 1.2f));
    auto row = area.reduced (12.0f, 0.0f);
    drawIcon (g, icon, row.removeFromLeft (18.0f).withSizeKeepingCentre (18.0f, 18.0f),
              selected ? p.accent : text, 1.7f);
    row.removeFromLeft (12.0f);

    g.setColour (text);
    g.setFont (brandFont (13.5f, selected));
    g.drawText (getButtonText(), row.toNearestInt(), juce::Justification::centredLeft, true);

    g.setColour (p.inkFaint.withAlpha (0.7f));
    g.setFont (monoFont (10.5f));
    g.drawText (shortcut, row.toNearestInt(), juce::Justification::centredRight, false);
}
} // namespace mbs
