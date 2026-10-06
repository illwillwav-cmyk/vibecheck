#include "AppIcon.h"

#include "BrandAssets.h"
#include "Theme.h"

#include <vector>

namespace mbs
{
namespace
{
void paintBackdrop (juce::Graphics& g, juce::Rectangle<float> bounds, const Palette& p)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2925), bounds.getX(), bounds.getY(),
                                             juce::Colour (0xff0b0b0a), bounds.getRight(), bounds.getBottom(), false));
    g.fillRect (bounds);

    const auto radius = juce::jmax (bounds.getWidth(), bounds.getHeight()) * 0.7f;
    g.setGradientFill (juce::ColourGradient (p.accent.withAlpha (0.22f), bounds.getX() + bounds.getWidth() * 0.25f, bounds.getY() + bounds.getHeight() * 0.2f,
                                             p.accent.withAlpha (0.0f), bounds.getX() + bounds.getWidth() * 0.25f + radius, bounds.getY() + bounds.getHeight() * 0.2f, true));
    g.fillRect (bounds);
    g.setGradientFill (juce::ColourGradient (p.accent2.withAlpha (0.18f), bounds.getRight() - bounds.getWidth() * 0.2f, bounds.getBottom(),
                                             p.accent2.withAlpha (0.0f), bounds.getRight() - bounds.getWidth() * 0.2f - radius * 0.8f, bounds.getBottom(), true));
    g.fillRect (bounds);
}
} // namespace

void drawLogoMark (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour first, juce::Colour second)
{
    constexpr int bars = 7;
    const auto barWidth = bounds.getWidth() / ((float) bars * 1.6f);
    const auto step = (bounds.getWidth() - barWidth) / (float) (bars - 1);

    for (int i = 0; i < bars; ++i)
    {
        const auto t = (float) std::abs (i - 3) / 3.0f;                      // 0 at the point of the V, 1 at the tips
        const auto height = bounds.getHeight() * (0.30f + 0.26f * t);
        const auto centreY = bounds.getY() + bounds.getHeight() * (0.28f + 0.44f * (1.0f - t));
        const auto x = bounds.getX() + (float) i * step;

        g.setColour (first.interpolatedWith (second, (float) i / (float) (bars - 1)));
        g.fillRoundedRectangle (x, centreY - height * 0.5f, barWidth, height, barWidth * 0.5f);
    }
}

juce::Image renderLogo (int size, bool lightBackdrop)
{
    const auto p = lightBackdrop ? lightPalette() : darkPalette();
    juce::Image image (juce::Image::ARGB, size, size, true);
    juce::Graphics g (image);
    drawLogoMark (g, juce::Rectangle<float> ((float) size, (float) size).reduced ((float) size * 0.08f), p.accent, p.accent2);
    return image;
}

namespace
{
/** The mark as it appears in the app icon: the same seven bars, finished as lit objects rather than
    flat colour. Each bar is brighter at the top, catches a thin highlight along its upper edge, and
    sits on a soft shadow, with a faint bloom of the brand colours behind the whole thing. */
void drawLitMark (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour first, juce::Colour second, float scale)
{
    constexpr int bars = 7;
    const auto barWidth = bounds.getWidth() / ((float) bars * 1.45f);
    const auto step = (bounds.getWidth() - barWidth) / (float) (bars - 1);

    juce::Path all;
    std::vector<juce::Rectangle<float>> rects;

    for (int i = 0; i < bars; ++i)
    {
        const auto t = (float) std::abs (i - 3) / 3.0f;
        const auto height = bounds.getHeight() * (0.30f + 0.26f * t);
        const auto centreY = bounds.getY() + bounds.getHeight() * (0.28f + 0.44f * (1.0f - t));
        const juce::Rectangle<float> r (bounds.getX() + (float) i * step, centreY - height * 0.5f, barWidth, height);
        rects.push_back (r);
        all.addRoundedRectangle (r, barWidth * 0.5f);
    }

    // Bloom, then the contact shadow that lifts the bars off the tile.
    juce::DropShadow (first.interpolatedWith (second, 0.5f).withAlpha (0.38f), juce::roundToInt (70.0f * scale), {}).drawForPath (g, all);
    juce::DropShadow (juce::Colours::black.withAlpha (0.65f), juce::roundToInt (34.0f * scale), { 0, juce::roundToInt (26.0f * scale) }).drawForPath (g, all);

    for (int i = 0; i < bars; ++i)
    {
        const auto& r = rects[(size_t) i];
        const auto base = first.interpolatedWith (second, (float) i / (float) (bars - 1));
        const auto radius = r.getWidth() * 0.5f;

        // Body: light at the top, a step darker at the foot.
        g.setGradientFill (juce::ColourGradient (base.brighter (0.20f), r.getX(), r.getY(), base.darker (0.34f), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, radius);

        // A highlight down the left edge, where the light falls.
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.30f), r.getX(), r.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), r.getX(), r.getY() + r.getHeight() * 0.7f, false));
        g.fillRoundedRectangle (r.withWidth (r.getWidth() * 0.28f).translated (r.getWidth() * 0.10f, 0.0f).reduced (0.0f, radius * 0.55f), radius * 0.3f);

        // A hairline edge, so each bar stays crisp against the glow.
        g.setColour (juce::Colours::white.withAlpha (0.16f));
        g.drawRoundedRectangle (r.reduced (0.5f * scale), radius, 1.2f * scale);
    }
}
} // namespace

