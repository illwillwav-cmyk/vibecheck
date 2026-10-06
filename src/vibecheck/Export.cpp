#include "Export.h"

#include <algorithm>
#include <map>

namespace vibecheck
{
namespace
{
constexpr const char* exportFormat = "vibecheck-export";
constexpr const char* masterFormat = "vibecheck-master";

juce::var object (std::initializer_list<std::pair<const char*, juce::var>> members)
{
    auto* result = new juce::DynamicObject();

    for (const auto& [name, value] : members)
        result->setProperty (name, value);

    return juce::var (result);
}

juce::var behaviourVar (const juce::String& line)
{
    if (line.isEmpty())
        return {};

    const auto report = BehaviourReport::fromMachine (line);

    if (! report.ok)
        return {};

    return object ({ { "allocationsMeasured", report.allocationsMeasured },
                     { "allocationsPerBlock", report.allocationsPerBlock },
                     { "fractionOfBlocksAllocating", report.fractionOfBlocksAllocating },
                     { "worstAllocationsInOneBlock", report.worstAllocationsInOneBlock },
                     { "allocationsPerBlockAutomated", report.allocationsPerBlockAutomated },
                     { "cpuPercent", report.cpuPercent },
                     { "spikeRatio", report.spikeRatio },
                     { "smallBufferOverhead", report.smallBufferOverhead },
                     { "denormalSlowdown", report.denormalSlowdown },
                     { "instrument", report.instrument } });
}

/** What makes two rows the same plugin. The binary fingerprint is left out when it is unknown,
    so a row from an older export can still meet its newer twin by name. */
juce::String identityKey (const juce::var& plugin)
{
    return (plugin["format"].toString() + "|" + plugin["name"].toString() + "|" + plugin["manufacturer"].toString()
            + "|" + plugin["version"].toString() + "|" + plugin["binaryId"].toString()).toLowerCase();
}

double medianOf (std::vector<double> values)
{
    if (values.empty())
        return 0.0;

    std::sort (values.begin(), values.end());
    const auto n = values.size();
    return n % 2 == 1 ? values[n / 2] : 0.5 * (values[n / 2 - 1] + values[n / 2]);
}

juce::String csvCell (const juce::String& text)
{
    return "\"" + text.replace ("\"", "\"\"") + "\"";
}

/** What a Source check found, without the quoted lines: for a local folder those are someone's
    private code, and for any plugin the finding names are what a master list needs. */
juce::var sourceVar (const juce::String& json)
{
    if (json.isEmpty())
        return {};

    const auto report = SourceReport::fromJson (json);

    if (! report.ok)
        return {};

    juce::Array<juce::var> findings;

    for (const auto& finding : report.findings)
        findings.add (object ({ { "finding", finding.finding }, { "points", finding.points } }));

    return object ({ { "origin", report.origin }, { "files", report.files }, { "lines", report.lines },
                     { "commits", report.commitsRead }, { "findings", findings } });
}
} // namespace

juce::var buildExport (const std::vector<SweepEntry>& entries, const juce::String& appVersion,
                       const std::map<juce::String, Label>& labels)
{
    juce::Array<juce::var> plugins;

    for (const auto& entry : entries)
    {
        const auto& d = entry.description;

        // What the person exporting knows about this plugin, if they have said.
        const auto known = labels.find (labelKey (d));
        const auto label = known != labels.end() ? toString (known->second) : juce::String();

        juce::Array<juce::var> findings;

        for (const auto& finding : entry.findings)
            findings.add (object ({ { "family", finding.family }, { "finding", finding.finding }, { "points", finding.points } }));

        plugins.add (object ({ { "name", d.name },
                               { "manufacturer", d.manufacturerName },
                               { "format", d.pluginFormatName },
                               { "version", d.version },
                               { "category", d.category },
                               { "instrument", d.isInstrument },
                               { "binaryId", entry.binaryId },
                               { "score", entry.score },
                               { "confidence", entry.confidence },
                               { "conclusive", entry.conclusive },
                               { "headline", entry.headline },
                               { "deep", entry.deep },
                               { "findings", findings },
                               { "behaviour", behaviourVar (entry.behaviourLine) },
                               { "source", sourceVar (entry.sourceJson) },
                               { "label", label } }));
    }

    return object ({ { "format", exportFormat },
                     { "schema", exportSchema },
                     { "app", appVersion },
                     { "heuristics", heuristicsVersion },
                     { "behaviourVersion", behaviourVersion },
                     { "exportedAt", juce::Time::getCurrentTime().toISO8601 (true) },
                     { "system", juce::SystemStats::getOperatingSystemName() },
                     { "plugins", plugins } });
}

juce::Result writeExport (const juce::File& destination, const std::vector<SweepEntry>& entries, const juce::String& appVersion,
                          const std::map<juce::String, Label>& labels)
{
    const auto text = juce::JSON::toString (buildExport (entries, appVersion, labels), false);

    if (! destination.replaceWithText (text))
        return juce::Result::fail ("could not write " + destination.getFullPathName());

    return juce::Result::ok();
}

juce::Array<juce::File> exportFilesFrom (const juce::String& argument)
{
    juce::Array<juce::File> files;

    for (auto part : juce::StringArray::fromTokens (argument, ",", "\""))
    {
        const juce::File file (part.trim());

        if (file.isDirectory())
            files.addArray (file.findChildFiles (juce::File::findFiles, false, "*.json"));
        else if (file.existsAsFile())
            files.add (file);
    }

    return files;
}

MergeResult mergeExports (const juce::Array<juce::File>& files)
{
    struct Row
    {
        juce::var plugin;
        int heuristics = 0;
        int file = 0;
    };

    MergeResult result;
    std::map<juce::String, std::vector<Row>> grouped;

    for (int f = 0; f < files.size(); ++f)
    {
        const auto parsed = juce::JSON::parse (files[f]);

        if (parsed["format"].toString() != exportFormat)
        {
            result.problems.add (files[f].getFileName() + ": not a VibeCheck export, skipped");
            continue;
        }

        if ((int) parsed["schema"] > exportSchema)
        {
            result.problems.add (files[f].getFileName() + ": made by a newer VibeCheck (layout " + parsed["schema"].toString()
                                 + "), update this one to read it");
            continue;
        }

        ++result.filesRead;
        const auto heuristics = (int) parsed["heuristics"];

        if (const auto* list = parsed["plugins"].getArray())
            for (const auto& plugin : *list)
            {
                ++result.pluginsSeen;
                grouped[identityKey (plugin)].push_back ({ plugin, heuristics, f });
            }
    }

    juce::Array<juce::var> plugins;

    for (auto& [key, rows] : grouped)
    {
        // Scores from an older rule set are not mixed with newer ones: only the rows made by the
        // newest rules take part, and the rest are kept as a count so nothing silently vanishes.
        int newest = 0;

        for (const auto& row : rows)
            newest = juce::jmax (newest, row.heuristics);

        std::vector<const Row*> current;
        std::vector<double> scores;
        std::vector<int> contributors;
        const Row* deepRow = nullptr;

        for (const auto& row : rows)
        {
            if (std::find (contributors.begin(), contributors.end(), row.file) == contributors.end())
                contributors.push_back (row.file);

            if (row.heuristics != newest)
                continue;

            current.push_back (&row);

            if ((bool) row.plugin["conclusive"])
                scores.push_back ((double) row.plugin["score"]);

            if ((bool) row.plugin["deep"] && deepRow == nullptr)
                deepRow = &row;
        }

        // Prefer the deep-checked row as the representative, since it carries the most evidence.
        const auto& first = deepRow != nullptr ? *deepRow : *current.front();
        const auto& base = first.plugin;

        auto* merged = new juce::DynamicObject();

        for (const auto* name : { "name", "manufacturer", "format", "version", "category", "instrument", "binaryId",
                                  "confidence", "conclusive", "headline", "deep", "findings", "behaviour", "source" })
            merged->setProperty (name, base[name]);

        merged->setProperty ("score", scores.empty() ? (double) base["score"] : medianOf (scores));
        merged->setProperty ("scoreMin", scores.empty() ? (double) base["score"] : *std::min_element (scores.begin(), scores.end()));
        merged->setProperty ("scoreMax", scores.empty() ? (double) base["score"] : *std::max_element (scores.begin(), scores.end()));
        // What the people who reported it say they know. One vote per file, whatever the rule set.
        std::vector<int> aiVoters, humanVoters;

        for (const auto& row : rows)
        {
            const auto label = labelFromString (row.plugin["label"].toString());
            auto& voters = label == Label::vibeCoded ? aiVoters : humanVoters;

            if (label != Label::none && std::find (voters.begin(), voters.end(), row.file) == voters.end())
                voters.push_back (row.file);
        }

        merged->setProperty ("labelAi", (int) aiVoters.size());
        merged->setProperty ("labelHuman", (int) humanVoters.size());
        merged->setProperty ("label", aiVoters.size() > humanVoters.size() ? "ai" : humanVoters.size() > aiVoters.size() ? "human" : "");
        merged->setProperty ("reports", (int) contributors.size());
        merged->setProperty ("heuristics", newest);
        merged->setProperty ("olderReports", (int) (rows.size() - current.size()));
        plugins.add (juce::var (merged));
    }

    std::sort (plugins.begin(), plugins.end(), [] (const juce::var& a, const juce::var& b)
               { return (double) a["score"] > (double) b["score"]; });

    result.pluginsMerged = plugins.size();
    result.master = object ({ { "format", masterFormat },
                              { "schema", exportSchema },
                              { "builtAt", juce::Time::getCurrentTime().toISO8601 (true) },
                              { "sources", result.filesRead },
                              { "plugins", plugins } });
    return result;
}

juce::String masterToCsv (const juce::var& master)
{
    juce::String csv = "name,manufacturer,format,version,score,score_min,score_max,reports,verdict,known_as,says_vibe_coded,says_hand_written,deep,cpu_percent,allocations_per_block,findings\n";

    if (const auto* list = master["plugins"].getArray())
        for (const auto& plugin : *list)
        {
            juce::StringArray findings;

            if (const auto* items = plugin["findings"].getArray())
                for (const auto& item : *items)
                    findings.add (item["finding"].toString());

            const auto& behaviour = plugin["behaviour"];

            csv << csvCell (plugin["name"].toString()) << ',' << csvCell (plugin["manufacturer"].toString()) << ','
                << csvCell (plugin["format"].toString()) << ',' << csvCell (plugin["version"].toString()) << ','
                << juce::String ((double) plugin["score"], 1) << ',' << juce::String ((double) plugin["scoreMin"], 1) << ','
                << juce::String ((double) plugin["scoreMax"], 1) << ',' << (int) plugin["reports"] << ','
                << csvCell (plugin["headline"].toString()) << ',' << csvCell (plugin["label"].toString()) << ','
                << (int) plugin["labelAi"] << ',' << (int) plugin["labelHuman"] << ',' << ((bool) plugin["deep"] ? "yes" : "no") << ','
                << (behaviour.isObject() ? juce::String ((double) behaviour["cpuPercent"], 2) : juce::String())
                << ',' << (behaviour.isObject() ? juce::String ((double) behaviour["allocationsPerBlock"], 2) : juce::String())
                << ',' << csvCell (findings.joinIntoString ("; ")) << '\n';
        }

    return csv;
}
} // namespace vibecheck
