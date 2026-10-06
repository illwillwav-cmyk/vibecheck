#include "BinaryInspector.h"

#include "MachOReader.h"
#include "PEReader.h"

#include <map>

namespace vibecheck
{
namespace
{
/** Caps on how much is read, so a huge plugin cannot stall the UI or eat memory. */
constexpr int maximumStrings = 200000;

#if JUCE_MAC
juce::File executableInsideBundle (const juce::File& bundle)
{
    if (bundle.existsAsFile())
        return bundle;

    const auto macOsFolder = bundle.getChildFile ("Contents/MacOS");

    if (! macOsFolder.isDirectory())
        return {};

    // The executable is the only file in a bundle's MacOS folder.
    const auto files = macOsFolder.findChildFiles (juce::File::findFiles, false);
    return files.isEmpty() ? juce::File() : files.getFirst();
}

juce::String xmlEscaped (juce::String text)
{
    return text.replace ("&", "&amp;").replace ("<", "&lt;").replace (">", "&gt;");
}

/** An AudioUnit is named by component codes rather than a path, so its bundle has to be found by
    matching those codes against the Info.plist of everything installed. That is done once, into an
    index, rather than by walking every bundle for every plugin: with a few hundred units the walk
    was hundreds of file reads per plugin. */
struct AudioUnitIndex
{
    std::map<juce::String, juce::File> bySubtype, byName;

    AudioUnitIndex()
    {
        juce::Array<juce::File> searchPaths {
            juce::File ("/Library/Audio/Plug-Ins/Components"),
            juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Plug-Ins/Components"),

            // Apple's own units are not installed like third-party ones; several of them share a
            // single system bundle.
            juce::File ("/System/Library/Components")
        };

        for (const auto& folder : searchPaths)
        {
            if (! folder.isDirectory())
                continue;

            for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*.component", juce::File::findDirectories))
            {
                const auto bundle = entry.getFile();
                const auto plist = bundle.getChildFile ("Contents/Info.plist");

                if (! plist.existsAsFile())
                    continue;

                byName.emplace (bundle.getFileNameWithoutExtension().toLowerCase(), bundle);

                // A bundle can register several components, each with its own subtype code.
                const auto text = plist.loadFileAsString();

                for (int at = text.indexOf ("<key>subtype</key>"); at >= 0; at = text.indexOf (at + 10, "<key>subtype</key>"))
                {
                    const auto open = text.indexOf (at, "<string>");
                    const auto close = text.indexOf (at, "</string>");

                    if (open >= 0 && close > open)
                        bySubtype.emplace (text.substring (open + 8, close).trim(), bundle);
                }
            }
        }
    }

    juce::File find (const juce::PluginDescription& description) const
    {
        juce::StringArray parts;
        parts.addTokens (description.fileOrIdentifier.fromLastOccurrenceOf ("/", false, false), ",", "");
        parts.trim();

        // The subtype is the distinguishing code; manufacturer codes repeat across a vendor's range.
        if (parts.size() >= 2 && parts[1].isNotEmpty())
            if (const auto found = bySubtype.find (xmlEscaped (parts[1])); found != bySubtype.end())
                return found->second;

        if (const auto found = byName.find (description.name.toLowerCase()); found != byName.end())
            return found->second;

        return {};
    }
};

class MachOInspector final : public BinaryInspector
{
public:
    juce::File locate (const juce::PluginDescription& description) override
    {
        if (juce::File::isAbsolutePath (description.fileOrIdentifier))
            if (const juce::File file (description.fileOrIdentifier); file.exists())
                return file;

        if (index == nullptr)
            index = std::make_unique<AudioUnitIndex>();

        return index->find (description);
    }

    BinaryFacts inspect (const juce::PluginDescription& description) override
    {
        return inspectBundle (locate (description));
    }