juce::Image renderAppIcon (int size)
{
    const auto p = darkPalette();
    juce::Image image (juce::Image::ARGB, size, size, true);
    juce::Graphics g (image);

    const auto s = (float) size / 1024.0f;

    // macOS leaves a margin around the artwork so icons sit on one grid in the Dock.
    const auto body = juce::Rectangle<float> (100.0f, 100.0f, 824.0f, 824.0f) * s;
    const auto corner = 186.0f * s;

    juce::Path shape;
    shape.addRoundedRectangle (body, corner);

    g.setColour (juce::Colours::black.withAlpha (0.40f));
    g.fillPath (shape, juce::AffineTransform::translation (0.0f, 16.0f * s));

    g.saveState();
    g.reduceClipRegion (shape);

    // The tile: warm graphite, lit from above, falling to near-black.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3935), 0.0f, body.getY(),
                                             juce::Colour (0xff0b0b0a), 0.0f, body.getBottom(), false));
    g.fillRect (body);

    // Soft light pooled behind the mark, and a cool wash from the lower right.
    g.setGradientFill (juce::ColourGradient (p.accent.withAlpha (0.20f), body.getCentreX(), body.getY() + body.getHeight() * 0.38f,
                                             p.accent.withAlpha (0.0f), body.getCentreX() + body.getWidth() * 0.62f, body.getY() + body.getHeight() * 0.38f, true));
    g.fillRect (body);
    g.setGradientFill (juce::ColourGradient (p.accent2.withAlpha (0.20f), body.getRight(), body.getBottom(),
                                             p.accent2.withAlpha (0.0f), body.getRight() - body.getWidth() * 0.7f, body.getBottom() - body.getHeight() * 0.1f, true));
    g.fillRect (body);

    // Light falling from above: a plain vertical fade, so there is no edge to see.
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.075f), 0.0f, body.getY(),
                                             juce::Colours::white.withAlpha (0.0f), 0.0f, body.getY() + body.getHeight() * 0.42f, false));
    g.fillRect (body);

    // The mark, sized up and centred by its own outline rather than by its box: the bars occupy the top
    // 87% of the box, so the box has to sit lower for the mark to look centred in the tile.
    const auto markSize = 560.0f * s;
    const auto markBox = juce::Rectangle<float> (markSize, markSize).withCentre ({ 512.0f * s, 512.0f * s + markSize * 0.065f });
    drawLitMark (g, markBox, juce::Colour (0xffe6e2d6), juce::Colour (0xff9ab4c2), s);
    g.restoreState();

    // The rim: bright where the light meets it, dim along the bottom.
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.30f), 0.0f, body.getY(),
                                             juce::Colours::white.withAlpha (0.04f), 0.0f, body.getBottom(), false));
    g.drawRoundedRectangle (body.reduced (1.5f * s), corner, 3.0f * s);
    return image;
}

juce::Image renderInstallerBackdrop (int width, int height, float scale)
{
    const auto p = darkPalette();
    const auto w = juce::roundToInt ((float) width * scale), h = juce::roundToInt ((float) height * scale);
    juce::Image image (juce::Image::RGB, w, h, true);
    juce::Graphics g (image);
    g.addTransform (juce::AffineTransform::scale (scale));

    const auto bounds = juce::Rectangle<float> ((float) width, (float) height);
    paintBackdrop (g, bounds, p);

    drawLogoMark (g, juce::Rectangle<float> ((float) width * 0.5f - 22.0f, 22.0f, 44.0f, 40.0f), p.accent, p.accent2);

    g.setColour (juce::Colours::white);
    g.setFont (brandFont (24.0f, true));
    g.drawText ("Install VibeCheck", juce::Rectangle<int> (0, 68, width, 34), juce::Justification::centred);

    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.setFont (brandFont (13.0f));
    g.drawText ("Drag the app onto Applications", juce::Rectangle<int> (0, 102, width, 20), juce::Justification::centred);

    // An arrow between the two icon positions (170,200) and (490,200).
    const auto cy = (float) height * 0.5f + 8.0f;
    juce::Path arrow;
    arrow.startNewSubPath (250.0f, cy);
    arrow.lineTo (404.0f, cy);
    arrow.startNewSubPath (386.0f, cy - 16.0f);
    arrow.lineTo (408.0f, cy);
    arrow.lineTo (386.0f, cy + 16.0f);

    g.setGradientFill (juce::ColourGradient (p.accent, 250.0f, cy, p.accent2, 408.0f, cy, false));
    g.strokePath (arrow, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.setFont (monoFont (10.5f));
    g.drawText ("Make Believe Studios", juce::Rectangle<int> (0, height - 30, width, 16), juce::Justification::centred);

    return image;
}

bool writeInstallerAssets (const juce::File& folder)
{
    if (! folder.createDirectory().wasOk())
        return false;

    const auto save = [] (const juce::Image& image, const juce::File& file)
    {
        file.deleteFile();
        juce::FileOutputStream stream (file);

        if (! stream.openedOk())
            return false;

        juce::PNGImageFormat png;
        return png.writeImageToStream (image, stream);
    };

    return save (renderAppIcon (1024), folder.getChildFile ("icon_1024.png"))
        && save (renderLogo (1024, false), folder.getChildFile ("logo_dark_1024.png"))
        && save (renderLogo (1024, true), folder.getChildFile ("logo_light_1024.png"))
        && save (renderInstallerBackdrop (660, 400, 1.0f), folder.getChildFile ("dmg_background.png"))
        && save (renderInstallerBackdrop (660, 400, 2.0f), folder.getChildFile ("dmg_background@2x.png"));
}
} // namespace mbs
