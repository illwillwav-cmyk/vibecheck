#include "BehaviourCache.h"

namespace vibecheck
{
namespace
{
constexpr const char* cacheName = "vibeBehaviourCache";

juce::String keyFor (const juce::PluginDescription& description, juce::int64 modified)
{
    return "b" + juce::String (behaviourVersion) + "|" + description.fileOrIdentifier + "|" + description.name + "|" + juce::String (modified);
}
} // namespace

std::optional<BehaviourReport> findBehaviour (juce::PropertiesFile* settings, const juce::PluginDescription& description, juce::int64 modified)
{
    if (settings == nullptr)
        return std::nullopt;

    if (const auto xml = settings->getXmlValue (cacheName))
        if (const auto* item = xml->getChildByAttribute ("key", keyFor (description, modified)))
        {
            auto report = BehaviourReport::fromMachine (item->getStringAttribute ("result"));

            if (report.ok)
                return report;
        }

    return std::nullopt;
}

void storeBehaviour (juce::PropertiesFile* settings, const juce::PluginDescription& description, juce::int64 modified, const BehaviourReport& report)
{
    if (settings == nullptr || ! report.ok)
        return;

    auto xml = settings->getXmlValue (cacheName);

    if (xml == nullptr)
        xml = std::make_unique<juce::XmlElement> ("BEHAVIOUR");

    const auto key = keyFor (description, modified);

    if (auto* existing = xml->getChildByAttribute ("key", key))
        xml->removeChildElement (existing, true);

    auto* item = xml->createNewChildElement ("ITEM");
    item->setAttribute ("key", key);
    item->setAttribute ("result", report.toMachine());

    settings->setValue (cacheName, xml.get());
    settings->saveIfNeeded();
}

BehaviourReport measureInChildProcess (const juce::PluginDescription& description, int timeoutMs, const std::atomic<bool>& cancel)
{
    BehaviourReport failed;

    juce::ChildProcess child;
    const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

    if (! child.start (juce::StringArray { self.getFullPathName(), "--behaviour=id:" + description.fileOrIdentifier }))
    {
        failed.error = "could not start the measuring process";
        return failed;
    }

    juce::String output;
    const auto started = juce::Time::getMillisecondCounter();

    while (child.isRunning())
    {
        char buffer[4096];

        if (const auto read = child.readProcessOutput (buffer, (int) sizeof (buffer)); read > 0)
            output += juce::String::fromUTF8 (buffer, read);
        else
            juce::Thread::sleep (50);

        if (cancel.load() || juce::Time::getMillisecondCounter() - started > (juce::uint32) timeoutMs)
        {
            child.kill();
            failed.error = cancel.load() ? "stopped" : "the plugin did not finish in time, which usually means it needs hardware or a licence to run";
            return failed;
        }
    }

    output += child.readAllProcessOutput();

    for (const auto& line : juce::StringArray::fromLines (output))
        if (line.startsWith ("BEHAVIOUR"))
            return BehaviourReport::fromMachine (line);

    // Exit code 2 is the measuring process saying it could not find or load the plugin; anything else
    // non-zero means it died part-way, which is the plugin's doing.
    const auto code = child.getExitCode();

    if (output.contains ("nothing found matching"))
        failed.error = "the plugin could not be found by the measuring process";
    else if (output.contains ("could not load"))
        failed.error = "the plugin would not load";
    else
        failed.error = code != 0 ? "the plugin crashed while it was being measured" : "no result came back";
    return failed;
}
} // namespace vibecheck
