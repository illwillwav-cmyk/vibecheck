#include "SplashScreen.h"

#include "AppIcon.h"
#include "BrandAssets.h"
#include "Theme.h"
#include "Widgets.h"

namespace
{
constexpr int frameMs = 16;

void drawLogoMarkAlpha (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour a, juce::Colour b)
{
    mbs::drawLogoMark (g, area, a, b);
}
}

VibeSplashScreen::VibeSplashScreen (std::function<void()> onDismissed)
    : dismissed (std::move (onDismissed))
{
    setInterceptsMouseClicks (true, true);
    setWantsKeyboardFocus (true);

    enterButton.setButtonText ("Find out");
    mbs::setButtonStyle (enterButton, mbs::ButtonStyle::primary);
    addAndMakeVisible (enterButton);
    enterButton.onClick = [this] { dismiss(); };

    startTimer (frameMs);
}

void VibeSplashScreen::dismiss()
{
    leaving = true;
}

void VibeSplashScreen::timerCallback()
{
    clock += (float) frameMs / 1000.0f;
    appearance += leaving ? -0.07f : 0.045f;

    if (leaving && appearance <= 0.0f)
    {
        stopTimer();
        setVisible (false);

        if (dismissed != nullptr)
            dismissed();

        return;
    }

    appearance = juce::jmin (1.0f, appearance);
    repaint();
}

void VibeSplashScreen::mouseDown (const juce::MouseEvent&)
{
    dismiss();
}

bool VibeSplashScreen::keyPressed (const juce::KeyPress&)
{
    dismiss();
    return true;
}

