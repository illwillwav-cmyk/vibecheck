#include "Plots.h"

#include "Theme.h"
#include "Widgets.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
bool usable (double value) { return std::isfinite (value); }

/** A round step size that gives roughly the wanted number of divisions across a span. */
double niceStep (double span, int wantedDivisions)
{
    if (span <= 0.0)
        return 1.0;

    const auto rough = span / (double) juce::jmax (1, wantedDivisions);
    const auto magnitude = std::pow (10.0, std::floor (std::log10 (rough)));
    const auto residual = rough / magnitude;

    const auto factor = residual < 1.5 ? 1.0 : residual < 3.0 ? 2.0 : residual < 7.0 ? 5.0 : 10.0;
    return factor * magnitude;
}

int decimalsForStep (double step)
{
    if (step >= 1.0)
        return 0;

    return juce::jlimit (0, 4, (int) std::ceil (-std::log10 (step)));
}

juce::String frequencyText (double hz)
{
    if (hz >= 1000.0)
    {
        const auto k = hz / 1000.0;
        return mbs::num (k, std::abs (k - std::round (k)) < 0.001 ? 0 : 1) + "k";
    }

    return mbs::num (hz, 0);
}

juce::Colour seriesColour (int slot)
{
    const auto& p = mbs::theme();

    switch (juce::jmax (0, slot) % 4)
    {
        case 0:  return p.accent;
        case 1:  return p.accent2;
        case 2:  return p.good;
        default: return p.warn;
    }
}
} // namespace

