#pragma once

#include <JuceHeader.h>

#include <atomic>
#include <functional>
#include <vector>

namespace vibecheck
{
/** One thing found in a plugin's source code or its repository. */
struct SourceFinding
{
    juce::String finding;       ///< What it is, in words.
    juce::String detail;        ///< Where, and the literal text: "Source/Proc.cpp:120  ...".
    juce::String explanation;   ///< Why it counts.
    double points = 0.0;
};

/** What reading a plugin's source turned up.

    A compiled binary hides most of how it was written. Where the source is public, far more is
    visible: whether the repository is set up for an AI coding tool, whether commits carry an AI
    co-author line, whether the save and restore functions are empty, and what the audio callback
    does that real-time code should not.

    Two things this is not. It is evidence of AI *assistance*, not of poor work: a careful developer
    who uses an assistant leaves the same co-author lines. And comment style is deliberately not
    scored: measured against a hand-written plugin and a generated one of the same size, "obvious",
    "narrator" and "hedging" comments turned up at the same rate in both. */
struct SourceReport
{
    bool ok = false;
    juce::String error;

    juce::String origin;        ///< "github.com/owner/repo", or "a local folder". Never a local path.
    int files = 0;
    int lines = 0;
    int commitsRead = 0;

    std::vector<SourceFinding> findings;

    double points() const;
    juce::String toText() const;

    /** For the cache and the export. */
    juce::String toJson() const;
    static SourceReport fromJson (const juce::String& json);
};

/** Bump when the rules change, so cached results are taken again. */
constexpr int sourceVersion = 1;

/** A public repository named inside a plugin. */
struct RepositoryRef
{
    juce::String owner, name;
    bool isValid() const { return owner.isNotEmpty() && name.isNotEmpty(); }
    juce::String display() const { return "github.com/" + owner + "/" + name; }
};

/** Looks through a binary's strings for the address of its own GitHub repository, ignoring the
    frameworks' addresses that every plugin carries. */
RepositoryRef findRepository (const juce::StringArray& binaryStrings);

/** Parses "https://github.com/owner/repo", "github.com/owner/repo.git" and the like. */
RepositoryRef parseRepository (const juce::String& address);

/** Accumulates files one at a time, so a folder and a downloaded archive are read the same way and
    nothing has to be unpacked to disk. */
class SourceScan
{
public:
    /** Every path in the tree, source or not: AI-tool configuration is recognised by file name. */
    void noteFile (const juce::String& relativePath);

    /** True for a C or C++ source file outside the framework and third-party folders. */
    static bool wantsContents (const juce::String& relativePath, juce::int64 sizeInBytes);

    void addSource (const juce::String& relativePath, const juce::String& text);

    /** Commit messages, newest first. */
    void addCommitMessages (const juce::StringArray& messages);

    SourceReport finish (const juce::String& origin);

private:
    struct Example { juce::String where, text; };

    void example (std::vector<Example>& list, const juce::String& path, int line, const juce::String& text);

    int files = 0, lines = 0;
    juce::StringArray aiToolFiles;
    juce::StringArray commitMessages;

    int typographic = 0, emoji = 0, stepComments = 0, hedges = 0;
    std::vector<Example> typographicExamples, emojiExamples, stepExamples, hedgeExamples;

    int emptySave = 0, emptyRestore = 0;
    std::vector<Example> stateExamples;

    int rawParameterLookups = 0;
    std::vector<Example> lookupExamples;

    struct Violation { juce::String kind; int count = 0; std::vector<Example> examples; };
    std::vector<Violation> violations;
};

/** Reads a folder of source code. */
SourceReport inspectSourceFolder (const juce::File& root);

/** Downloads a public repository's current source and recent commit messages and reads them.
    Nothing is written to disk but one temporary archive, removed afterwards. Blocks, so call it
    from a worker thread; `progress` is told how much has arrived. */
SourceReport inspectRepository (const RepositoryRef& repository,
                                const std::atomic<bool>& cancel,
                                const std::function<void (const juce::String&)>& progress = {});
} // namespace vibecheck
