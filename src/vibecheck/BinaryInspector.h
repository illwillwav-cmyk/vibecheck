#pragma once

#include <JuceHeader.h>

namespace vibecheck
{
/** What could be read out of a plugin's compiled binary.

    Everything here is evidence, and the absence of evidence is recorded just as carefully as its
    presence: a stripped or encrypted binary yields nothing, and the scoring engine has to know
    the difference between "no fingerprints found" and "nothing could be read". */
struct BinaryFacts
{
    bool ok = false;
    juce::String error;

    juce::File bundle;
    juce::File executable;
    juce::int64 sizeInBytes = 0;

    juce::StringArray definedSymbols;     ///< Exported/defined symbols, when not stripped.
    juce::StringArray undefinedSymbols;   ///< What the binary calls out to.
    juce::StringArray linkedLibraries;
    juce::StringArray strings;            ///< Printable strings, capped.
    juce::String infoPlist;
    juce::String moduleInfo;              ///< A VST3's moduleinfo.json, when it has one.

    // Who signed it, from the embedded code signature.
    bool hasSignature = false;
    bool adHocSigned = false;
    juce::String signingIdentifier;
    juce::String teamIdentifier;          ///< Set only for a binary signed by an Apple developer team.
    juce::String architecture;

    juce::String platform;                ///< "macOS" or "Windows": which kind of file this was.

    /** False where a shipping binary normally carries no symbol table at all, as on Windows. Without
        this a Windows plugin would always read as stripped and so as unreadable. */
    bool symbolTableExpected = true;

    bool stripped = false;
    bool paceWrapped = false;

    /** True when there is enough here to say anything at all. */
    bool readable() const { return ok && ! paceWrapped && (! definedSymbols.isEmpty() || strings.size() > 50); }

    /** Why the binary could not be read, if it could not. */
    juce::String opacityReason() const;
};

/** Reads facts out of a compiled plugin.

    The binary is parsed directly (see MachOReader), so nothing here depends on Xcode's command
    line tools being installed. */
class BinaryInspector
{
public:
    virtual ~BinaryInspector() = default;
    virtual BinaryFacts inspect (const juce::PluginDescription& description) = 0;

    /** Where this plugin's bundle sits, without reading it. A sweep uses this to inspect each
        binary once: several plugins often share one, and every Apple unit shares CoreAudio. */
    virtual juce::File locate (const juce::PluginDescription& description) = 0;

    /** Inspects a bundle that has already been located. */
    virtual BinaryFacts inspectBundle (const juce::File& bundle) = 0;
};

std::unique_ptr<BinaryInspector> createBinaryInspector();
} // namespace vibecheck