GraphComponent::GraphComponent()
{
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

GraphComponent::~GraphComponent()
{
    stopTimer();
}

void GraphComponent::timerCallback()
{
    reveal = juce::jmin (1.0f, reveal + 0.045f);

    if (reveal >= 1.0f)
        stopTimer();

    repaint();
}

void GraphComponent::setAxes (Axis newX, Axis newY)
{
    x = newX;
    y = newY;
    resolveAutoRanges();
    repaint();
}

void GraphComponent::setTraces (std::vector<Trace> newTraces)
{
    traces = std::move (newTraces);
    resolveAutoRanges();
    reveal = 0.0f;
    startTimerHz (60);
    repaint();
}

void GraphComponent::clearTraces()
{
    traces.clear();
    repaint();
}

void GraphComponent::setPlaceholder (juce::String text)
{
    placeholder = std::move (text);
    repaint();
}

void GraphComponent::resolveAutoRanges()
{
    if (! y.automatic || traces.empty())
        return;

    auto lowest = std::numeric_limits<double>::max();
    auto highest = std::numeric_limits<double>::lowest();

    for (const auto& trace : traces)
        for (std::size_t i = 0; i < trace.y.size() && i < trace.x.size(); ++i)
            if (usable (trace.y[i]) && trace.x[i] >= x.min && trace.x[i] <= x.max)
            {
                lowest = juce::jmin (lowest, trace.y[i]);
                highest = juce::jmax (highest, trace.y[i]);
            }

    if (lowest > highest)
        return;

    // Decibel and degree axes never want a range tighter than a few units, or a flat response
    // would zoom into noise. Other quantities (percent, samples) take their scale from the data.
    const auto angular = y.suffix.contains (juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")));
    const auto minimumSpan = (y.suffix.contains ("dB") || angular) ? 3.0 : 0.0;

    if (highest - lowest < 1.0e-9 && minimumSpan == 0.0)
    {
        // Every value is the same: show the baseline with room above it.
        y.min = lowest >= 0.0 ? 0.0 : lowest - 1.0;
        y.max = juce::jmax (y.min + 1.0, highest * 1.25);
        return;
    }

    const auto span = juce::jmax (highest - lowest, minimumSpan);
    const auto step = niceStep (span * 1.2, 5);

    y.min = std::floor ((lowest - span * 0.06) / step) * step;
    y.max = std::ceil ((highest + span * 0.06) / step) * step;

    // A quantity that cannot be negative, such as CPU load, starts at zero rather than hovering
    // just above it.
    if (lowest >= 0.0)
        y.min = 0.0;

    if (y.max - y.min < step)
        y.max = y.min + step;
}

juce::Rectangle<float> GraphComponent::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (50.0f)
                                     .withTrimmedBottom (24.0f)
                                     .withTrimmedTop (6.0f)
                                     .withTrimmedRight (8.0f);
}

float GraphComponent::xToPixel (double value, juce::Rectangle<float> area) const
{
    if (x.logarithmic)
    {
        const auto low = std::log10 (juce::jmax (1.0e-6, x.min));
        const auto high = std::log10 (juce::jmax (low + 1.0e-6, x.max));
        const auto position = (std::log10 (juce::jmax (1.0e-6, value)) - low) / (high - low);
        return area.getX() + (float) position * area.getWidth();
    }

    const auto position = (value - x.min) / juce::jmax (1.0e-12, x.max - x.min);
    return area.getX() + (float) position * area.getWidth();
}

double GraphComponent::pixelToX (float pixel, juce::Rectangle<float> area) const
{
    const auto t = (double) juce::jlimit (0.0f, 1.0f, (pixel - area.getX()) / juce::jmax (1.0f, area.getWidth()));

    if (x.logarithmic)
    {
        const auto low = std::log10 (juce::jmax (1.0e-6, x.min));
        const auto high = std::log10 (juce::jmax (low + 1.0e-6, x.max));
        return std::pow (10.0, low + t * (high - low));
    }

    return x.min + t * (x.max - x.min);
}

float GraphComponent::yToPixel (double value, juce::Rectangle<float> area) const
{
    const auto position = (value - y.min) / juce::jmax (1.0e-12, y.max - y.min);
    return area.getBottom() - (float) position * area.getHeight();
}

juce::Colour GraphComponent::colourFor (const Trace& trace, int index) const
{
    return trace.color.isTransparent() ? seriesColour (trace.series >= 0 ? trace.series : index) : trace.color;
}

void GraphComponent::drawGrid (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& p = mbs::theme();
    g.setFont (mbs::monoFont (10.5f));

    // Vertical lines.
    if (x.logarithmic)
    {
        for (double decade = 1.0; decade <= x.max; decade *= 10.0)
        {
            for (int step = 1; step <= 9; ++step)
            {
                const auto value = decade * step;

                if (value < x.min || value > x.max)
                    continue;

                const auto px = std::round (xToPixel (value, area));
                const auto major = step == 1;

                g.setColour (p.line.withAlpha (major ? 0.9f : 0.35f));
                g.fillRect (px, area.getY(), 1.0f, area.getHeight());

                if (step == 1 || step == 2 || step == 5)
                {
                    g.setColour (p.inkFaint);
                    g.drawText (frequencyText (value), juce::Rectangle<float> (px - 22.0f, area.getBottom() + 5.0f, 44.0f, 14.0f),
                                juce::Justification::centred);
                }
            }
        }
    }
    else
    {
        const auto step = niceStep (x.max - x.min, 6);
        const auto decimals = decimalsForStep (step);

        for (auto value = std::ceil (x.min / step) * step; value <= x.max + step * 0.001; value += step)
        {
            const auto px = std::round (xToPixel (value, area));
            g.setColour (p.line.withAlpha (0.55f));
            g.fillRect (px, area.getY(), 1.0f, area.getHeight());

            g.setColour (p.inkFaint);
            g.drawText (mbs::num (value, decimals),
                        juce::Rectangle<float> (px - 26.0f, area.getBottom() + 5.0f, 52.0f, 14.0f),
                        juce::Justification::centred);
        }
    }

    // Horizontal lines.
    const auto step = niceStep (y.max - y.min, juce::jmax (3, (int) (area.getHeight() / 38.0f)));
    const auto decimals = decimalsForStep (step);

    for (auto value = std::ceil (y.min / step) * step; value <= y.max + step * 0.001; value += step)
    {
        const auto py = std::round (yToPixel (value, area));
        const auto isZero = std::abs (value) < step * 0.001;

        g.setColour (isZero ? p.inkFaint.withAlpha (0.5f) : p.line.withAlpha (0.55f));
        g.fillRect (area.getX(), py, area.getWidth(), 1.0f);

        g.setColour (p.inkFaint);
        g.drawText (mbs::num (value, decimals),
                    juce::Rectangle<float> (0.0f, py - 7.0f, 45.0f, 14.0f), juce::Justification::centredRight);
    }

    // Axis unit, tucked into the corner so it never fights a tick label.
    const auto unit = (y.suffix.trim().isNotEmpty() ? y.suffix.trim() : juce::String());

    if (unit.isNotEmpty())
    {
        g.setColour (p.inkFaint.withAlpha (0.8f));
        g.setFont (mbs::brandFont (10.0f, true));
        g.drawText (unit, juce::Rectangle<float> (area.getX() + 6.0f, area.getY() + 2.0f, 60.0f, 13.0f), juce::Justification::centredLeft);
    }

    if (x.label.isNotEmpty())
    {
        g.setColour (p.inkFaint.withAlpha (0.8f));
        g.setFont (mbs::brandFont (10.0f, true));
        g.drawText (x.label, juce::Rectangle<float> (area.getRight() - 80.0f, area.getBottom() - 15.0f, 74.0f, 13.0f),
                    juce::Justification::centredRight);
    }
}

void GraphComponent::drawTrace (juce::Graphics& g, const Trace& trace, int index, juce::Rectangle<float> area)
{
    const auto colour = colourFor (trace, index);
    const auto n = juce::jmin (trace.x.size(), trace.y.size());

    if (n == 0)
        return;

    juce::Path line;
    bool started = false;

    // A spectrum carries tens of thousands of bins, far more than there are pixels. Drawing one
    // line through every point makes the top of the plot a solid block, so when the data is
    // denser than the display each pixel column contributes its peak instead.
    const auto columns = juce::jmax (1, (int) area.getWidth());

    if (n / (std::size_t) columns >= 3)
    {
        std::vector<float> peak ((std::size_t) columns, std::numeric_limits<float>::max());

        for (std::size_t i = 0; i < n; ++i)
        {
            if (! usable (trace.x[i]) || ! usable (trace.y[i]) || trace.x[i] < x.min || trace.x[i] > x.max)
                continue;

            const auto column = (int) (xToPixel (trace.x[i], area) - area.getX());

            if (juce::isPositiveAndBelow (column, columns))
                peak[(std::size_t) column] = juce::jmin (peak[(std::size_t) column], yToPixel (trace.y[i], area));
        }

        for (int column = 0; column < columns; ++column)
        {
            const auto value = peak[(std::size_t) column];

            if (value >= std::numeric_limits<float>::max())
                continue;

            const auto px = area.getX() + (float) column + 0.5f;

            if (! started) { line.startNewSubPath (px, value); started = true; }
            else           line.lineTo (px, value);
        }
    }
    else
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            if (! usable (trace.x[i]) || ! usable (trace.y[i]) || trace.x[i] < x.min || trace.x[i] > x.max)
                continue;

            const auto px = xToPixel (trace.x[i], area);
            const auto py = juce::jlimit (area.getY() - 300.0f, area.getBottom() + 300.0f, yToPixel (trace.y[i], area));

            if (! started) { line.startNewSubPath (px, py); started = true; }
            else           line.lineTo (px, py);
        }
    }

    if (! started)
        return;

    g.saveState();

    // The reveal wipes left to right, eased so it settles gently.
    const auto eased = 1.0f - std::pow (1.0f - reveal, 3.0f);
    g.reduceClipRegion (area.withWidth (area.getWidth() * eased).getSmallestIntegerContainer());

    if (trace.filled)
    {
        auto fill = line;
        const auto bounds = line.getBounds();
        fill.lineTo (bounds.getRight(), area.getBottom());
        fill.lineTo (bounds.getX(), area.getBottom());
        fill.closeSubPath();

        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.28f), 0.0f, bounds.getY(),
                                                 colour.withAlpha (0.0f), 0.0f, area.getBottom(), false));
        g.fillPath (fill);
    }

    g.reduceClipRegion (area.getSmallestIntegerContainer());

    juce::PathStrokeType stroke (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    if (trace.dashed)
    {
        const float dashes[] = { 6.0f, 4.0f };
        juce::Path dashed;
        stroke.createDashedStroke (dashed, line, dashes, 2);
        g.setColour (colour);
        g.fillPath (dashed);
    }
    else
    {
        // A soft glow under the line makes it read against a busy grid.
        g.setColour (colour.withAlpha (0.18f));
        g.strokePath (line, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colour);
        g.strokePath (line, stroke);
    }

    g.restoreState();
}

