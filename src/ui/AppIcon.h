#pragma once

#include <JuceHeader.h>

namespace mbs
{
/** The VibeCheck mark: seven rounded bars whose centres run along a V, tall at the ends and short at
    the point. It reads as a V for vibe and as a level meter for sound. Drawn from geometry rather
    than an image, so it is sharp at any size. */
void drawLogoMark (juce::Graphics&, juce::Rectangle<float> bounds, juce::Colour first, juce::Colour second);

/** The mark on its own over a transparent background. */
juce::Image renderLogo (int size, bool lightBackdrop);

/** The application icon, drawn at any size: a rounded square carrying the studio seal over a
    live-looking waveform. Drawn in code from the brand assets so it never drifts from them. */
juce::Image renderAppIcon (int size);

/** The backdrop of the installer disk image: a prompt to drag the app into Applications. The
    window is `width` by `height` points; `scale` makes a retina rendering of the same layout. */
juce::Image renderInstallerBackdrop (int width, int height, float scale);

/** Writes every installer image into a folder. Used by `VibeCheck --render-assets=<folder>`. */
bool writeInstallerAssets (const juce::File& folder);
} // namespace mbs
