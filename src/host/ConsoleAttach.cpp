#include "ConsoleAttach.h"

// windows.h defines macros (small, near, far, min, max) that break unrelated code, so it is
// included here and nowhere else.
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <cstdio>
 #include <iostream>
#endif

namespace vibecheck
{
void attachConsoleForCommandLine (const juce::String& commandLine)
{
   #if JUCE_WINDOWS
    static const char* const printing[] = { "--selftest", "--scan", "--vibecheck=", "--health=", "--analyze=", "--behaviour=",
                                            "--export=", "--merge=", "--help" };
    bool prints = false;

    for (const auto* name : printing)
        prints = prints || commandLine.contains (name);

    if (! prints)
        return;

    const auto out = GetStdHandle (STD_OUTPUT_HANDLE);

    if (out != nullptr && out != INVALID_HANDLE_VALUE)
        return;   // already redirected somewhere, which is where the output should go

    if (AttachConsole (ATTACH_PARENT_PROCESS))
    {
        FILE* stream = nullptr;
        freopen_s (&stream, "CONOUT$", "w", stdout);
        freopen_s (&stream, "CONOUT$", "w", stderr);
        std::ios::sync_with_stdio();
    }
   #else
    juce::ignoreUnused (commandLine);
   #endif
}
} // namespace vibecheck