void GraphComponent::drawLegend (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& p = mbs::theme();
    g.setFont (mbs::brandFont (11.0f, true));

    auto right = area.getRight() - 8.0f;
    const auto top = area.getY() + 6.0f;

    for (int i = (int) traces.size() - 1; i >= 0; --i)
    {
        const auto& trace = traces[(std::size_t) i];

        if (trace.name.isEmpty())
            continue;

        const auto width = (float) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), trace.name) + 34.0f;
        const juce::Rectangle<float> chip (right - width, top, width, 20.0f);

        g.setColour (p.card.withAlpha (0.85f));
        g.fillRoundedRectangle (chip, 10.0f);
        g.setColour (p.line);
        g.drawRoundedRectangle (chip.reduced (0.5f), 10.0f, 1.0f);

        const auto colour = colourFor (trace, i);
        const auto swatchY = chip.getCentreY();

        g.setColour (colour);

        if (trace.dashed)
        {
            g.fillRect (chip.getX() + 9.0f, swatchY - 1.0f, 4.0f, 2.0f);
            g.fillRect (chip.getX() + 15.0f, swatchY - 1.0f, 4.0f, 2.0f);
        }
        else
        {
            g.fillRoundedRectangle (chip.getX() + 9.0f, swatchY - 1.5f, 10.0f, 3.0f, 1.5f);
        }

        g.setColour (p.inkMuted);
        g.drawText (trace.name, chip.withTrimmedLeft (24.0f).withTrimmedRight (6.0f), juce::Justification::centredLeft, true);

        right -= width + 6.0f;
    }
}

