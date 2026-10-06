#pragma once

#include <JuceHeader.h>

/** Stand-in for a tab whose contents arrive in a later phase. It states what will live here
    so the shape of the app is visible while the foundation is being reviewed. */
class PlaceholderTab final : public juce::Component
{
public:
    PlaceholderTab (juce::String titleIn, juce::String bodyIn);

    void paint (juce::Graphics& g) override;

private:
    juce::String title, body;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaceholderTab)
};
