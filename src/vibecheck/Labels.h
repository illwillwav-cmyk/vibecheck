#pragma once

#include <JuceHeader.h>

#include <map>

namespace vibecheck
{
/** What the person using the app knows about a plugin, as opposed to what the detector guesses.

    A detector can only be improved against plugins whose origin is actually known. These labels are
    that knowledge: they travel in the export, add up in the master list, and are what the
    evaluation report measures the scores against. */
enum class Label
{
    none,
    vibeCoded,     ///< Known to have been generated.
    handWritten    ///< Known to have been written by a person.
};

juce::String toString (Label label);                 ///< "", "ai" or "human": the form used in files.
Label labelFromString (const juce::String& text);
juce::String describe (Label label);                 ///< For people: "Known vibe-coded".

/** A plugin's identity for labelling: its name and maker, so the AudioUnit and VST3 builds of one
    plugin share a label. */
juce::String labelKey (const juce::String& name, const juce::String& manufacturer);
juce::String labelKey (const juce::PluginDescription& description);

Label getLabel (juce::PropertiesFile* settings, const juce::PluginDescription& description);
void setLabel (juce::PropertiesFile* settings, const juce::PluginDescription& description, Label label);
std::map<juce::String, Label> allLabels (juce::PropertiesFile* settings);

/** How well the scores separate plugins of known origin.

    `plugins` is the "plugins" array of an export or of a master list. Reports the score ranges of
    each group, how many each verdict line catches and wrongly flags, the threshold that separates
    them best, and how often each fingerprint appears in each group - which is what says whether a
    fingerprint deserves its weight. */
juce::String evaluateLabels (const juce::var& plugins);
} // namespace vibecheck