void GraphComponent::drawReadout (juce::Graphics& g, juce::Rectangle<float> area)
{
    if (hoverX < area.getX() || hoverX > area.getRight() || traces.empty() || reveal < 1.0f)
        return;

    const auto& p = mbs::theme();
    const auto xv = pixelToX (hoverX, area);

    juce::StringArray lines;
    juce::Array<juce::Colour> colours;
    juce::Array<float> dotY;

    for (int i = 0; i < (int) traces.size(); ++i)
    {
        const auto& trace = traces[(std::size_t) i];
        const auto n = juce::jmin (trace.x.size(), trace.y.size());

        if (n == 0)
            continue;

        // Nearest point in x. Spectra are sorted, so this is a binary search for those; the
        // transfer curve's falling branch runs backwards, so it falls back to a linear scan.
        std::size_t best = 0;

        if (std::is_sorted (trace.x.begin(), trace.x.begin() + (std::ptrdiff_t) n))
        {
            const auto it = std::lower_bound (trace.x.begin(), trace.x.begin() + (std::ptrdiff_t) n, xv);
            best = (std::size_t) std::distance (trace.x.begin(), it);

            if (best >= n) best = n - 1;
            if (best > 0 && std::abs (trace.x[best - 1] - xv) < std::abs (trace.x[best] - xv)) --best;
        }
        else
        {
            double bestDistance = std::numeric_limits<double>::max();

            for (std::size_t k = 0; k < n; ++k)
                if (std::abs (trace.x[k] - xv) < bestDistance) { bestDistance = std::abs (trace.x[k] - xv); best = k; }
        }

        if (! usable (trace.y[best]))
            continue;

        const auto label = trace.name.isNotEmpty() ? trace.name + "  " : juce::String();
        const auto decimals = (y.max - y.min) > 40.0 ? 1 : 2;
        lines.add (label + mbs::num (trace.y[best], decimals) + y.suffix);
        colours.add (colourFor (trace, i));
        dotY.add (yToPixel (trace.y[best], area));
    }

    if (lines.isEmpty())
        return;

    const auto px = std::round (hoverX);
    g.setColour (p.inkFaint.withAlpha (0.55f));
    g.fillRect (px, area.getY(), 1.0f, area.getHeight());

    for (int i = 0; i < dotY.size(); ++i)
    {
        const auto dot = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ px + 0.5f, juce::jlimit (area.getY(), area.getBottom(), dotY[i]) });
        g.setColour (p.card);
        g.fillEllipse (dot.expanded (2.0f));
        g.setColour (colours[i]);
        g.fillEllipse (dot);
    }

    const auto xText = x.logarithmic ? frequencyText (xv) + " Hz" : mbs::num (xv, 1) + (x.label.isNotEmpty() ? " " + x.label : juce::String());

    g.setFont (mbs::monoFont (11.0f));
    float width = (float) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), xText);

    for (const auto& line : lines)
        width = juce::jmax (width, (float) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), line) + 16.0f);

    width += 20.0f;
    const auto height = 22.0f + 16.0f * (float) lines.size();

    auto box = juce::Rectangle<float> (px + 12.0f, area.getY() + 30.0f, width, height);

    if (box.getRight() > area.getRight())
        box.setX (px - 12.0f - width);

    g.setColour (p.card.brighter (p.isDark ? 0.08f : 0.0f).withAlpha (0.96f));
    g.fillRoundedRectangle (box, 8.0f);
    g.setColour (p.line);
    g.drawRoundedRectangle (box.reduced (0.5f), 8.0f, 1.0f);

    auto inner = box.reduced (10.0f, 6.0f);
    g.setColour (p.inkFaint);
    g.drawText (xText, inner.removeFromTop (16.0f).toNearestInt(), juce::Justification::centredLeft);

    for (int i = 0; i < lines.size(); ++i)
    {
        auto row = inner.removeFromTop (16.0f);
        g.setColour (colours[i]);
        g.fillEllipse (row.removeFromLeft (10.0f).withSizeKeepingCentre (6.0f, 6.0f));
        g.setColour (p.ink);
        g.drawText (lines[i], row.toNearestInt(), juce::Justification::centredLeft);
    }
}

