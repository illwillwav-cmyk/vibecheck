#pragma once

#include <JuceHeader.h>

/** The way in: the studio seal, a live waveform, and the only question the app really asks.

    Covers the whole window until it is dismissed, then fades out and gets out of the way. */
class VibeSplashScreen final : public juce::Component,
                               private juce::Timer
{
public:
    explicit VibeSplashScreen (std::function<void()> onDismissed);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    void dismiss();

private:
    void timerCallback() override;

    std::function<void()> dismissed;
    juce::TextButton enterButton { "Find out" };
    float appearance = 0.0f;   ///< 0 is invisible, 1 fully drawn.
    float clock = 0.0f;        ///< Seconds since the splash appeared; drives the waveform.
    bool leaving = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VibeSplashScreen)
};
