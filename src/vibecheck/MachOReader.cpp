#include "MachOReader.h"

#if JUCE_MAC

#include <mach-o/fat.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <libkern/OSByteOrder.h>

#include <cstring>

namespace vibecheck
{
namespace
{
const char* cpuName (cpu_type_t type)
{
    if (type == CPU_TYPE_ARM64)  return "arm64";
    if (type == CPU_TYPE_X86_64) return "x86_64";
    return "other";
}

/** A NUL-terminated string at an offset, never reading past the end of the mapped file. */
juce::String stringAt (const uint8_t* base, size_t size, size_t offset, size_t limit = 4096)
{
    if (offset >= size)
        return {};

    const auto available = juce::jmin (limit, size - offset);
    const auto* start = reinterpret_cast<const char*> (base + offset);
    const auto length = ::strnlen (start, available);
    return juce::String (juce::CharPointer_UTF8 (start), length);
}

uint32_t readBE32 (const uint8_t* p) { return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3]; }

/** Looks inside the embedded code signature for who signed it. */
void readSignature (const uint8_t* base, size_t size, size_t sliceStart, uint32_t dataOffset, uint32_t dataSize, MachOFacts& facts)
{
    const auto blobStart = sliceStart + dataOffset;

    if (blobStart + 12 > size || dataSize < 12)
        return;

    const auto* blob = base + blobStart;

    if (readBE32 (blob) != 0xfade0cc0)   // CSMAGIC_EMBEDDED_SIGNATURE
        return;

    facts.hasSignature = true;
    const auto count = readBE32 (blob + 8);

    for (uint32_t i = 0; i < juce::jmin<uint32_t> (count, 16); ++i)
    {
        const auto entry = 12 + (size_t) i * 8;

        if (blobStart + entry + 8 > size)
            return;

        const auto type = readBE32 (blob + entry);
        const auto offset = readBE32 (blob + entry + 4);

        if (type != 0 || blobStart + offset + 52 > size)   // slot 0 is the code directory
            continue;

        const auto* dir = blob + offset;

        if (readBE32 (dir) != 0xfade0c02)
            continue;

        const auto version = readBE32 (dir + 8);
        const auto flags = readBE32 (dir + 12);
        const auto identOffset = readBE32 (dir + 20);

        facts.adHocSigned = (flags & 0x2) != 0;
        facts.signingIdentifier = stringAt (base, size, blobStart + offset + identOffset, 256);

        // The team identifier arrived in version 2.2 of the format.
        if (version >= 0x20200 && blobStart + offset + 52 <= size)
        {
            const auto teamOffset = readBE32 (dir + 48);

            if (teamOffset != 0)
                facts.teamIdentifier = stringAt (base, size, blobStart + offset + teamOffset, 64);
        }

        return;
    }
}

void collectStrings (const uint8_t* base, size_t size, int maximum, int minimumLength, juce::StringArray& out)
{
    size_t runStart = 0;
    bool inRun = false;

    for (size_t i = 0; i <= size; ++i)
    {
        const auto c = i < size ? base[i] : 0;
        const auto printable = (c >= 0x20 && c < 0x7f) || c == '\t';

        if (printable)
        {
            if (! inRun) { runStart = i; inRun = true; }
            continue;
        }

        // A real string ends in a NUL or a newline. Runs of printable bytes that stop at anything
        // else are almost always machine code that happens to look like text.
        const auto terminated = i == size || c == 0 || c == '\n';

        if (inRun && terminated && (int) (i - runStart) >= minimumLength)
        {
            out.add (juce::String (juce::CharPointer_UTF8 (reinterpret_cast<const char*> (base + runStart)), i - runStart));

            if (out.size() >= maximum)
                return;
        }

        inRun = false;
    }
}
} // namespace

