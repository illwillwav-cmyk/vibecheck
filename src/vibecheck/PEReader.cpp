#include "PEReader.h"

#include <cstring>
#include <set>

namespace vibecheck
{
namespace
{
uint16_t u16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
uint32_t u32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
uint64_t u64 (const uint8_t* p) { return (uint64_t) u32 (p) | ((uint64_t) u32 (p + 4) << 32); }

struct Section
{
    uint32_t virtualAddress = 0, virtualSize = 0, rawOffset = 0, rawSize = 0;
};

struct Image
{
    const uint8_t* data = nullptr;
    size_t size = 0;
    std::vector<Section> sections;

    /** Maps a relative virtual address to a file offset, or -1 when it falls outside every section. */
    int64_t offsetOf (uint32_t rva) const
    {
        for (const auto& s : sections)
        {
            const auto span = juce::jmax (s.virtualSize, s.rawSize);

            if (rva >= s.virtualAddress && rva < s.virtualAddress + span)
            {
                const auto offset = (int64_t) s.rawOffset + (rva - s.virtualAddress);
                return offset < (int64_t) size ? offset : -1;
            }
        }

        return -1;
    }

    juce::String cString (int64_t offset, size_t limit = 512) const
    {
        if (offset < 0 || (size_t) offset >= size)
            return {};

        const auto available = juce::jmin (limit, size - (size_t) offset);
        const auto* start = reinterpret_cast<const char*> (data + offset);
        const auto length = ::strnlen (start, available);
        return juce::String (juce::CharPointer_UTF8 (start), length);
    }
};

const char* machineName (uint16_t machine)
{
    switch (machine)
    {
        case 0x8664: return "x86_64";
        case 0xAA64: return "arm64";
        case 0x014c: return "x86";
        default:     return "other";
    }
}

bool printableAscii (uint8_t c) { return c >= 0x20 && c < 0x7f; }

/** Narrow and UTF-16 runs of printable text. Windows keeps its version resource, and much of a
    plugin's own text, as wide strings, so reading only the narrow ones would miss the company name. */
void collectStrings (const Image& image, int maximum, int minimumLength, juce::StringArray& out)
{
    std::set<juce::String> seen;
    const auto add = [&] (const juce::String& text)
    {
        if (out.size() < maximum && seen.insert (text).second)
            out.add (text);
    };

    // Narrow.
    for (size_t i = 0; i < image.size && out.size() < maximum;)
    {
        if (! printableAscii (image.data[i])) { ++i; continue; }

        size_t j = i;

        while (j < image.size && (printableAscii (image.data[j]) || image.data[j] == '\t'))
            ++j;

        if ((int) (j - i) >= minimumLength)
            add (juce::String (juce::CharPointer_UTF8 (reinterpret_cast<const char*> (image.data + i)), j - i).substring (0, 400));

        i = j + 1;
    }

    // Wide: printable ASCII with a zero high byte.
    for (size_t i = 0; i + 1 < image.size && out.size() < maximum;)
    {
        if (! (printableAscii (image.data[i]) && image.data[i + 1] == 0)) { ++i; continue; }

        juce::String text;
        size_t j = i;

        while (j + 1 < image.size && printableAscii (image.data[j]) && image.data[j + 1] == 0)
        {
            text += (juce::juce_wchar) image.data[j];
            j += 2;
        }

        if (text.length() >= minimumLength)
            add (text.substring (0, 400));

        i = j + 2;
    }
}
} // namespace

juce::StringArray classPathFromRtti (const juce::String& decorated)
{
    // ".?AV<name>@<scope>@...@@" for a class, ".?AU" for a struct. Template instances start with '?'
    // inside the name and are skipped: their names are not the author's.
    if (! (decorated.startsWith (".?AV") || decorated.startsWith (".?AU")) || ! decorated.endsWith ("@@"))
        return {};

    const auto body = decorated.substring (4, decorated.length() - 2);

    if (body.isEmpty() || body.contains ("?"))
        return {};

    auto parts = juce::StringArray::fromTokens (body, "@", "");
    parts.removeEmptyStrings();

    for (const auto& part : parts)
        for (auto c : part)
            if (! juce::CharacterFunctions::isLetterOrDigit (c) && c != '_')
                return {};

    // MSVC lists the innermost name first.
    juce::StringArray outermostFirst;

    for (int i = parts.size(); --i >= 0;)
        outermostFirst.add (parts[i]);

    return outermostFirst;
}

MachOFacts readPE (const juce::File& file, int maximumSymbols, int maximumStrings, int minimumStringLength)
{
    juce::MemoryBlock block;

    if (! file.loadFileAsData (block))
    {
        MachOFacts facts;
        facts.error = "could not read " + file.getFileName();
        return facts;
    }

    return readPEFromMemory (static_cast<const uint8_t*> (block.getData()), block.getSize(), maximumSymbols, maximumStrings, minimumStringLength);
}

MachOFacts readPEFromMemory (const uint8_t* data, size_t size, int maximumSymbols, int maximumStrings, int minimumStringLength)
{
    MachOFacts facts;

    if (size < 0x40 || data[0] != 'M' || data[1] != 'Z')
    {
        facts.error = "not a Windows executable";
        return facts;
    }

    const auto peOffset = (size_t) u32 (data + 0x3c);

    if (peOffset + 24 > size || std::memcmp (data + peOffset, "PE\0\0", 4) != 0)
    {
        facts.error = "not a Windows executable";
        return facts;
    }

    const auto* coff = data + peOffset + 4;
    const auto machine = u16 (coff);
    const auto sectionCount = u16 (coff + 2);
    const auto optionalSize = u16 (coff + 16);
    const auto* optional = coff + 20;

    if (peOffset + 24 + optionalSize > size || optionalSize < 96)
    {
        facts.error = "the Windows executable header is damaged";
        return facts;
    }

    const auto magic = u16 (optional);
    const auto isPlus = magic == 0x20b;

    if (magic != 0x10b && ! isPlus)
    {
        facts.error = "unrecognised Windows executable type";
        return facts;
    }

    facts.architecture = machineName (machine);

    const auto directoriesAt = optional + (isPlus ? 112 : 96);
    const auto directoryCount = u32 (optional + (isPlus ? 108 : 92));

    const auto directory = [&] (uint32_t index, uint32_t& address, uint32_t& length)
    {
        address = length = 0;

        if (index < directoryCount && directoriesAt + (index + 1) * 8 <= data + size)
        {
            address = u32 (directoriesAt + index * 8);
            length = u32 (directoriesAt + index * 8 + 4);
        }
    };

    Image image;
    image.data = data;
    image.size = size;

    const auto* table = optional + optionalSize;

    for (uint32_t i = 0; i < sectionCount && table + (i + 1) * 40 <= data + size; ++i)
    {
        const auto* row = table + i * 40;
        image.sections.push_back ({ u32 (row + 12), u32 (row + 8), u32 (row + 20), u32 (row + 16) });
    }

    // --- Exports: a VST3 exports only a handful of entry points, but the names are evidence. ---
    {
        uint32_t address = 0, length = 0;
        directory (0, address, length);

        if (const auto at = image.offsetOf (address); address != 0 && at >= 0 && (size_t) at + 40 <= size)
        {
            const auto* exports = data + at;
            const auto count = juce::jmin ((uint32_t) maximumSymbols, u32 (exports + 24));
            const auto namesAt = image.offsetOf (u32 (exports + 32));

            for (uint32_t i = 0; namesAt >= 0 && i < count && (size_t) namesAt + (i + 1) * 4 <= size; ++i)
            {
                const auto name = image.cString (image.offsetOf (u32 (data + namesAt + i * 4)));

                if (name.isNotEmpty())
                    facts.definedSymbols.add (name);
            }
        }
    }

    // --- Imports: which DLLs, and which functions out of them. ---
    {
        uint32_t address = 0, length = 0;
        directory (1, address, length);
        auto at = image.offsetOf (address);

        for (int entry = 0; address != 0 && at >= 0 && (size_t) at + 20 <= size && entry < 512; ++entry, at += 20)
        {
            const auto* descriptor = data + at;
            const auto nameRva = u32 (descriptor + 12);

            if (nameRva == 0)
                break;

            facts.linkedLibraries.add (image.cString (image.offsetOf (nameRva), 256));

            const auto thunkRva = u32 (descriptor) != 0 ? u32 (descriptor) : u32 (descriptor + 16);
            auto thunk = image.offsetOf (thunkRva);
            const auto width = isPlus ? 8 : 4;

            for (int n = 0; thunk >= 0 && (size_t) thunk + width <= size && facts.undefinedSymbols.size() < maximumSymbols; ++n, thunk += width)
            {
                const auto value = isPlus ? u64 (data + thunk) : (uint64_t) u32 (data + thunk);

                if (value == 0)
                    break;

                const auto byOrdinal = (value >> (isPlus ? 63 : 31)) & 1;

                if (byOrdinal)
                    continue;

                // The first two bytes are a hint; the name follows. A leading underscore matches how
                // the same C functions are spelt in a Mach-O symbol table, so the scoring engine can
                // compare them without knowing the platform.
                const auto nameAt = image.offsetOf ((uint32_t) value);
                const auto name = nameAt >= 0 ? image.cString (nameAt + 2) : juce::String();

                if (name.isNotEmpty())
                    facts.undefinedSymbols.add ("_" + name);
            }
        }
    }

    // --- Signature: the security directory holds an Authenticode blob, by file offset. ---
    {
        uint32_t address = 0, length = 0;
        directory (4, address, length);
        facts.hasSignature = address != 0 && length != 0;
        facts.signingIdentifier = facts.hasSignature ? "Authenticode" : juce::String();
    }

    collectStrings (image, maximumStrings, minimumStringLength, facts.strings);

    // --- Class names. MSVC writes each polymorphic class's decorated name into the binary; turn
    // them into the Itanium-style names the scoring engine already understands. ---
    {
        std::set<juce::String> seen;

        for (const auto& text : facts.strings)
        {
            if (! text.startsWith (".?A"))
                continue;

            const auto path = classPathFromRtti (text);

            if (path.isEmpty())
                continue;

            // __ZN<len><name><len><name>E : the nested-name form userScopes() reads.
            juce::String mangled = "__ZN";

            for (const auto& part : path)
                mangled << juce::String (part.length()) << part;

            mangled << "E";

            if (seen.insert (mangled).second && facts.definedSymbols.size() < maximumSymbols)
                facts.definedSymbols.add (mangled);
        }
    }

    facts.ok = true;
    return facts;
}
} // namespace vibecheck
