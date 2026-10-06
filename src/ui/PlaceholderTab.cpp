#include "PlaceholderTab.h"

#include "Theme.h"

PlaceholderTab::PlaceholderTab (juce::String titleIn, juce::String bodyIn)
    : title (std::move (titleIn)), body (std::move (bodyIn))
{
}

void PlaceholderTab::paint (juce::Graphics& g)
{
    g.fillAll (mbs::theme().paper);

    auto area = getLocalBounds().reduced (32);

    g.setColour (mbs::theme().ink);
    g.setFont (mbs::brandFont (22.0f, true));
    g.drawText (title, area.removeFromTop (34), juce::Justification::topLeft);

    area.removeFromTop (12);

    g.setColour (mbs::theme().inkMuted);
    g.setFont (mbs::brandFont (15.0f));
    g.drawFittedText (body, area, juce::Justification::topLeft, 20);
}