void GraphComponent::paint (juce::Graphics& g)
{
    const auto area = plotArea();

    if (area.getWidth() < 40.0f || area.getHeight() < 30.0f)
        return;

    drawGrid (g, area);

    if (traces.empty())
    {
        const auto& p = mbs::theme();
        mbs::drawIcon (g, mbs::Icon::analyzer, juce::Rectangle<float> (22.0f, 22.0f).withCentre (area.getCentre().translated (0.0f, -14.0f)),
                       p.inkFaint.withAlpha (0.55f), 1.6f);
        g.setColour (p.inkFaint);
        g.setFont (mbs::brandFont (12.5f));
        g.drawFittedText (placeholder, area.withSizeKeepingCentre (juce::jmin (320.0f, area.getWidth() - 30.0f), 40.0f)
                                           .translated (0.0f, 24.0f).toNearestInt(),
                          juce::Justification::centredTop, 2);
        return;
    }

    for (int i = 0; i < (int) traces.size(); ++i)
        drawTrace (g, traces[(std::size_t) i], i, area);

    drawLegend (g, area);
    drawReadout (g, area);
}

void GraphComponent::mouseMove (const juce::MouseEvent& e)
{
    if (traces.empty())
        return;

    hoverX = (float) e.x;
    repaint();
}

