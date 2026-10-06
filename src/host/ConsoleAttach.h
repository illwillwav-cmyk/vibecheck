#pragma once

#include <JuceHeader.h>

namespace vibecheck
{
/** A Windows GUI program has no console, so anything a command-line switch prints would vanish.
    When VibeCheck is started from a terminal with such a switch, this borrows that terminal's
    console. When the app starts itself with its output piped (the Deep check does), the pipe is
    already there and is left alone. Does nothing on other platforms. */
void attachConsoleForCommandLine (const juce::String& commandLine);
} // namespace vibecheck
