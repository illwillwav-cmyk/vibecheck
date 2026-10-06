#include "Labels.h"

#include <algorithm>
#include <vector>

namespace vibecheck
{
namespace
{
constexpr const char* storeName = "vibeLabels";

double medianOf (std::vector<double> values)
{
    if (values.empty())
        return 0.0;

    std::sort (values.begin(), values.end());
    const auto n = values.size();
    return n % 2 == 1 ? values[n / 2] : 0.5 * (values[n / 2 - 1] + values[n / 2]);
}

juce::String padded (const juce::String& text, int width)
{
    return text.length() >= width ? text.substring (0, width) : text + juce::String::repeatedString (" ", width - text.length());
}

/** A plugin's label in either file layout: "label" in an export, vote counts in a master list. */
Label labelOf (const juce::var& plugin)
{
    if (plugin.hasProperty ("labelAi") || plugin.hasProperty ("labelHuman"))
    {
        const auto ai = (int) plugin["labelAi"], human = (int) plugin["labelHuman"];
        return ai > human ? Label::vibeCoded : human > ai ? Label::handWritten : Label::none;
    }

    return labelFromString (plugin["label"].toString());
}
} // namespace

juce::String toString (Label label)
{
    return label == Label::vibeCoded ? "ai" : label == Label::handWritten ? "human" : juce::String();
}

Label labelFromString (const juce::String& text)
{
    const auto lower = text.trim().toLowerCase();
    return lower == "ai" ? Label::vibeCoded : lower == "human" ? Label::handWritten : Label::none;
}

juce::String describe (Label label)
{
    return label == Label::vibeCoded ? "Known vibe-coded" : label == Label::handWritten ? "Known hand-written" : "Not labelled";
}

juce::String labelKey (const juce::String& name, const juce::String& manufacturer)
{
    return (name.trim() + "|" + manufacturer.trim()).toLowerCase();
}

juce::String labelKey (const juce::PluginDescription& description)
{
    return labelKey (description.name, description.manufacturerName);
}

std::map<juce::String, Label> allLabels (juce::PropertiesFile* settings)
{
    std::map<juce::String, Label> labels;

    if (settings != nullptr)
        if (const auto xml = settings->getXmlValue (storeName))
            for (auto* item : xml->getChildIterator())
                if (const auto label = labelFromString (item->getStringAttribute ("label")); label != Label::none)
                    labels[item->getStringAttribute ("key")] = label;

    return labels;
}

Label getLabel (juce::PropertiesFile* settings, const juce::PluginDescription& description)
{
    const auto labels = allLabels (settings);
    const auto found = labels.find (labelKey (description));
    return found != labels.end() ? found->second : Label::none;
}

void setLabel (juce::PropertiesFile* settings, const juce::PluginDescription& description, Label label)
{
    if (settings == nullptr)
        return;

    auto xml = settings->getXmlValue (storeName);

    if (xml == nullptr)
        xml = std::make_unique<juce::XmlElement> ("LABELS");

    const auto key = labelKey (description);

    if (auto* existing = xml->getChildByAttribute ("key", key))
        xml->removeChildElement (existing, true);

    if (label != Label::none)
    {
        auto* item = xml->createNewChildElement ("ITEM");
        item->setAttribute ("key", key);
        item->setAttribute ("label", toString (label));
    }

    settings->setValue (storeName, xml.get());
    settings->saveIfNeeded();
}

juce::String evaluateLabels (const juce::var& plugins)
{
    struct Row { double score; bool conclusive; juce::StringArray findings; };
    std::vector<Row> ai, human;
    int total = 0;

    if (const auto* list = plugins.getArray())
        for (const auto& plugin : *list)
        {
            ++total;
            const auto label = labelOf (plugin);

            if (label == Label::none)
                continue;

            Row row { (double) plugin["score"], (bool) plugin["conclusive"], {} };

            if (const auto* findings = plugin["findings"].getArray())
                for (const auto& finding : *findings)
                    row.findings.addIfNotAlreadyThere (finding["finding"].toString());

            (label == Label::vibeCoded ? ai : human).push_back (std::move (row));
        }

    juce::String text;
    text << "Labelled plugins: " << (int) ai.size() << " known vibe-coded, " << (int) human.size()
         << " known hand-written (of " << total << " rows)\n";

    if (ai.empty() || human.empty())
    {
        text << "\nBoth kinds are needed to measure anything. Label some plugins you are sure about in AI Check\n"
                "(the menu at the top of the evidence panel), or merge exports from people who have.\n";
        return text;
    }

    const auto judged = [] (const std::vector<Row>& rows)
    {
        std::vector<double> scores;

        for (const auto& row : rows)
            if (row.conclusive)
                scores.push_back (row.score);

        return scores;
    };

    const auto aiScores = judged (ai), humanScores = judged (human);
    const auto unjudged = (int) (ai.size() - aiScores.size() + human.size() - humanScores.size());

    const auto range = [] (const std::vector<double>& scores)
    {
        if (scores.empty())
            return juce::String ("none could be judged");

        return "min " + juce::String (*std::min_element (scores.begin(), scores.end()), 0) + "   median " + juce::String (medianOf (scores), 0)
               + "   max " + juce::String (*std::max_element (scores.begin(), scores.end()), 0);
    };

    text << "\nScores\n"
         << "  vibe-coded     " << range (aiScores) << "\n"
         << "  hand-written   " << range (humanScores) << "\n";

    const auto countAtOrAbove = [] (const std::vector<double>& scores, double line)
    {
        return (int) std::count_if (scores.begin(), scores.end(), [line] (double s) { return s >= line; });
    };

    const auto atLine = [&] (const char* name, double line)
    {
        const auto caught = countAtOrAbove (aiScores, line), wrong = countAtOrAbove (humanScores, line);
        return juce::String ("  ") + padded (name, 34) + "catches " + juce::String (caught) + " of " + juce::String ((int) aiScores.size())
               + " vibe-coded, wrongly flags " + juce::String (wrong) + " of " + juce::String ((int) humanScores.size()) + " hand-written\n";
    };

    if (! aiScores.empty() && ! humanScores.empty())
    {
        text << "\nWhat each verdict line does\n"
             << atLine ("\"Some template smell\" (10)", 10.0)
             << atLine ("\"Substantial fingerprints\" (30)", 30.0)
             << atLine ("\"Machine-generated\" (55)", 55.0);

        // The threshold that gets the most right, counting both kinds of mistake equally.
        double best = 0.0;
        int bestCorrect = -1;

        for (int line = 1; line <= 100; ++line)
        {
            const auto correct = countAtOrAbove (aiScores, line) + ((int) humanScores.size() - countAtOrAbove (humanScores, line));

            if (correct > bestCorrect)
            {
                bestCorrect = correct;
                best = line;
            }
        }

        text << "\nBest single line: " << juce::String (best, 0) << "  (" << bestCorrect << " of "
             << (int) (aiScores.size() + humanScores.size()) << " right: catches " << countAtOrAbove (aiScores, best) << " of "
             << (int) aiScores.size() << ", wrongly flags " << countAtOrAbove (humanScores, best) << " of " << (int) humanScores.size() << ")\n";
    }

    if (unjudged > 0)
        text << "\n" << unjudged << " labelled row" << (unjudged == 1 ? "" : "s") << " could not be judged (unreadable binary) and "
             << (unjudged == 1 ? "is" : "are") << " left out above.\n";

    // Which fingerprints do the separating. One that shows up as often in hand-written plugins as in
    // generated ones is not earning its points.
    std::map<juce::String, std::pair<int, int>> counts;

    for (const auto& row : ai)    for (const auto& f : row.findings) ++counts[f].first;
    for (const auto& row : human) for (const auto& f : row.findings) ++counts[f].second;

    std::vector<std::pair<juce::String, std::pair<int, int>>> ordered (counts.begin(), counts.end());
    std::sort (ordered.begin(), ordered.end(), [&] (const auto& a, const auto& b)
    {
        const auto rate = [&] (const std::pair<int, int>& c) { return (double) c.first / (double) ai.size() - (double) c.second / (double) human.size(); };
        return rate (a.second) > rate (b.second);
    });

    text << "\nFingerprints, most telling first\n"
         << "  " << padded ("fingerprint", 58) << padded ("vibe-coded", 14) << "hand-written\n";

    for (const auto& [finding, count] : ordered)
        text << "  " << padded (finding, 58) << padded (juce::String (count.first) + " of " + juce::String ((int) ai.size()), 14)
             << count.second << " of " << (int) human.size() << "\n";

    text << "\nWith this few labelled plugins these are indications, not error rates. Every confirmed label makes them firmer.\n";
    return text;
}
} // namespace vibecheck