void VibeSplashScreen::paint (juce::Graphics& g)
{
    const auto& p = mbs::theme();
    const auto fade = juce::jlimit (0.0f, 1.0f, appearance);
    const auto eased = 1.0f - std::pow (1.0f - fade, 3.0f);

    g.fillAll (p.paper);

    auto area = getLocalBounds();
    const auto centre = area.getCentre().toFloat();

    // Two slow glows behind everything, one per accent, drifting against each other.
    const auto drift = std::sin (clock * 0.5f) * 40.0f;
    const auto radius = (float) juce::jmax (area.getWidth(), area.getHeight()) * 0.55f;

    g.setGradientFill (juce::ColourGradient (p.accent.withAlpha (0.20f * eased), centre.x - 120.0f + drift, centre.y - 60.0f,
                                             p.accent.withAlpha (0.0f), centre.x - 120.0f + drift, centre.y - 60.0f + radius, true));
    g.fillAll();
    g.setGradientFill (juce::ColourGradient (p.accent2.withAlpha (0.13f * eased), centre.x + 160.0f - drift, centre.y + 80.0f,
                                             p.accent2.withAlpha (0.0f), centre.x + 160.0f - drift, centre.y + 80.0f + radius * 0.8f, true));
    g.fillAll();

    const auto lift = (1.0f - eased) * 16.0f;
    const auto blockTop = centre.y - 190.0f + lift;

    // The seal, inside a ring that pulses once a beat.
    const auto sealSize = juce::jmin (112.0f, (float) area.getHeight() * 0.18f);
    const auto sealBox = juce::Rectangle<float> (sealSize, sealSize).withCentre ({ centre.x, blockTop + sealSize * 0.5f });
    const auto beat = 0.5f + 0.5f * std::sin (clock * 3.2f);

    g.setColour (p.accent.withAlpha (0.10f * eased * (1.0f - beat)));
    g.drawRoundedRectangle (sealBox.expanded (14.0f + beat * 12.0f), sealBox.getWidth() * 0.26f + 14.0f, 1.5f);
    g.setColour (p.line.withAlpha (eased));
    

    g.setGradientFill (juce::ColourGradient (p.cardHi.withAlpha (eased), sealBox.getX(), sealBox.getY(), p.card.withAlpha (eased), sealBox.getRight(), sealBox.getBottom(), false));
    g.fillRoundedRectangle (sealBox, sealBox.getWidth() * 0.26f);
    g.setColour (p.line.withAlpha (eased));
    g.drawRoundedRectangle (sealBox.reduced (0.5f), sealBox.getWidth() * 0.26f, 1.0f);
    drawLogoMarkAlpha (g, sealBox.reduced (sealBox.getWidth() * 0.2f), p.accent.withAlpha (eased), p.accent2.withAlpha (eased));

    // The question, in a gradient.
    auto titleArea = juce::Rectangle<int> (area.getWidth(), 58).withCentre ({ (int) centre.x, (int) (blockTop + sealSize + 56.0f) });
    g.setGradientFill (juce::ColourGradient (p.ink.withAlpha (eased), (float) titleArea.getX() + (float) titleArea.getWidth() * 0.3f, 0.0f,
                                             p.accent.interpolatedWith (p.ink, 0.25f).withAlpha (eased),
                                             (float) titleArea.getRight() - (float) titleArea.getWidth() * 0.3f, 0.0f, false));
    g.setFont (mbs::brandFont (46.0f, true));
    g.drawText ("What's the vibe?", titleArea, juce::Justification::centred);

    g.setColour (p.inkMuted.withAlpha (eased));
    g.setFont (mbs::brandFont (14.5f));
    g.drawText ("Measure what a plugin does to your signal. Check what its binary says about how it was made.",
                juce::Rectangle<int> (area.getWidth(), 24).withCentre ({ (int) centre.x, titleArea.getBottom() + 18 }),
                juce::Justification::centred);

    // A live equaliser strip: a signal passing through, which is what the app is about.
    const auto barCount = juce::jlimit (20, 64, area.getWidth() / 16);
    const auto stripWidth = juce::jmin (560.0f, (float) area.getWidth() - 80.0f);
    const auto barStep = stripWidth / (float) barCount;
    const auto stripY = (float) titleArea.getBottom() + 62.0f;
    const auto stripHeight = 54.0f;

    for (int i = 0; i < barCount; ++i)
    {
        const auto t = (float) i / (float) (barCount - 1);
        const auto envelope = std::sin (t * juce::MathConstants<float>::pi);
        const auto motion = 0.5f + 0.5f * std::sin (clock * 2.4f + (float) i * 0.55f)
                                    * std::sin (clock * 1.3f - (float) i * 0.21f);
        const auto h = juce::jmax (3.0f, stripHeight * envelope * (0.18f + 0.82f * motion) * eased);
        const auto x = centre.x - stripWidth * 0.5f + (float) i * barStep + barStep * 0.5f;

        g.setColour (p.accent.interpolatedWith (p.accent2, t).withAlpha (0.35f + 0.65f * envelope));
        g.fillRoundedRectangle (x - 1.5f, stripY + stripHeight * 0.5f - h * 0.5f, 3.0f, h, 1.5f);
    }

    // Footer.
    if (const auto mark = mbs::wordmark(); mark.isValid())
    {
        const auto w = juce::jmin (150, area.getWidth() / 4);
        const auto h = juce::roundToInt ((float) w * (float) mark.getHeight() / (float) mark.getWidth());
        g.setOpacity (0.6f * eased);
        g.drawImage (mark, juce::Rectangle<int> (area.getCentreX() - w / 2, area.getBottom() - h - 28, w, h).toFloat(),
                     juce::RectanglePlacement::centred);
        g.setOpacity (1.0f);
    }

    g.setColour (p.inkFaint.withAlpha (eased));
    g.setFont (mbs::monoFont (11.0f));
    g.drawText ("or press any key", juce::Rectangle<int> (area.getWidth(), 18).withCentre ({ area.getCentreX(), enterButton.getBottom() + 22 }),
                juce::Justification::centred);
}

void VibeSplashScreen::resized()
{
    auto area = getLocalBounds();
    const auto centre = area.getCentre();
    const auto y = (int) ((float) centre.y - 190.0f + juce::jmin (112.0f, (float) area.getHeight() * 0.18f) + 56.0f + 29.0f + 18.0f + 12.0f + 62.0f + 54.0f + 22.0f);
    enterButton.setBounds (juce::Rectangle<int> (168, 42).withCentre ({ centre.x, y + 21 }));
}
