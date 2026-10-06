#pragma once

#include <JuceHeader.h>

#include "MachOReader.h"

#include <cstdint>
#include <vector>

namespace vibecheck
{
/** What can be read out of a Windows executable (a plugin's .vst3 is a DLL), with no external tools.

    Windows binaries carry no symbol table, so the evidence comes from elsewhere: the exported
    functions, the DLLs and functions it imports, the C++ class names that MSVC leaves behind in its
    run-time type information, the strings (narrow and wide) and the version resource, which holds
    the company name and copyright text. The result reuses the Mach-O fact structure so the scoring
    engine need not care which platform a binary came from. */
MachOFacts readPE (const juce::File& file, int maximumSymbols, int maximumStrings, int minimumStringLength = 4);

/** The same, over bytes already in memory. Lets the parser be tested without a file. */
MachOFacts readPEFromMemory (const uint8_t* data, size_t size, int maximumSymbols, int maximumStrings, int minimumStringLength = 4);

/** Turns an MSVC run-time type name such as ".?AVEditor@Sway@@" into the class path
    {"Sway", "Editor"}, outermost first. Returns an empty array for anything else. */
juce::StringArray classPathFromRtti (const juce::String& decorated);
} // namespace vibecheck
