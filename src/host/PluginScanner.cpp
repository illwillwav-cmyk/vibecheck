#include "PluginScanner.h"
#include "ScanWorker.h"

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace
{
constexpr const char* pluginListKey = "pluginList";

/** A worker process probes one plugin at a time. Anything slower than this is treated as a
    hang: the worker is discarded and the plugin is recorded as failing to scan. */
constexpr int scanTimeoutMs = 30000;

/** Coordinator side of the scan connection: owns the worker process and turns its
    asynchronous replies into a blocking request/response call. */
class Superprocess final : private juce::ChildProcessCoordinator
{
public:
    Superprocess()
    {
        launchWorkerProcess (juce::File::getSpecialLocation (juce::File::currentExecutableFile),
                             vibecheck::scanProcessUID,
                             0,
                             0);
    }

    ~Superprocess() override
    {
        killWorkerProcess();
    }

    enum class State { timeout, gotResult, connectionLost };

    struct Response
    {
        State state;
        std::unique_ptr<juce::XmlElement> xml;
    };

    Response getResponse()
    {
        std::unique_lock<std::mutex> lock (mutex);

        if (! condvar.wait_for (lock,
                                std::chrono::milliseconds (scanTimeoutMs),
                                [&] { return gotResult || connectionLost; }))
            return { State::timeout, nullptr };

        const auto state = connectionLost ? State::connectionLost : State::gotResult;
        gotResult = false;
        connectionLost = false;

        return { state, std::move (result) };
    }

    using ChildProcessCoordinator::sendMessageToWorker;

private:
    void handleMessageFromWorker (const juce::MemoryBlock& mb) override
    {
        const std::lock_guard<std::mutex> lock (mutex);

        juce::MemoryInputStream stream { mb, false };
        result = juce::parseXML (stream.readString());
        gotResult = true;
        condvar.notify_one();
    }

    void handleConnectionLost() override
    {
        const std::lock_guard<std::mutex> lock (mutex);
        connectionLost = true;
        condvar.notify_one();
    }

    std::mutex mutex;
    std::condition_variable condvar;
    std::unique_ptr<juce::XmlElement> result;
    bool gotResult = false;
    bool connectionLost = false;
};

/** Hands each probe to a worker process instead of loading the plugin here. */
class CustomPluginScanner final : public juce::KnownPluginList::CustomScanner
{
public:
    bool findPluginTypesFor (juce::AudioPluginFormat& format,
                             juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrIdentifier) override
    {
        if (superprocess == nullptr)
            superprocess = std::make_unique<Superprocess>();

        juce::MemoryBlock request;
        {
            juce::MemoryOutputStream stream { request, true };
            stream.writeString (format.getName());
            stream.writeString (fileOrIdentifier);
        }

        if (! superprocess->sendMessageToWorker (request))
        {
            superprocess = nullptr;
            return false;
        }

        const auto response = superprocess->getResponse();

        if (response.state != Superprocess::State::gotResult)
        {
            // The worker hung or died on this plugin. Start the next one with a fresh process.
            superprocess = nullptr;
            return false;
        }

        if (response.xml != nullptr)
        {
            for (auto* child : response.xml->getChildIterator())
            {
                auto description = std::make_unique<juce::PluginDescription>();

                if (description->loadFromXml (*child))
                    result.add (description.release());
            }
        }

        return true;
    }

    void scanFinished() override
    {
        superprocess = nullptr;
    }

private:
    std::unique_ptr<Superprocess> superprocess;
};
} // namespace

PluginScanner::PluginScanner()
{
    juce::PropertiesFile::Options options;
    options.applicationName     = "VibeCheck";
    options.filenameSuffix      = "settings";
    options.folderName          = "VibeCheck";
    options.osxLibrarySubFolder = "Application Support";
    appProperties.setStorageParameters (options);

    formatManager.addDefaultFormats();
    knownPluginList.setCustomScanner (std::make_unique<CustomPluginScanner>());

    if (auto* settings = getSettings())
        if (auto xml = settings->getXmlValue (pluginListKey))
            knownPluginList.recreateFromXml (*xml);

    // A plugin that crashed the worker mid-probe leaves its name in the dead man's pedal.
    // Blacklisting it here, before anything scans again, is what stops it being probed - and
    // crashing - at every single launch. The result is saved immediately rather than waiting
    // for the change message, which would be lost if the next probe took the app down first.
    const auto blacklistedBefore = knownPluginList.getBlacklistedFiles().size();
    juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal (knownPluginList, getDeadMansPedalFile());

    if (knownPluginList.getBlacklistedFiles().size() != blacklistedBefore)
        save();

    knownPluginList.addChangeListener (this);
}

PluginScanner::~PluginScanner()
{
    knownPluginList.removeChangeListener (this);
    save();
}

juce::File PluginScanner::getDeadMansPedalFile()
{
    if (auto* settings = getSettings())
        return settings->getFile().getSiblingFile ("VibeCheckScanCrashLog");

    return juce::File::getSpecialLocation (juce::File::tempDirectory)
               .getChildFile ("VibeCheckScanCrashLog");
}

void PluginScanner::save()
{
    if (auto* settings = getSettings())
    {
        if (auto xml = knownPluginList.createXml())
            settings->setValue (pluginListKey, xml.get());

        settings->saveIfNeeded();
    }
}

void PluginScanner::clearSkippedPlugins()
{
    knownPluginList.clearBlacklistedFiles();

    // The dead man's pedal is what puts them back on the list at the next launch, so it has to
    // go too or the reset would not survive a restart.
    getDeadMansPedalFile().deleteFile();

    save();
}

void PluginScanner::changeListenerCallback (juce::ChangeBroadcaster*)
{
    save();
}
