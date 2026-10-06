#include "ScanWorker.h"

namespace
{
/** The child-process side of plugin scanning. Lives in a throwaway process so that a
    plugin which crashes or hangs while being probed takes only this process with it. */
class ScanWorker final : public juce::ChildProcessWorker,
                         private juce::AsyncUpdater
{
public:
    ScanWorker()
    {
        formatManager.addDefaultFormats();
    }

    ~ScanWorker() override
    {
        cancelPendingUpdate();
    }

    void handleMessageFromCoordinator (const juce::MemoryBlock& mb) override
    {
        {
            const juce::ScopedLock lock (queueLock);
            pending.add (mb);
        }

        // Scanning loads arbitrary third-party code, so keep it off the IPC callback.
        triggerAsyncUpdate();
    }

    void handleConnectionLost() override
    {
        juce::JUCEApplicationBase::quit();
    }

private:
    void handleAsyncUpdate() override
    {
        for (;;)
        {
            juce::MemoryBlock next;

            {
                const juce::ScopedLock lock (queueLock);

                if (pending.isEmpty())
                    return;

                next = pending.removeAndReturn (0);
            }

            scan (next);
        }
    }

    void scan (const juce::MemoryBlock& request)
    {
        juce::MemoryInputStream stream { request, false };
        const auto formatName    = stream.readString();
        const auto fileOrIdentifier = stream.readString();

        juce::OwnedArray<juce::PluginDescription> found;

        for (auto* format : formatManager.getFormats())
            if (format->getName() == formatName)
                format->findAllTypesForFile (found, fileOrIdentifier);

        juce::XmlElement result ("PLUGINS");

        for (const auto* description : found)
            result.addChildElement (description->createXml().release());

        juce::MemoryBlock response;
        {
            juce::MemoryOutputStream out { response, true };
            out.writeString (result.toString());
        }

        sendMessageToCoordinator (response);
    }

    juce::AudioPluginFormatManager formatManager;
    juce::CriticalSection queueLock;
    juce::Array<juce::MemoryBlock> pending;
};
} // namespace

std::unique_ptr<juce::ChildProcessWorker> createScanWorkerIfRequested (const juce::String& commandLine)
{
    auto worker = std::make_unique<ScanWorker>();

    if (worker->initialiseFromCommandLine (commandLine, vibecheck::scanProcessUID))
        return worker;

    return nullptr;
}