    BinaryFacts inspectBundle (const juce::File& bundle) override
    {
        BinaryFacts facts;

        if (bundle == juce::File() || ! bundle.exists())
        {
            facts.error = "could not find this plugin's binary on disk";
            return facts;
        }

        facts.bundle = bundle;
        facts.executable = executableInsideBundle (bundle);

        if (facts.executable == juce::File() || ! facts.executable.existsAsFile())
        {
            facts.error = "the bundle contains no executable";
            return facts;
        }

        facts.sizeInBytes = facts.executable.getSize();
        facts.platform = "macOS";
        facts.infoPlist = bundle.getChildFile ("Contents/Info.plist").loadFileAsString();

        // Copy protection wraps the real code in an encrypted payload, so everything below will
        // come back empty. Worth knowing before drawing any conclusion from that emptiness.
        facts.paceWrapped = bundle.getChildFile ("Contents/PlugIns/__Pace_Eden").exists()
                            || bundle.getChildFile ("Contents/__Pace_Eden").exists()
                            || bundle.getChildFile ("Contents/MacOS/__Pace_Eden").exists();

        // VST3 bundles may carry their own description of the vendor.
        facts.moduleInfo = bundle.getChildFile ("Contents/moduleinfo.json").loadFileAsString();

        const auto read = readMachO (facts.executable, maximumStrings, maximumStrings);

        if (! read.ok)
        {
            facts.error = read.error;
            return facts;
        }

        facts.definedSymbols   = read.definedSymbols;
        facts.undefinedSymbols = read.undefinedSymbols;
        facts.linkedLibraries  = read.linkedLibraries;
        facts.strings          = read.strings;
        facts.hasSignature     = read.hasSignature;
        facts.adHocSigned      = read.adHocSigned;
        facts.signingIdentifier = read.signingIdentifier;
        facts.teamIdentifier   = read.teamIdentifier;
        facts.architecture     = read.architecture;

        facts.stripped = facts.definedSymbols.isEmpty();

        // PACE's wrapper leaves its own name in the binary. A mention of iLok on its own is not
        // enough: an unwrapped plugin that merely uses iLok licensing says the same.
        if (! facts.paceWrapped)
            for (const auto& line : facts.strings)
                if (line.contains ("PACE Anti-Piracy") || line.contains ("WrapHelper") || line.contains ("PACE Eden"))
                {
                    facts.paceWrapped = true;
                    break;
                }

        facts.ok = true;
        return facts;
    }

private:
    std::unique_ptr<AudioUnitIndex> index;
};
#endif   // JUCE_MAC

/** Reads Windows plugins: a .vst3 is either a single DLL or a bundle folder with the DLL inside. */
class PEInspector final : public BinaryInspector
{
public:
    juce::File locate (const juce::PluginDescription& description) override
    {
        if (juce::File::isAbsolutePath (description.fileOrIdentifier))
            if (const juce::File file (description.fileOrIdentifier); file.exists())
                return file;

        return {};
    }

    BinaryFacts inspect (const juce::PluginDescription& description) override
    {
        return inspectBundle (locate (description));
    }

    BinaryFacts inspectBundle (const juce::File& bundle) override
    {
        BinaryFacts facts;

        if (bundle == juce::File() || ! bundle.exists())
        {
            facts.error = "could not find this plugin's binary on disk";
            return facts;
        }

        facts.bundle = bundle;
        facts.executable = binaryInside (bundle);

        if (facts.executable == juce::File() || ! facts.executable.existsAsFile())
        {
            facts.error = "the bundle contains no Windows binary";
            return facts;
        }

        facts.sizeInBytes = facts.executable.getSize();
        facts.platform = "Windows";
        facts.symbolTableExpected = false;
        facts.moduleInfo = bundle.getChildFile ("Contents/moduleinfo.json").loadFileAsString();

        const auto read = readPE (facts.executable, maximumStrings, maximumStrings);

        if (! read.ok)
        {
            facts.error = read.error;
            return facts;
        }

        facts.definedSymbols    = read.definedSymbols;
        facts.undefinedSymbols  = read.undefinedSymbols;
        facts.linkedLibraries   = read.linkedLibraries;
        facts.strings           = read.strings;
        facts.hasSignature      = read.hasSignature;
        facts.signingIdentifier = read.signingIdentifier;
        facts.architecture      = read.architecture;

        // MSVC leaves no symbol table in a release build, so having none says nothing.
        facts.stripped = false;

        for (const auto& line : facts.strings)
            if (line.contains ("PACE Anti-Piracy") || line.contains ("WrapHelper") || line.contains ("PACE Eden"))
            {
                facts.paceWrapped = true;
                break;
            }

        facts.ok = true;
        return facts;
    }

private:
    static juce::File binaryInside (const juce::File& bundle)
    {
        if (bundle.existsAsFile())
            return bundle;

        for (const auto* folder : { "x86_64-win", "arm64ec-win", "arm64-win", "x86-win" })
        {
            const auto dir = bundle.getChildFile ("Contents").getChildFile (folder);

            if (! dir.isDirectory())
                continue;

            const auto files = dir.findChildFiles (juce::File::findFiles, false, "*.vst3;*.dll");

            if (! files.isEmpty())
                return files.getFirst();
        }

        return {};
    }
};
} // namespace

juce::String BinaryFacts::opacityReason() const
{
    if (! ok)
        return error;

    if (paceWrapped)
        return "the binary is wrapped by copy protection, so its real code is encrypted and "
               "nothing can be read from it";

    if (stripped && strings.size() <= 50)
        return "the binary is stripped and holds almost no readable strings";

    if (stripped)
        return "the binary is stripped, so no symbol names survive";

    return {};
}

std::unique_ptr<BinaryInspector> createBinaryInspector()
{
   #if JUCE_MAC
    return std::make_unique<MachOInspector>();
   #else
    return std::make_unique<PEInspector>();
   #endif
}
} // namespace vibecheck
