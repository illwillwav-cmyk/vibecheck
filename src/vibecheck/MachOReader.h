#pragma once

#include <JuceHeader.h>

namespace vibecheck
{
/** What can be read straight out of a Mach-O executable, with no external tools.

    The first version shelled out to nm, otool and strings. Those are stubs on a Mac without
    Xcode's command line tools, which opens an installer dialog and returns nothing, so on most
    people's machines every plugin would have looked unreadable. Reading the file directly also
    avoids the per-architecture header lines nm prints for a universal binary, which were being
    counted as symbols. */
struct MachOFacts
{
    bool ok = false;
    juce::String error;

    juce::StringArray definedSymbols, undefinedSymbols, linkedLibraries, strings;

    bool hasSignature = false;
    bool adHocSigned = false;
    juce::String signingIdentifier;
    juce::String teamIdentifier;   ///< Present only when signed by an Apple developer team.

    juce::String architecture;
};

/** Reads one file. A universal binary is read at its arm64 slice, or its first if there is none. */
MachOFacts readMachO (const juce::File& file, int maximumSymbols, int maximumStrings, int minimumStringLength = 4);
} // namespace vibecheck
