#include "MbsLookAndFeel.h"

#include "Widgets.h"

MbsLookAndFeel::MbsLookAndFeel()
{
    refreshColours();
}

void MbsLookAndFeel::refreshColours()
{
    const auto& p = mbs::theme();

    setColour (juce::ResizableWindow::backgroundColourId, p.paper);
    setColour (juce::DocumentWindow::textColourId,        p.ink);

    setColour (juce::Label::textColourId,                 p.ink);
    setColour (juce::Label::outlineColourId,              juce::Colours::transparentBlack);
    setColour (juce::Label::backgroundColourId,           juce::Colours::transparentBlack);
    setColour (juce::TextButton::buttonColourId,          p.cardHi);
    setColour (juce::TextButton::buttonOnColourId,        p.accent);
    setColour (juce::TextButton::textColourOffId,         p.ink);
    setColour (juce::TextButton::textColourOnId,          p.onAccent());

    setColour (juce::ComboBox::backgroundColourId,        p.cardHi);
    setColour (juce::ComboBox::textColourId,              p.ink);
    setColour (juce::ComboBox::outlineColourId,           p.line);
    setColour (juce::ComboBox::arrowColourId,             p.inkMuted);
    setColour (juce::ComboBox::focusedOutlineColourId,    p.accent);

    setColour (juce::PopupMenu::backgroundColourId,       p.card.brighter (p.isDark ? 0.04f : 0.0f));
    setColour (juce::PopupMenu::textColourId,             p.ink);
    setColour (juce::PopupMenu::headerTextColourId,       p.inkFaint);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, p.accent);
    setColour (juce::PopupMenu::highlightedTextColourId,  p.onAccent());

    setColour (juce::TextEditor::backgroundColourId,      p.cardHi);
    setColour (juce::TextEditor::textColourId,            p.ink);
    setColour (juce::TextEditor::outlineColourId,         p.line);
    setColour (juce::TextEditor::focusedOutlineColourId,  p.accent);
    setColour (juce::TextEditor::highlightColourId,       p.accent.withAlpha (0.35f));
    setColour (juce::TextEditor::highlightedTextColourId, p.ink);
    setColour (juce::CaretComponent::caretColourId,       p.accent);

    setColour (juce::TableHeaderComponent::backgroundColourId, p.card);
    setColour (juce::TableHeaderComponent::textColourId,  p.inkFaint);
    setColour (juce::TableHeaderComponent::outlineColourId, p.line);
    setColour (juce::TableHeaderComponent::highlightColourId, p.cardHi);

    setColour (juce::ListBox::backgroundColourId,         juce::Colours::transparentBlack);
    setColour (juce::ListBox::textColourId,               p.ink);
    setColour (juce::ListBox::outlineColourId,            juce::Colours::transparentBlack);

    setColour (juce::ScrollBar::thumbColourId,            p.inkFaint);
    setColour (juce::Slider::trackColourId,               p.accent);
    setColour (juce::Slider::backgroundColourId,          p.line);
    setColour (juce::Slider::thumbColourId,               p.ink);
    setColour (juce::Slider::textBoxTextColourId,         p.ink);
    setColour (juce::Slider::textBoxBackgroundColourId,   p.cardHi);
    setColour (juce::Slider::textBoxOutlineColourId,      p.line);
    setColour (juce::Slider::textBoxHighlightColourId,    p.accent.withAlpha (0.35f));

    setColour (juce::AlertWindow::backgroundColourId,     p.card);
    setColour (juce::AlertWindow::textColourId,           p.ink);
    setColour (juce::AlertWindow::outlineColourId,        p.line);

    setColour (juce::ToggleButton::textColourId,          p.ink);
    setColour (juce::ToggleButton::tickColourId,          p.accent);
    setColour (juce::ToggleButton::tickDisabledColourId,  p.inkFaint);

    setColour (juce::ProgressBar::backgroundColourId,     p.line);
    setColour (juce::ProgressBar::foregroundColourId,     p.accent);

    setColour (juce::TooltipWindow::backgroundColourId,   p.cardHi);
    setColour (juce::TooltipWindow::textColourId,         p.ink);
    setColour (juce::TooltipWindow::outlineColourId,      p.line);
}

// --- Buttons ------------------------------------------------------------------------------------

void MbsLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                           const juce::Colour&, bool isOver, bool isDown)
{
    const auto& p = mbs::theme();
    const auto area = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto radius = mbs::controlRadius;
    const auto enabled = button.isEnabled();
    const auto style = mbs::getButtonStyle (button);
    const auto selected = button.getToggleState();

    if (style == mbs::ButtonStyle::primary || selected)
    {
        auto fill = p.accent;

        if (! enabled)       fill = p.accent.withAlpha (0.28f);
        else if (isDown)     fill = fill.darker (0.2f);
        else if (isOver)     fill = fill.brighter (0.12f);

        g.setColour (fill);
        g.fillRoundedRectangle (area, radius);
        return;
    }

    if (style == mbs::ButtonStyle::danger)
    {
        g.setColour (p.bad.withAlpha (isDown ? 0.30f : isOver ? 0.20f : 0.10f));
        g.fillRoundedRectangle (area, radius);
        g.setColour (p.bad.withAlpha (enabled ? 0.6f : 0.25f));
        g.drawRoundedRectangle (area, radius, 1.0f);
        return;
    }

    if (style == mbs::ButtonStyle::ghost)
    {
        if (isOver || isDown)
        {
            g.setColour (p.ink.withAlpha (isDown ? 0.12f : 0.07f));
            g.fillRoundedRectangle (area, radius);
        }

        return;
    }

    g.setColour (isDown ? p.cardHi.brighter (0.1f) : isOver ? p.cardHi.brighter (p.isDark ? 0.08f : -0.03f) : p.cardHi);
    g.fillRoundedRectangle (area, radius);
    g.setColour (isOver && enabled ? p.inkFaint : p.line);
    g.drawRoundedRectangle (area, radius, 1.0f);
}

juce::Font MbsLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return mbs::brandFont (13.0f, true);
}

void MbsLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto& p = mbs::theme();
    const auto style = mbs::getButtonStyle (button);
    const auto enabled = button.isEnabled();

    juce::Colour colour = p.ink;

    if (style == mbs::ButtonStyle::primary || button.getToggleState())
        colour = enabled ? p.onAccent() : p.onAccent().withAlpha (0.6f);
    else if (style == mbs::ButtonStyle::danger)
        colour = enabled ? p.bad : p.bad.withAlpha (0.5f);
    else if (! enabled)
        colour = p.inkFaint;

    g.setColour (colour);
    g.setFont (getTextButtonFont (button, button.getHeight()));

    auto area = button.getLocalBounds().reduced (10, 0);
    const auto textWidth = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), button.getButtonText());

    if (auto* withIcon = dynamic_cast<mbs::IconTextButton*> (&button))
    {
        constexpr float iconSize = 15.0f;
        const auto group = iconSize + 8.0f + (float) textWidth;
        const auto left = (float) area.getCentreX() - group * 0.5f;

        mbs::drawIcon (g, withIcon->icon,
                       juce::Rectangle<float> (iconSize, iconSize).withPosition (left, (float) area.getCentreY() - iconSize * 0.5f),
                       colour, 1.9f);

        g.drawText (button.getButtonText(),
                    juce::Rectangle<int> ((int) (left + iconSize + 8.0f), area.getY(), (int) std::ceil (textWidth) + 4, area.getHeight()),
                    juce::Justification::centredLeft, false);
        return;
    }

    g.drawText (button.getButtonText(), area, juce::Justification::centred, true);
}

void MbsLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool isOver, bool)
{
    const auto& p = mbs::theme();
    const auto box = juce::Rectangle<float> (18.0f, 18.0f).withCentre ({ 14.0f, (float) button.getHeight() * 0.5f });

    g.setColour (button.getToggleState() ? p.accent : p.cardHi);
    g.fillRoundedRectangle (box, 5.0f);
    g.setColour (button.getToggleState() ? p.accent : (isOver ? p.inkFaint : p.line));
    g.drawRoundedRectangle (box.reduced (0.5f), 5.0f, 1.0f);

    if (button.getToggleState())
        mbs::drawIcon (g, mbs::Icon::check, box.reduced (4.0f), p.onAccent(), 2.4f);

    g.setColour (button.isEnabled() ? p.ink : p.inkFaint);
    g.setFont (mbs::brandFont (13.0f));
    g.drawText (button.getButtonText(), button.getLocalBounds().withTrimmedLeft (32), juce::Justification::centredLeft, true);
}

// --- Combo box ----------------------------------------------------------------------------------

void MbsLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                   int, int, int, int, juce::ComboBox& box)
{
    const auto& p = mbs::theme();
    const auto area = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);

    g.setColour (box.isEnabled() ? p.cardHi : p.cardHi.withAlpha (0.5f));
    g.fillRoundedRectangle (area, mbs::controlRadius);
    g.setColour (box.hasKeyboardFocus (true) ? p.accent : p.line);
    g.drawRoundedRectangle (area, mbs::controlRadius, 1.0f);

    const auto cx = (float) width - 17.0f;
    const auto cy = (float) height * 0.5f;

    juce::Path chevron;
    chevron.startNewSubPath (cx - 4.0f, cy - 2.0f);
    chevron.lineTo (cx, cy + 2.5f);
    chevron.lineTo (cx + 4.0f, cy - 2.0f);

    g.setColour (box.isEnabled() ? p.inkMuted : p.inkFaint);
    g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void MbsLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (11, 1, box.getWidth() - 36, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

juce::Font MbsLookAndFeel::getComboBoxFont (juce::ComboBox&)  { return mbs::brandFont (13.5f); }
juce::Font MbsLookAndFeel::getLabelFont (juce::Label& label)  { return label.getFont(); }

// --- Slider -------------------------------------------------------------------------------------

void MbsLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float, float,
                                       juce::Slider::SliderStyle, juce::Slider& slider)
{
    const auto& p = mbs::theme();
    const auto cy = (float) y + (float) height * 0.5f;
    const auto enabled = slider.isEnabled();

    const juce::Rectangle<float> track ((float) x, cy - 2.0f, (float) width, 4.0f);
    g.setColour (p.line);
    g.fillRoundedRectangle (track, 2.0f);

    g.setColour (enabled ? p.accent : p.inkFaint);
    g.fillRoundedRectangle (track.withRight (sliderPos), 2.0f);

    const auto thumb = juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ sliderPos, cy });
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillEllipse (thumb.translated (0.0f, 1.0f));
    g.setColour (enabled ? p.ink : p.inkFaint);
    g.fillEllipse (thumb);
    g.setColour (enabled ? p.accent : p.line);
    g.drawEllipse (thumb.reduced (4.5f), 2.0f);
}

