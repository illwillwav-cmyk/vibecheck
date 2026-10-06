#include "LibrarySweep.h"

#include "BehaviourCache.h"
#include "BinaryInspector.h"

#include <atomic>
#include <map>
#include <mutex>
#include <thread>

namespace vibecheck
{
namespace
{
constexpr const char* cacheKey = "vibeSweepCache";

/** Identifies a cached result: the plugin, plus when its file was last written, so a plugin that
    has been updated since the last sweep is weighed again rather than remembered wrongly. */
juce::String cacheKeyFor (const juce::PluginDescription& description, juce::int64 modified)
{
    return "h" + juce::String (heuristicsVersion) + "|" + description.fileOrIdentifier + "|" + description.name + "|" + juce::String (modified);
}
} // namespace

juce::String binaryFingerprint (const juce::File& file)
{
    if (! file.existsAsFile())
        return {};

    // Size plus the first and last quarter-megabyte: cheap enough for a whole library, and two
    // different builds of a plugin do not share all three.
    juce::FileInputStream stream (file);

    if (! stream.openedOk())
        return {};

    const auto size = stream.getTotalLength();
    const auto chunk = (juce::int64) 256 * 1024;

    juce::MemoryBlock data;
    stream.readIntoMemoryBlock (data, (size_t) juce::jmin (chunk, size));

    if (size > chunk)
    {
        stream.setPosition (juce::jmax ((juce::int64) 0, size - chunk));
        stream.readIntoMemoryBlock (data, (size_t) chunk);
    }

    // FNV-1a over the sampled bytes. This is an identity, not a security measure.
    juce::uint64 hash = 14695981039346656037ull;

    for (size_t i = 0; i < data.getSize(); ++i)
        hash = (hash ^ (juce::uint8) data[i]) * 1099511628211ull;

    return juce::String (size) + "-" + juce::String::toHexString ((juce::int64) hash);
}

juce::String SweepSummary::line() const
{
    if (total == 0)
        return "nothing scanned yet";

    juce::String text;
    text << juce::String (flaggedPercent(), 1) << "% of the library reads as machine-generated  -  "
         << flagged << " of " << judged << " plugins that could be read";

    if (const auto opaque = total - judged; opaque > 0)
        text << "  -  " << opaque << " could not be read at all";

    return text;
}

SweepSummary summarise (const std::vector<SweepEntry>& entries)
{
    SweepSummary summary;
    summary.total = (int) entries.size();

    double scoreSum = 0.0;

    for (const auto& entry : entries)
    {
        if (! entry.conclusive)
            continue;

        ++summary.judged;
        scoreSum += entry.score;

        if (entry.score >= flagThreshold)
            ++summary.flagged;
    }

    summary.averageScore = summary.judged > 0 ? scoreSum / (double) summary.judged : 0.0;
    return summary;
}

LibrarySweep::LibrarySweep() : juce::Thread ("VibeCheck library sweep") {}

LibrarySweep::~LibrarySweep()
{
    cancel();
}

void LibrarySweep::cancel()
{
    stopThread (8000);
}

void LibrarySweep::start (juce::Array<juce::PluginDescription> types,
                          juce::PropertiesFile* cacheFile,
                          std::function<void (int, int, const std::vector<SweepEntry>&)> onProgress,
                          std::function<void (std::vector<SweepEntry>)> onFinished)
{
    cancel();

    pending = std::move (types);
    cache = cacheFile;
    progress = std::move (onProgress);
    finished = std::move (onFinished);

    startThread();
}

void LibrarySweep::run()
{
    auto inspector = createBinaryInspector();

    // Read from the cache of the last sweep, so only new or changed plugins cost anything.
    std::map<juce::String, SweepEntry> cached;

    if (cache != nullptr)
    {
        if (const auto xml = cache->getXmlValue (cacheKey))
        {
            for (auto* item : xml->getChildIterator())
            {
                SweepEntry entry;
                entry.score        = item->getDoubleAttribute ("score");
                entry.confidence   = item->getDoubleAttribute ("confidence");
                entry.conclusive   = item->getBoolAttribute ("conclusive");
                entry.headline     = item->getStringAttribute ("headline");
                entry.fingerprints = item->getIntAttribute ("fingerprints");
                entry.deep         = item->getStringAttribute ("key").endsWith ("|deep");
                entry.binaryId     = item->getStringAttribute ("binary");
                entry.behaviourLine = item->getStringAttribute ("behaviour");

                for (auto* finding : item->getChildIterator())
                    entry.findings.push_back ({ finding->getStringAttribute ("family"), finding->getStringAttribute ("finding"), finding->getDoubleAttribute ("points") });

                cached[item->getStringAttribute ("key")] = entry;
            }
        }
    }

    const auto total = pending.size();
    std::vector<SweepEntry> results ((std::size_t) total);
    std::vector<juce::String> keys ((std::size_t) total);
    std::vector<char> ready ((std::size_t) total, 0);
    std::vector<std::optional<BehaviourReport>> behaviours ((std::size_t) total);

    // Plugins that share a binary (every Apple unit lives in one CoreAudio bundle) are grouped, so
    // each binary is read once, scored for all of its plugins, and then let go. Holding every
    // plugin's symbol and string tables until the end used gigabytes on a large library.
    std::map<juce::String, std::pair<juce::File, std::vector<int>>> groups;

    for (int i = 0; i < total; ++i)
    {
        const auto& description = pending[i];
        const auto bundle = inspector->locate (description);
        const auto modified = bundle.exists() ? bundle.getLastModificationTime().toMilliseconds() : 0;

        // A plugin that has had a deep check carries its measured behaviour into the score, and
        // the cache key says so, so a score taken without it is never reused for one with it.
        behaviours[(std::size_t) i] = findBehaviour (cache, description, modified);
        const auto key = cacheKeyFor (description, modified) + (behaviours[(std::size_t) i].has_value() ? "|deep" : "");

        keys[(std::size_t) i] = key;
        results[(std::size_t) i].description = description;

        if (const auto found = cached.find (key); found != cached.end())
        {
            auto entry = found->second;
            entry.description = description;
            results[(std::size_t) i] = std::move (entry);
            ready[(std::size_t) i] = 1;
        }
        else
        {
            auto& group = groups[bundle.getFullPathName()];
            group.first = bundle;
            group.second.push_back (i);
        }
    }

    std::vector<std::pair<juce::File, std::vector<int>>> work;
    work.reserve (groups.size());

    for (auto& [path, group] : groups)
        work.push_back (std::move (group));

    // Read several binaries at once. Each thread has its own inspector and writes only the entries
    // of the group it is on; the lock covers those writes and the monitor's copy of the results.
    std::mutex lock;
    std::atomic<int> nextGroup { 0 };
    std::atomic<int> finishedGroups { 0 };

    const auto threadCount = (int) juce::jlimit (1u, 8u, juce::jmax (1u, std::thread::hardware_concurrency()) - (std::thread::hardware_concurrency() > 2 ? 1u : 0u));
    std::vector<std::thread> workers;

    for (int t = 0; t < threadCount; ++t)
        workers.emplace_back ([&]
        {
            auto local = createBinaryInspector();

            for (;;)
            {
                const auto index = nextGroup.fetch_add (1);

                if (index >= (int) work.size() || threadShouldExit())
                    break;

                const auto& [bundle, indices] = work[(std::size_t) index];
                const auto facts = local->inspectBundle (bundle);

                std::vector<std::pair<int, SweepEntry>> scored;

                for (const auto i : indices)
                {
                    const auto& behaviour = behaviours[(std::size_t) i];
                    const auto report = assessVibe (facts, pending[i], behaviour.has_value() ? &*behaviour : nullptr);
                    SweepEntry entry;
                    entry.description  = pending[i];
                    entry.score        = report.score;
                    entry.confidence   = report.confidence;
                    entry.conclusive   = report.conclusive;
                    entry.headline     = report.headline;
                    entry.fingerprints = (int) report.evidence.size();
                    entry.deep         = behaviour.has_value();
                    entry.binaryId     = binaryFingerprint (facts.executable);
                    entry.behaviourLine = behaviour.has_value() ? behaviour->toMachine() : juce::String();

                    for (const auto& item : report.evidence)
                        entry.findings.push_back ({ toString (item.family), item.finding, item.points });

                    scored.emplace_back (i, std::move (entry));
                }

                {
                    const std::lock_guard<std::mutex> guard (lock);

                    for (auto& [i, entry] : scored)
                    {
                        results[(std::size_t) i] = std::move (entry);
                        ready[(std::size_t) i] = 1;
                    }
                }

                ++finishedGroups;
            }
        });

    // Report progress while they work.
    while (finishedGroups.load() < (int) work.size() && ! threadShouldExit())
    {
        wait (200);

        if (progress == nullptr)
            continue;

        std::vector<SweepEntry> snapshot;

        {
            const std::lock_guard<std::mutex> guard (lock);

            for (std::size_t i = 0; i < results.size(); ++i)
                if (ready[i])
                    snapshot.push_back (results[i]);
        }

        const auto done = (int) snapshot.size();
        juce::MessageManager::callAsync ([callback = progress, done, total, current = std::move (snapshot)] { callback (done, total, current); });
    }

    for (auto& worker : workers)
        worker.join();

    if (threadShouldExit())
        return;

    juce::XmlElement store ("SWEEP");

    for (std::size_t i = 0; i < results.size(); ++i)
    {
        auto* item = store.createNewChildElement ("PLUGIN");
        item->setAttribute ("key", keys[i]);
        item->setAttribute ("score", results[i].score);
        item->setAttribute ("confidence", results[i].confidence);
        item->setAttribute ("conclusive", results[i].conclusive);
        item->setAttribute ("headline", results[i].headline);
        item->setAttribute ("fingerprints", results[i].fingerprints);
        item->setAttribute ("binary", results[i].binaryId);
        item->setAttribute ("behaviour", results[i].behaviourLine);

        for (const auto& finding : results[i].findings)
        {
            auto* child = item->createNewChildElement ("F");
            child->setAttribute ("family", finding.family);
            child->setAttribute ("finding", finding.finding);
            child->setAttribute ("points", finding.points);
        }
    }

    if (cache != nullptr)
    {
        juce::MessageManager::callAsync ([file = cache, xml = store.toString()]
        {
            if (auto parsed = juce::parseXML (xml))
            {
                file->setValue (cacheKey, parsed.get());
                file->saveIfNeeded();
            }
        });
    }

    if (finished != nullptr)
        juce::MessageManager::callAsync ([callback = finished, finalResults = std::move (results)]() mutable
                                         { callback (std::move (finalResults)); });
}
} // namespace vibecheck
