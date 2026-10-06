#include "SourceInspector.h"

#include <algorithm>
#include <map>
#include <string>

namespace vibecheck
{
namespace
{
//==============================================================================
// Reading C++ without a compiler: one pass that separates code from comments and string literals.

struct Parsed
{
    /** The source with comments and literals blanked to spaces and anything non-ASCII replaced, one
        byte per character, so offsets and line numbers match the original. */
    std::string code;

    struct Comment { int line; juce::String text; };
    std::vector<Comment> comments;

    std::vector<size_t> lineStarts;

    int lineOf (size_t offset) const
    {
        const auto it = std::upper_bound (lineStarts.begin(), lineStarts.end(), offset);
        return (int) (it - lineStarts.begin());
    }

    std::string lineText (int line) const
    {
        if (line < 1 || line > (int) lineStarts.size())
            return {};

        const auto start = lineStarts[(size_t) line - 1];
        const auto end = line < (int) lineStarts.size() ? lineStarts[(size_t) line] : code.size();
        return code.substr (start, end - start);
    }
};

Parsed parse (const juce::String& text)
{
    Parsed out;
    out.code.reserve ((size_t) text.length());
    out.lineStarts.push_back (0);

    enum class State { code, lineComment, blockComment, string, character };
    auto state = State::code;
    int line = 1;
    juce::String comment;
    int commentLine = 1;

    const auto flush = [&]
    {
        // Documentation comments open with extra slashes, stars or a bang; none of it is text.
        const auto cleaned = comment.trimCharactersAtStart ("/*!< \t").trim();

        if (cleaned.isNotEmpty())
            out.comments.push_back ({ commentLine, cleaned });

        comment.clear();
    };

    auto p = text.getCharPointer();
    juce::juce_wchar previous = 0;

    while (! p.isEmpty())
    {
        const auto c = p.getAndAdvance();
        const auto next = *p;
        char emit = c < 128 ? (char) c : '?';

        switch (state)
        {
            case State::code:
                if (c == '/' && (next == '/' || next == '*'))
                {
                    // Step over the second character of the opener so it is not read as comment text.
                    state = next == '/' ? State::lineComment : State::blockComment;
                    commentLine = line;
                    p.getAndAdvance();
                    out.code += ' ';
                    emit = ' ';
                }
                else if (c == '"')                 { state = State::string; }
                else if (c == '\'' && ! juce::CharacterFunctions::isLetterOrDigit (previous)) { state = State::character; }
                break;

            case State::lineComment:
                if (c == '\n') { flush(); state = State::code; }
                else           { comment += c; emit = ' '; }
                break;

            case State::blockComment:
                if (c == '*' && next == '/')
                {
                    flush();
                    p.getAndAdvance();
                    out.code += ' ';
                    emit = ' ';
                    state = State::code;
                }
                else if (c == '\n') { flush(); commentLine = line + 1; }
                else                { comment += c; emit = ' '; }
                break;

            case State::string:
                if (c == '\\' && ! p.isEmpty()) { p.getAndAdvance(); out.code += ' '; emit = ' '; }
                else if (c == '"')              { state = State::code; }
                else if (c != '\n')             { emit = ' '; }
                break;

            case State::character:
                if (c == '\\' && ! p.isEmpty()) { p.getAndAdvance(); out.code += ' '; emit = ' '; }
                else if (c == '\'' || c == '\n') { state = State::code; }
                else                             { emit = ' '; }
                break;
        }

        out.code += emit;

        if (c == '\n')
        {
            ++line;
            out.lineStarts.push_back (out.code.size());
        }

        previous = c;
    }

    flush();
    return out;
}

bool isIdentifierChar (char c) { return std::isalnum ((unsigned char) c) != 0 || c == '_'; }

/** Bodies of every definition of a function with this name: the text between its braces. */
struct Body { size_t start = 0; std::string text; };

std::vector<Body> bodiesOf (const Parsed& parsed, const std::string& name)
{
    std::vector<Body> bodies;
    const auto& code = parsed.code;

    for (auto at = code.find (name); at != std::string::npos; at = code.find (name, at + name.size()))
    {
        if (at > 0 && isIdentifierChar (code[at - 1]))
            continue;

        auto i = at + name.size();

        if (i < code.size() && isIdentifierChar (code[i]))
            continue;

        while (i < code.size() && std::isspace ((unsigned char) code[i]))
            ++i;

        if (i >= code.size() || code[i] != '(')
            continue;

        // Past the parameter list.
        int depth = 0;

        for (; i < code.size(); ++i)
        {
            if (code[i] == '(') ++depth;
            else if (code[i] == ')' && --depth == 0) { ++i; break; }
        }

        // A definition reaches a brace before a semicolon; a declaration or a call does not.
        while (i < code.size() && code[i] != '{' && code[i] != ';' && code[i] != ')' && code[i] != ',')
            ++i;

        if (i >= code.size() || code[i] != '{')
            continue;

        const auto open = i + 1;
        depth = 1;

        for (i = open; i < code.size() && depth > 0; ++i)
        {
            if (code[i] == '{') ++depth;
            else if (code[i] == '}') --depth;
        }

        if (depth == 0)
            bodies.push_back ({ open, code.substr (open, i - 1 - open) });
    }

    return bodies;
}

std::string withoutSpaces (std::string text)
{
    text.erase (std::remove_if (text.begin(), text.end(), [] (char c) { return c == ' ' || c == '\t'; }), text.end());
    return text;
}

bool contains (const std::string& haystack, const char* needle) { return haystack.find (needle) != std::string::npos; }

/** "new Thing" or "new (", but not "renew" or a variable called newValue. */
bool hasNewExpression (const std::string& line)
{
    for (auto at = line.find ("new"); at != std::string::npos; at = line.find ("new", at + 3))
    {
        const auto before = at == 0 || ! isIdentifierChar (line[at - 1]);
        const auto after = at + 3 < line.size() && (line[at + 3] == ' ' || line[at + 3] == '(');

        if (before && after)
            return true;
    }

    return false;
}

//==============================================================================
bool isTypographic (juce::juce_wchar c)
{
    switch (c)
    {
        case 0x2014: case 0x2013:                           // em and en dash
        case 0x2192: case 0x2190: case 0x2194: case 0x21d2: // arrows
        case 0x00d7: case 0x2265: case 0x2264: case 0x2248: // multiply, >=, <=, approximately
        case 0x2026:                                        // ellipsis
        case 0x2018: case 0x2019: case 0x201c: case 0x201d: // curly quotes
        case 0x2022: case 0x2713: case 0x2714:              // bullet, check marks
            return true;
        default:
            return false;
    }
}

bool isEmoji (juce::juce_wchar c)
{
    return (c >= 0x1f300 && c <= 0x1faff) || c == 0x2705 || c == 0x274c || c == 0x26a0 || c == 0x2728 || c == 0x2b50;
}

const char* const skippedFolders[] = { "juce", "jucelibrarycode", "modules", "third_party", "thirdparty", "third-party", "deps", "dependencies",
                                       "lib", "libs", "external", "externals", "vendor", "build", "builds", ".git", "node_modules", "submodules",
                                       "vst3sdk", "vst3_sdk", "clap", "pluginval", "sdk", "sdks", "iplug2", "cmake-build-debug", "cmake-build-release" };

bool inSkippedFolder (const juce::String& relativePath)
{
    auto parts = juce::StringArray::fromTokens (relativePath.replaceCharacter ('\\', '/'), "/", "");
    parts.remove (parts.size() - 1);

    for (const auto& part : parts)
        for (const auto* skipped : skippedFolders)
            if (part.equalsIgnoreCase (skipped))
                return true;

    return false;
}

juce::String htmlUnescaped (juce::String text)
{
    return text.replace ("&lt;", "<").replace ("&gt;", ">").replace ("&quot;", "\"").replace ("&#39;", "'")
               .replace ("&#x27;", "'").replace ("&nbsp;", " ").replace ("&amp;", "&");
}

juce::String withoutTags (const juce::String& html)
{
    juce::String out;
    bool inTag = false;

    for (auto p = html.getCharPointer(); ! p.isEmpty();)
    {
        const auto c = p.getAndAdvance();

        if (c == '<') inTag = true;
        else if (c == '>') inTag = false;
        else if (! inTag) out += c;
    }

    return out;
}

/** True for a commit message that names an AI tool as a co-author or as its generator. */
bool hasAiTrailer (const juce::String& message)
{
    for (const auto& raw : juce::StringArray::fromLines (message))
    {
        const auto line = raw.trim().toLowerCase();

        if (line.startsWith ("co-authored-by:"))
            for (const auto* tool : { "claude", "anthropic", "copilot", "cursor", "codex", "openai", "chatgpt", "gemini", "devin", "aider", "windsurf", "jules" })
                if (line.contains (tool))
                    return true;

        if (line.contains ("generated with [claude code]") || line.contains ("generated with claude code")
            || line.contains ("generated by copilot") || line.contains ("generated with cursor"))
            return true;
    }

    return false;
}

juce::var jsonObject (std::initializer_list<std::pair<const char*, juce::var>> members)
{
    auto* object = new juce::DynamicObject();

    for (const auto& [name, value] : members)
        object->setProperty (name, value);

    return juce::var (object);
}
} // namespace

//==============================================================================
double SourceReport::points() const
{
    double total = 0.0;

    for (const auto& finding : findings)
        total += finding.points;

    return total;
}

juce::String SourceReport::toText() const
{
    juce::String text;

    if (! ok)
        return "source could not be read: " + error + "\n";

    text << "source: " << origin << "  (" << files << " files, " << lines << " lines";

    if (commitsRead > 0)
        text << ", " << commitsRead << " recent commits";

    text << ")\n";

    if (findings.empty())
        text << "nothing found in the source.\n";

    for (const auto& finding : findings)
        text << "  [source] " << finding.finding << "  (+" << juce::String (finding.points, 0) << ")\n"
             << "      " << finding.detail.replace ("\n", "\n      ") << "\n";

    text << "total from source: +" << juce::String (points(), 0) << "\n";
    return text;
}

juce::String SourceReport::toJson() const
{
    juce::Array<juce::var> list;

    for (const auto& finding : findings)
        list.add (jsonObject ({ { "finding", finding.finding }, { "detail", finding.detail },
                                { "explanation", finding.explanation }, { "points", finding.points } }));

    return juce::JSON::toString (jsonObject ({ { "version", sourceVersion }, { "ok", ok }, { "origin", origin }, { "files", files },
                                               { "lines", lines }, { "commits", commitsRead }, { "findings", list } }), true);
}

SourceReport SourceReport::fromJson (const juce::String& json)
{
    SourceReport report;
    const auto parsed = juce::JSON::parse (json);

    if (! parsed.isObject() || (int) parsed["version"] != sourceVersion)
    {
        report.error = "unrecognised result";
        return report;
    }

    report.ok = (bool) parsed["ok"];
    report.origin = parsed["origin"].toString();
    report.files = (int) parsed["files"];
    report.lines = (int) parsed["lines"];
    report.commitsRead = (int) parsed["commits"];

    if (const auto* list = parsed["findings"].getArray())
        for (const auto& item : *list)
            report.findings.push_back ({ item["finding"].toString(), item["detail"].toString(),
                                         item["explanation"].toString(), (double) item["points"] });

    return report;
}

//==============================================================================
RepositoryRef parseRepository (const juce::String& address)
{
    RepositoryRef ref;
    const auto at = address.indexOfIgnoreCase ("github.com/");

    if (at < 0)
        return ref;

    // The address ends at the first character that cannot be part of one: a space, a quote, a bracket.
    const auto tail = address.substring (at + 11);
    int end = 0;

    while (end < tail.length() && (juce::CharacterFunctions::isLetterOrDigit (tail[end]) || juce::String ("-_./").containsChar (tail[end])))
        ++end;

    const auto parts = juce::StringArray::fromTokens (tail.substring (0, end), "/", "");

    if (parts.size() < 2)
        return ref;

    const auto clean = [] (juce::String part)
    {
        if (part.endsWithIgnoreCase (".git"))
            part = part.dropLastCharacters (4);

        return part.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.");
    };

    ref.owner = clean (parts[0]);
    ref.name = clean (parts[1]);

    if (ref.owner.containsOnly (".") || ref.name.containsOnly ("."))
        ref = {};

    return ref;
}

RepositoryRef findRepository (const juce::StringArray& binaryStrings)
{
    // Addresses that are in a binary because of what it was built with, not who built it.
    static const juce::StringArray frameworkOwners { "juce-framework", "weareroli", "steinbergmedia", "steinberg", "free-audio", "iplug2",
                                                      "microsoft", "google", "apple", "nlohmann", "catchorg", "boostorg", "llvm", "madler",
                                                      "xiph", "sudara", "ff-meters", "facebook", "protocolbuffers", "abseil", "fmtlib",
                                                      "gabime", "kfrlib", "sponsors", "orgs", "features", "about" };
    std::map<juce::String, int> seen;
    std::map<juce::String, RepositoryRef> refs;

    for (const auto& line : binaryStrings)
    {
        if (! line.containsIgnoreCase ("github.com/"))
            continue;

        const auto ref = parseRepository (line);

        if (! ref.isValid() || frameworkOwners.contains (ref.owner.toLowerCase()))
            continue;

        const auto key = (ref.owner + "/" + ref.name).toLowerCase();
        ++seen[key];
        refs[key] = ref;
    }

    // The plugin's own address is the one it mentions most.
    RepositoryRef best;
    int most = 0;

    for (const auto& [key, count] : seen)
        if (count > most)
        {
            most = count;
            best = refs[key];
        }

    return best;
}

//==============================================================================
void SourceScan::example (std::vector<Example>& list, const juce::String& path, int line, const juce::String& text)
{
    if (list.size() < 3)
        list.push_back ({ path + ":" + juce::String (line), text.trim().substring (0, 110) });
}

void SourceScan::noteFile (const juce::String& relativePath)
{
    const auto path = relativePath.replaceCharacter ('\\', '/');
    const auto name = path.fromLastOccurrenceOf ("/", false, false).toLowerCase();

    // Files and folders that exist only to instruct an AI coding tool.
    for (const auto* marker : { "claude.md", "agents.md", "gemini.md", ".cursorrules", ".windsurfrules", "copilot-instructions.md",
                                ".clinerules", ".aider.conf.yml", ".roorules" })
        if (name == marker && ! aiToolFiles.contains (path))
            aiToolFiles.add (path);

    for (const auto* folder : { ".claude/", ".cursor/", ".codex/", ".windsurf/", ".aider/", ".cline/", ".roo/" })
        if ((path.startsWithIgnoreCase (folder) || path.containsIgnoreCase (juce::String ("/") + folder)))
        {
            const auto shown = path.upToFirstOccurrenceOf (folder, true, true);

            if (! aiToolFiles.contains (shown))
                aiToolFiles.add (shown);
        }
}

bool SourceScan::wantsContents (const juce::String& relativePath, juce::int64 sizeInBytes)
{
    // Generated resource files are megabytes of numbers and say nothing about the author.
    if (sizeInBytes > 1500 * 1024 || sizeInBytes <= 0)
        return false;

    const auto name = relativePath.replaceCharacter ('\\', '/').fromLastOccurrenceOf ("/", false, false);

    if (name.startsWithIgnoreCase ("BinaryData"))
        return false;

    bool isSource = false;

    for (const auto* extension : { ".cpp", ".h", ".hpp", ".cc", ".cxx", ".mm", ".c" })
        isSource = isSource || name.endsWithIgnoreCase (extension);

    return isSource && ! inSkippedFolder (relativePath);
}

void SourceScan::addCommitMessages (const juce::StringArray& messages)
{
    commitMessages.addArray (messages);
}

void SourceScan::addSource (const juce::String& relativePath, const juce::String& text)
{
    const auto path = relativePath.replaceCharacter ('\\', '/');
    const auto parsed = parse (text);

    // Examples quote the line as it was written; the parsed copy has its strings blanked out.
    const auto original = juce::StringArray::fromLines (text);
    const auto written = [&original] (int line) { return original[line - 1]; };

    ++files;
    lines += (int) parsed.lineStarts.size();

    for (const auto& comment : parsed.comments)
    {
        bool sawTypographic = false, sawEmoji = false;

        for (auto p = comment.text.getCharPointer(); ! p.isEmpty();)
        {
            const auto c = p.getAndAdvance();
            sawTypographic = sawTypographic || isTypographic (c);
            sawEmoji = sawEmoji || isEmoji (c);
        }

        if (sawTypographic) { ++typographic; example (typographicExamples, path, comment.line, comment.text); }
        if (sawEmoji)       { ++emoji;       example (emojiExamples, path, comment.line, comment.text); }

        const auto lower = comment.text.toLowerCase();

        if (lower.startsWith ("step ") && juce::CharacterFunctions::isDigit (lower[5]))
        {
            ++stepComments;
            example (stepExamples, path, comment.line, comment.text);
        }

        for (const auto* phrase : { "in a real implementation", "in a real plugin", "in a production", "for production use", "simplified version",
                                    "simplified implementation", "basic implementation", "placeholder implementation", "this is a placeholder",
                                    "your code here", "would go here", "implementation goes here", "left as an exercise" })
            if (lower.contains (phrase))
            {
                ++hedges;
                example (hedgeExamples, path, comment.line, comment.text);
                break;
            }
    }

    // What the audio callback does that real-time code should not.
    {
        for (const auto* name : { "processBlock", "ProcessBlock" })
            for (const auto& body : bodiesOf (parsed, name))
            {
                const auto firstLine = parsed.lineOf (body.start);
                const auto lastLine = parsed.lineOf (body.start + body.text.size());

                for (int line = firstLine; line <= lastLine; ++line)
                {
                    const auto raw = parsed.lineText (line);
                    const auto compact = withoutSpaces (raw);

                    if (compact.empty())
                        continue;

                    const auto note = [&] (const char* kind)
                    {
                        auto found = std::find_if (violations.begin(), violations.end(), [kind] (const Violation& v) { return v.kind == kind; });

                        if (found == violations.end())
                        {
                            violations.push_back ({ kind, 0, {} });
                            found = violations.end() - 1;
                        }

                        ++found->count;
                        example (found->examples, path, line, written (line));
                    };

                    if (hasNewExpression (raw) || contains (compact, "malloc(") || contains (compact, "calloc(")
                        || contains (compact, "make_unique") || contains (compact, "make_shared"))
                        note ("asks for memory");

                    if (contains (compact, ".push_back(") || contains (compact, ".emplace_back(") || contains (compact, ".resize("))
                        note ("grows a container");

                    if (contains (compact, "std::cout") || contains (compact, "printf(") || contains (compact, "writeToLog")
                        || compact.rfind ("std::string", 0) == 0 || contains (compact, "std::to_string("))
                        note ("builds or prints text");

                    const auto tries = contains (compact, "try") || contains (compact, "Try") || contains (compact, "SpinLock");

                    if (! tries && (contains (compact, "lock_guard") || contains (compact, "scoped_lock") || contains (compact, "unique_lock")
                                    || contains (compact, "ScopedLock") || contains (compact, ".lock()")))
                        note ("waits for a lock");

                    if (contains (compact, "fopen(") || contains (compact, "ifstream") || contains (compact, "ofstream")
                        || contains (compact, "FileInputStream") || contains (compact, "FileOutputStream"))
                        note ("touches a file");

                    if (contains (compact, "getRawParameterValue("))
                    {
                        ++rawParameterLookups;
                        example (lookupExamples, path, line, written (line));
                    }
                }
            }
    }

    for (const auto* name : { "getStateInformation", "setStateInformation" })
        for (const auto& body : bodiesOf (parsed, name))
        {
            auto compact = withoutSpaces (body.text);
            compact.erase (std::remove (compact.begin(), compact.end(), '\n'), compact.end());
            compact.erase (std::remove (compact.begin(), compact.end(), '\r'), compact.end());

            // An empty body, or one that only marks its arguments as unused.
            const auto onlyIgnores = compact.rfind ("juce::ignoreUnused(", 0) == 0 || compact.rfind ("ignoreUnused(", 0) == 0;
            const auto statements = std::count (compact.begin(), compact.end(), ';');

            if (compact.empty() || (onlyIgnores && statements <= 1))
            {
                (juce::String (name).startsWith ("get") ? emptySave : emptyRestore)++;
                example (stateExamples, path, parsed.lineOf (body.start), juce::String (name) + " has an empty body");
            }
        }
}

SourceReport SourceScan::finish (const juce::String& origin)
{
    SourceReport report;
    report.origin = origin;
    report.files = files;
    report.lines = lines;
    report.commitsRead = commitMessages.size();

    if (files == 0 && commitMessages.isEmpty() && aiToolFiles.isEmpty())
    {
        report.error = "no C or C++ source files were found";
        return report;
    }

    report.ok = true;

    const auto listed = [] (const std::vector<Example>& examples)
    {
        juce::StringArray lines;

        for (const auto& item : examples)
            lines.add (item.where + "  " + item.text);

        return lines.joinIntoString ("\n");
    };

    // --- The repository itself -------------------------------------------------------------------
    if (! commitMessages.isEmpty())
    {
        int withTrailer = 0;
        juce::String sample;

        for (const auto& message : commitMessages)
            if (hasAiTrailer (message))
            {
                ++withTrailer;

                if (sample.isEmpty())
                    for (const auto& line : juce::StringArray::fromLines (message))
                        if (line.trim().startsWithIgnoreCase ("co-authored-by:") || line.containsIgnoreCase ("generated with"))
                            sample = line.trim().substring (0, 90);
            }

        if (withTrailer > 0)
        {
            const auto share = (double) withTrailer / (double) commitMessages.size();
            const auto points = share >= 0.5 ? 40.0 : share >= 0.1 ? 28.0 : 20.0;

            report.findings.push_back ({ "AI co-author lines in the commit history",
                                         juce::String (withTrailer) + " of the last " + juce::String (commitMessages.size()) + " commits\n" + sample,
                                         "The commits themselves name an AI tool as co-author. That is direct evidence the tool wrote part of "
                                         "the code. It says nothing about quality: careful developers who use an assistant leave the same line.",
                                         points });
        }
    }

    if (! aiToolFiles.isEmpty())
        report.findings.push_back ({ "the repository is set up for an AI coding tool", aiToolFiles.joinIntoString ("\n").substring (0, 300),
                                     "Files such as CLAUDE.md, AGENTS.md or .cursorrules exist only to instruct an AI coding tool, so the "
                                     "project is being developed with one.",
                                     20.0 });

    // --- What the code does --------------------------------------------------------------------
    if (emptySave > 0 || emptyRestore > 0)
        report.findings.push_back ({ emptySave > 0 && emptyRestore > 0 ? "saving and restoring settings are both empty functions"
                                                                       : "saving or restoring settings is an empty function",
                                     listed (stateExamples),
                                     "The plugin cannot bring its settings back when a project reopens. Generated projects very often leave "
                                     "these two functions as the empty stubs the template gave them.",
                                     emptySave > 0 && emptyRestore > 0 ? 12.0 : 6.0 });

    {
        double total = 0.0;

        for (const auto& violation : violations)
        {
            if (total >= 18.0)
                break;

            total += 6.0;
            report.findings.push_back ({ "the audio callback " + violation.kind, juce::String (violation.count) + " place"
                                                                                     + (violation.count == 1 ? "" : "s") + " in processBlock\n" + listed (violation.examples),
                                         "Code on the audio thread must never wait: asking for memory, taking a lock, printing or touching a "
                                         "file can each stall it and cause a dropout. Experienced plugin developers avoid all of them there.",
                                         6.0 });
        }
    }

    if (rawParameterLookups >= 3)
        report.findings.push_back ({ "looks parameters up by name inside the audio callback", juce::String (rawParameterLookups) + " lookups in processBlock\n" + listed (lookupExamples),
                                     "Each lookup searches for the parameter by its text name on every buffer. Tutorials do this for brevity and "
                                     "generated code copies it; finished plugins keep the pointer.",
                                     4.0 });

    // --- How it is written. Only what was measured to differ; comment style is not scored. ---------
    const auto perThousand = lines > 0 ? 1000.0 * (double) typographic / (double) lines : 0.0;

    if (typographic >= 5 && perThousand >= 0.3)
        report.findings.push_back ({ "typographic characters in code comments", juce::String (typographic) + " comments\n" + listed (typographicExamples),
                                     "Em dashes, arrows and curly quotes are awkward to type in a code editor and rare in hand-written "
                                     "comments. Language models produce them freely.",
                                     6.0 });

    if (emoji >= 3)
        report.findings.push_back ({ "emoji in code comments", juce::String (emoji) + " comments\n" + listed (emojiExamples),
                                     "Emoji in source comments are a habit of chat assistants far more than of people writing audio code.", 6.0 });

    if (stepComments >= 3)
        report.findings.push_back ({ "numbered step comments", juce::String (stepComments) + " comments\n" + listed (stepExamples),
                                     "\"Step 1\", \"Step 2\" narration through a function is how assistants explain code to the person who "
                                     "asked for it. Weak: tutorials do it too.",
                                     4.0 });

    if (hedges >= 2)
        report.findings.push_back ({ "comments that apologise for the code", juce::String (hedges) + " comments\n" + listed (hedgeExamples),
                                     "Phrases such as \"in a real implementation\" describe code its writer does not consider finished. Weak.", 4.0 });

    return report;
}

//==============================================================================
namespace
{
void walk (const juce::File& folder, const juce::File& root, SourceScan& scan, int depth)
{
    if (depth > 12)
        return;

    for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findFilesAndDirectories))
    {
        const auto file = entry.getFile();
        const auto relative = file.getRelativePathFrom (root).replaceCharacter ('\\', '/');

        if (file.isDirectory())
        {
            // Note AI-tool folders, then leave framework and third-party trees unread.
            scan.noteFile (relative + "/");

            // wantsContents refuses anything inside a skipped folder, so asking about an imaginary
            // file in this one says whether to go in.
            if (SourceScan::wantsContents (relative + "/x.cpp", 1))
                walk (file, root, scan, depth + 1);

            continue;
        }

        scan.noteFile (relative);

        if (SourceScan::wantsContents (relative, file.getSize()))
            scan.addSource (relative, file.loadFileAsString());
    }
}