// --- Text editor --------------------------------------------------------------------------------

void MbsLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (0.0f, 0.0f, (float) width, (float) height, mbs::controlRadius);
}

void MbsLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    if (! editor.isEnabled())
        return;

    const auto colour = editor.hasKeyboardFocus (true) && ! editor.isReadOnly()
                          ? editor.findColour (juce::TextEditor::focusedOutlineColourId)
                          : editor.findColour (juce::TextEditor::outlineColourId);

    if (colour.isTransparent())
        return;

    g.setColour (colour);
    g.drawRoundedRectangle (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f, mbs::controlRadius, 1.0f);
}

// --- Menus, scrollbars, tables ------------------------------------------------------------------

void MbsLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto& p = mbs::theme();
    g.fillAll (p.card.brighter (p.isDark ? 0.04f : 0.0f));
    g.setColour (p.line);
    g.drawRect (0, 0, width, height, 1);
}

void MbsLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                    bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                                    bool isMouseOver, bool isMouseDown)
{
    const auto& p = mbs::theme();

    const auto thumb = isScrollbarVertical
                         ? juce::Rectangle<float> ((float) x + (float) width * 0.5f - 2.5f, (float) thumbStartPosition, 5.0f, (float) thumbSize)
                         : juce::Rectangle<float> ((float) thumbStartPosition, (float) y + (float) height * 0.5f - 2.5f, (float) thumbSize, 5.0f);

    g.setColour (p.ink.withAlpha (isMouseDown ? 0.55f : isMouseOver ? 0.38f : 0.2f));
    g.fillRoundedRectangle (thumb, 2.5f);
}

void MbsLookAndFeel::drawTableHeaderColumn (juce::Graphics& g, juce::TableHeaderComponent& header,
                                            const juce::String& columnName, int columnId, int width, int height,
                                            bool isMouseOver, bool, int columnFlags)
{
    const auto& p = mbs::theme();

    if (isMouseOver)
    {
        g.setColour (p.cardHi.withAlpha (0.6f));
        g.fillRect (0, 0, width, height);
    }

    const auto sorted = (columnFlags & (juce::TableHeaderComponent::sortedForwards | juce::TableHeaderComponent::sortedBackwards)) != 0;

    g.setColour (sorted ? p.ink : p.inkFaint);
    g.setFont (mbs::brandFont (10.5f, true));

    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (10, 0);

    if (sorted)
    {
        const auto cx = (float) area.getRight() - 6.0f;
        const auto cy = (float) height * 0.5f;
        const auto up = (columnFlags & juce::TableHeaderComponent::sortedForwards) != 0;

        juce::Path arrow;
        arrow.addTriangle (cx - 4.0f, up ? cy + 2.0f : cy - 2.0f, cx + 4.0f, up ? cy + 2.0f : cy - 2.0f,
                           cx, up ? cy - 3.0f : cy + 3.0f);
        g.setColour (p.accent);
        g.fillPath (arrow);
        area.removeFromRight (14);
        g.setColour (p.ink);
    }

    mbs::drawTracked (g, columnName, area, 1.2f, juce::Justification::centredLeft);

    juce::ignoreUnused (header, columnId);
}

void MbsLookAndFeel::drawProgressBar (juce::Graphics& g, juce::ProgressBar&, int width, int height,
                                      double progress, const juce::String& textToShow)
{
    const auto& p = mbs::theme();
    const auto bar = juce::Rectangle<float> (0.0f, (float) height * 0.5f - 3.0f, (float) width, 6.0f);

    g.setColour (p.line);
    g.fillRoundedRectangle (bar, 3.0f);

    if (progress >= 0.0)
    {
        g.setColour (p.accent);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) juce::jlimit (0.0, 1.0, progress)), 3.0f);
    }

    if (textToShow.isNotEmpty())
    {
        g.setColour (p.inkMuted);
        g.setFont (mbs::brandFont (12.0f));
        g.drawText (textToShow, 0, 0, width, (int) bar.getY() - 2, juce::Justification::centredLeft);
    }
}
