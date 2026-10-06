#include "HeadlessAnalyze.h"

#include "PluginLoader.h"
#include "Watchdog.h"
#include "audio/OfflineRenderer.h"
#include "audio/TestSignal.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace
{
void print (const juce::String& line)
{
    std::cout << line << std::endl;
}

}

HeadlessAnalyze::HeadlessAnalyze (PluginScanner& scannerToUse, juce::String pluginNameQuery, std::function<void()> onFinished)
    : juce::Thread ("VibeCheck headless analysis"),
      scanner (scannerToUse),
      query (std::move (pluginNameQuery)),
      finished (std::move (onFinished))
{
}

HeadlessAnalyze::~HeadlessAnalyze()
{
    stopThread (10000);
}

void HeadlessAnalyze::start()
{
    startThread();
}

void HeadlessAnalyze::run()
{
    vibecheck::Watchdog watchdog (120);

    const auto types = scanner.getKnownPluginList().getTypes();

    const juce::PluginDescription* match = nullptr;

    for (const auto& type : types)
        if (type.name.containsIgnoreCase (query))
        {
            match = &type;
            break;
        }

    if (match == nullptr)
    {
        print ("no plugin found matching \"" + query + "\" - run --scan first, or try another name");
        juce::MessageManager::callAsync (finished);
        return;
    }

    print (match->name + "  [" + match->pluginFormatName + "]  by " + match->manufacturerName
               + "  v" + match->version);

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    auto loaded = vibecheck::loadPlugin (scanner.getFormatManager(), *match, sampleRate, blockSize);

    if (loaded.instance == nullptr)
    {
        print ("could not load: " + loaded.error);
        juce::MessageManager::callAsync (finished);
        return;
    }

    const auto runSignal = [&] (const vibecheck::SignalSpec& spec)
    {
        print ("");
        print ("signal: " + vibecheck::describe (spec));

        const auto input = vibecheck::generate (spec);

        vibecheck::OfflineRenderer::Options options;
        options.sampleRate = spec.sampleRate;
        options.blockSize = blockSize;
        options.tailSamples = (int) spec.sampleRate / 2;

        print (vibecheck::OfflineRenderer::render (*loaded.instance, input, options).summary());
    };

    vibecheck::SignalSpec sine;
    sine.type = vibecheck::SignalType::sine;
    sine.sampleRate = sampleRate;
    sine.numSamples = (int) sampleRate;
    runSignal (sine);

    vibecheck::SignalSpec impulse;
    impulse.type = vibecheck::SignalType::impulse;
    impulse.sampleRate = sampleRate;
    impulse.numSamples = (int) sampleRate;
    impulse.amplitudeDb = 0.0;
    runSignal (impulse);

    vibecheck::SignalSpec silence;
    silence.type = vibecheck::SignalType::silence;
    silence.sampleRate = sampleRate;
    silence.numSamples = (int) sampleRate;
    runSignal (silence);

    // Plugin instances expect to be destroyed on the message thread.
    auto* released = loaded.instance.release();
    juce::MessageManager::callAsync ([released, callback = finished]
                                     {
                                         delete released;
                                         callback();
                                     });
}