juce::String fetchText (const juce::String& address, int timeoutMs)
{
    const auto stream = juce::URL (address).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                                   .withConnectionTimeoutMs (timeoutMs)
                                                                   .withNumRedirectsToFollow (5));
    return stream != nullptr ? stream->readEntireStreamAsString() : juce::String();
}

/** Commit messages out of GitHub's public feed, which needs no API key and has no hourly limit. */
juce::StringArray commitMessagesFromFeed (const juce::String& feed)
{
    juce::StringArray messages;

    for (int at = feed.indexOf ("<entry>"); at >= 0; at = feed.indexOf (at + 7, "<entry>"))
    {
        const auto end = feed.indexOf (at, "</entry>");

        if (end < 0)
            break;

        const auto entry = feed.substring (at, end);
        const auto open = entry.indexOf ("<content");
        const auto close = entry.indexOf ("</content>");

        if (open < 0 || close < open)
            continue;

        const auto inner = entry.substring (entry.indexOf (open, ">") + 1, close);
        messages.add (htmlUnescaped (withoutTags (htmlUnescaped (inner))).trim());
    }

    return messages;
}
} // namespace

SourceReport inspectSourceFolder (const juce::File& root)
{
    SourceReport failed;

    if (! root.isDirectory())
    {
        failed.error = "that is not a folder";
        return failed;
    }

    SourceScan scan;
    walk (root, root, scan, 0);
    return scan.finish ("a local folder");
}