MachOFacts readMachO (const juce::File& file, int maximumSymbols, int maximumStrings, int minimumStringLength)
{
    MachOFacts facts;

    juce::MemoryMappedFile mapped (file, juce::MemoryMappedFile::readOnly);

    if (mapped.getData() == nullptr || mapped.getSize() < 32)
    {
        facts.error = "could not read the binary";
        return facts;
    }

    const auto* base = static_cast<const uint8_t*> (mapped.getData());
    const auto size = (size_t) mapped.getSize();

    // Strings come from the whole file, as `strings -a` does, so they cover every slice.
    collectStrings (base, size, maximumStrings, minimumStringLength, facts.strings);

    // Choose a slice.
    size_t sliceStart = 0, sliceSize = size;
    uint32_t magic;
    std::memcpy (&magic, base, 4);

    if (magic == FAT_CIGAM || magic == FAT_CIGAM_64)
    {
        const auto is64 = magic == FAT_CIGAM_64;
        const auto count = readBE32 (base + 4);
        const auto entrySize = is64 ? 32u : 20u;
        bool chosen = false;

        for (uint32_t i = 0; i < juce::jmin<uint32_t> (count, 8); ++i)
        {
            const auto* entry = base + 8 + (size_t) i * entrySize;

            if ((size_t) (entry - base) + entrySize > size)
                break;

            const auto cpu = (cpu_type_t) readBE32 (entry);
            uint64_t offset = readBE32 (entry + 8), length = readBE32 (entry + 12);

            if (is64)
            {
                offset = ((uint64_t) readBE32 (entry + 8) << 32) | readBE32 (entry + 12);
                length = ((uint64_t) readBE32 (entry + 16) << 32) | readBE32 (entry + 20);
            }

            if (offset >= size)
                continue;

            if (! chosen || cpu == CPU_TYPE_ARM64)
            {
                sliceStart = (size_t) offset;
                sliceSize = (size_t) juce::jmin<uint64_t> (length, size - offset);
                facts.architecture = cpuName (cpu);
                chosen = true;
            }
        }

        if (! chosen)
        {
            facts.error = "a universal binary with no readable slice";
            return facts;
        }

        std::memcpy (&magic, base + sliceStart, 4);
    }

    if (magic != MH_MAGIC_64)
    {
        facts.error = "not a 64-bit Mach-O binary";
        return facts;
    }

    const auto* slice = base + sliceStart;
    const auto* header = reinterpret_cast<const mach_header_64*> (slice);

    if (facts.architecture.isEmpty())
        facts.architecture = cpuName (header->cputype);

    size_t cursor = sizeof (mach_header_64);
    const auto end = juce::jmin<size_t> (sliceSize, sizeof (mach_header_64) + header->sizeofcmds);

    for (uint32_t i = 0; i < header->ncmds; ++i)
    {
        if (cursor + sizeof (load_command) > end)
            break;

        const auto* command = reinterpret_cast<const load_command*> (slice + cursor);

        if (command->cmdsize < sizeof (load_command) || cursor + command->cmdsize > end)
            break;

        switch (command->cmd)
        {
            case LC_SYMTAB:
            {
                const auto* symtab = reinterpret_cast<const symtab_command*> (command);

                if ((uint64_t) symtab->symoff + (uint64_t) symtab->nsyms * sizeof (nlist_64) > sliceSize
                    || (uint64_t) symtab->stroff + symtab->strsize > sliceSize)
                    break;

                const auto* symbols = reinterpret_cast<const nlist_64*> (slice + symtab->symoff);
                const auto* strings = reinterpret_cast<const char*> (slice + symtab->stroff);

                for (uint32_t s = 0; s < symtab->nsyms; ++s)
                {
                    const auto& symbol = symbols[s];

                    if ((symbol.n_type & N_STAB) != 0 || symbol.n_un.n_strx == 0 || symbol.n_un.n_strx >= symtab->strsize)
                        continue;

                    const auto type = symbol.n_type & N_TYPE;
                    const auto maximumLength = (size_t) (symtab->strsize - symbol.n_un.n_strx);
                    const auto length = ::strnlen (strings + symbol.n_un.n_strx, maximumLength);
                    const juce::String name (juce::CharPointer_UTF8 (strings + symbol.n_un.n_strx), length);

                    if (type == N_SECT && facts.definedSymbols.size() < maximumSymbols)
                        facts.definedSymbols.add (name);
                    else if (type == N_UNDF && (symbol.n_type & N_EXT) && facts.undefinedSymbols.size() < maximumSymbols)
                        facts.undefinedSymbols.add (name);
                }

                break;
            }

            case LC_LOAD_DYLIB:
            case LC_LOAD_WEAK_DYLIB:
            case LC_REEXPORT_DYLIB:
            case LC_LAZY_LOAD_DYLIB:
            {
                const auto* dylib = reinterpret_cast<const dylib_command*> (command);

                if (dylib->dylib.name.offset < command->cmdsize)
                    facts.linkedLibraries.add (stringAt (slice, sliceSize, cursor + dylib->dylib.name.offset, command->cmdsize - dylib->dylib.name.offset));

                break;
            }

            case LC_CODE_SIGNATURE:
            {
                const auto* data = reinterpret_cast<const linkedit_data_command*> (command);
                readSignature (base, size, sliceStart, data->dataoff, data->datasize, facts);
                break;
            }

            default:
                break;
        }

        cursor += command->cmdsize;
    }

    facts.ok = true;
    return facts;
}
} // namespace vibecheck

#else   // Only macOS has Mach-O binaries to read; Windows plugins go through PEReader.

namespace vibecheck
{
MachOFacts readMachO (const juce::File&, int, int, int)
{
    MachOFacts facts;
    facts.error = "Mach-O files are only read on macOS";
    return facts;
}
} // namespace vibecheck

#endif