void GraphComponent::mouseExit (const juce::MouseEvent&)
{
    hoverX = -1.0f;
    repaint();
}

// --- Waveform -----------------------------------------------------------------------------------

void WaveformComponent::setBuffer (juce::AudioBuffer<float> newBuffer, double sampleRate)
{
    buffer = std::move (newBuffer);
    rate = sampleRate;
    repaint();
}

void WaveformComponent::paint (juce::Graphics& g)
{
    const auto& p = mbs::theme();
    auto area = getLocalBounds().toFloat().reduced (2.0f, 4.0f);

    if (buffer.getNumSamples() == 0 || buffer.getNumChannels() == 0)
    {
        g.setColour (p.line.withAlpha (0.8f));
        g.fillRect (area.getX(), std::round (area.getCentreY()), area.getWidth(), 1.0f);

        mbs::drawIcon (g, mbs::Icon::analyzer, juce::Rectangle<float> (22.0f, 22.0f).withCentre (area.getCentre().translated (0.0f, -14.0f)),
                       p.inkFaint.withAlpha (0.55f), 1.6f);
        g.setColour (p.inkFaint);
        g.setFont (mbs::brandFont (12.5f));
        g.drawText ("Nothing captured yet", area.withSizeKeepingCentre (area.getWidth(), 20.0f).translated (0.0f, 22.0f).toNearestInt(),
                    juce::Justification::centred);
        return;
    }

    const auto footer = area.removeFromBottom (16.0f);
    const auto* samples = buffer.getReadPointer (0);
    const auto numSamples = buffer.getNumSamples();
    const auto width = juce::jmax (1, (int) area.getWidth());
    const auto centre = area.getCentreY();
    const auto scale = area.getHeight() * 0.5f * 0.92f;

    g.setColour (p.line);
    g.fillRect (area.getX(), std::round (centre), area.getWidth(), 1.0f);

    juce::Path upper, lower;
    float peak = 0.0f;

    for (int column = 0; column < width; ++column)
    {
        const auto first = (int) ((juce::int64) column * numSamples / width);
        const auto last  = juce::jmax (first + 1, juce::jmin (numSamples, (int) ((juce::int64) (column + 1) * numSamples / width)));

        auto lowest = 0.0f, highest = 0.0f;

        for (int i = first; i < last; ++i)
        {
            lowest  = juce::jmin (lowest, samples[i]);
            highest = juce::jmax (highest, samples[i]);
        }

        peak = juce::jmax (peak, std::abs (lowest), std::abs (highest));

        const auto px = area.getX() + (float) column + 0.5f;
        upper.addRectangle (px - 0.5f, centre - highest * scale, 1.0f, juce::jmax (1.0f, (highest - lowest) * scale));
    }

    g.setGradientFill (juce::ColourGradient (p.accent2, 0.0f, area.getY(), p.accent, 0.0f, area.getBottom(), false));
    g.fillPath (upper);

    g.setColour (p.inkFaint);
    g.setFont (mbs::monoFont (10.5f));
    g.drawText (mbs::num (numSamples) + " samples"
                    + (rate > 0.0 ? "  /  " + mbs::num ((double) numSamples / rate, 2) + " s" : juce::String())
                    + "  /  peak " + mbs::num (juce::Decibels::gainToDecibels (peak, -120.0f), 1) + " dBFS",
                footer.toNearestInt(), juce::Justification::centredRight);
}