SourceReport inspectRepository (const RepositoryRef& repository, const std::atomic<bool>& cancel,
                                const std::function<void (const juce::String&)>& progress)
{
    SourceReport failed;

    if (! repository.isValid())
    {
        failed.error = "no repository address";
        return failed;
    }

    const auto say = [&progress] (const juce::String& text) { if (progress) progress (text); };
    const auto base = "https://github.com/" + repository.owner + "/" + repository.name;

    SourceScan scan;

    say ("Reading the commit history of " + repository.display());
    scan.addCommitMessages (commitMessagesFromFeed (fetchText (base + "/commits.atom", 15000)));

    if (cancel.load())
    {
        failed.error = "stopped";
        return failed;
    }

    // The archive goes to one temporary file; its source files are then read straight out of it.
    const auto archive = juce::File::createTempFile (".zip");
    constexpr juce::int64 limit = 600LL * 1024 * 1024;

    {
        const auto stream = juce::URL (base + "/archive/HEAD.zip").createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                                                         .withConnectionTimeoutMs (20000)
                                                                                         .withNumRedirectsToFollow (5));
        juce::FileOutputStream out (archive);

        if (stream == nullptr || ! out.openedOk())
        {
            archive.deleteFile();
            failed.error = "the repository could not be downloaded. It may be private, renamed or gone.";
            return failed;
        }

        juce::HeapBlock<char> buffer (1 << 16);
        juce::int64 total = 0;
        auto lastSaid = juce::Time::getMillisecondCounter();

        for (;;)
        {
            const auto read = stream->read (buffer, 1 << 16);

            if (read <= 0)
                break;

            out.write (buffer, (size_t) read);
            total += read;

            if (cancel.load() || total > limit)
            {
                out.flush();
                archive.deleteFile();
                failed.error = cancel.load() ? "stopped" : "the repository is larger than 600 MB, which is more than this will download";
                return failed;
            }

            if (juce::Time::getMillisecondCounter() - lastSaid > 400)
            {
                say ("Downloading " + repository.display() + ": " + juce::String ((double) total / (1024.0 * 1024.0), 1) + " MB");
                lastSaid = juce::Time::getMillisecondCounter();
            }
        }
    }

    say ("Reading the source of " + repository.display());

    {
        juce::ZipFile zip (archive);

        if (zip.getNumEntries() == 0)
        {
            archive.deleteFile();
            failed.error = "the repository could not be downloaded. It may be private, renamed or gone.";
            return failed;
        }

        for (int i = 0; i < zip.getNumEntries() && ! cancel.load(); ++i)
        {
            const auto* entry = zip.getEntry (i);

            // GitHub wraps everything in one folder named after the repository and commit.
            const auto relative = entry->filename.fromFirstOccurrenceOf ("/", false, false);

            if (relative.isEmpty())
                continue;

            scan.noteFile (relative);

            if (! relative.endsWith ("/") && SourceScan::wantsContents (relative, entry->uncompressedSize))
                if (const std::unique_ptr<juce::InputStream> stream { zip.createStreamForEntry (i) })
                    scan.addSource (relative, stream->readEntireStreamAsString());
        }
    }

    archive.deleteFile();

    if (cancel.load())
    {
        failed.error = "stopped";
        return failed;
    }

    return scan.finish (repository.display());
}
} // namespace vibecheck
