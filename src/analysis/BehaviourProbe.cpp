#include "BehaviourProbe.h"

#include "HealthSuite.h"
#include "ui/Theme.h"

#include <algorithm>
#include <atomic>
#include <vector>

#if JUCE_MAC
 #include <pthread.h>
 #include <stdint.h>

extern "C"
{
    typedef void (malloc_logger_t) (uint32_t type, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3,
                                    uintptr_t result, uint32_t numHotFramesToSkip);
    extern malloc_logger_t* malloc_logger;   // libsystem_malloc calls this on every allocation, if set
}
#endif

namespace vibecheck
{
namespace
{
constexpr double sampleRate = 48000.0;

#if JUCE_MAC
// The allocator calls the hook from every thread, including while thread-local storage is still
// being set up, so the hook may use nothing but plain atomics and pthread_self().
std::atomic<pthread_t> countedThread { nullptr };
std::atomic<int> allocationsCounted { 0 };

void countAllocation (uint32_t type, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t)
{
    // Bit 1 is "this call allocated"; bit 4 is "this call freed", and a realloc has both.
    // Virtual-memory calls (bit 16) are the allocator growing itself, not the plugin asking.
    if ((type & 2u) != 0 && (type & 16u) == 0 && pthread_equal (countedThread.load (std::memory_order_relaxed), pthread_self()))
        allocationsCounted.fetch_add (1, std::memory_order_relaxed);
}
#endif

struct Pass
{
    std::vector<double> micros;
    int blocksAllocating = 0;
    int totalAllocations = 0;
    int worstAllocations = 0;
    double totalSeconds = 0.0;
    int totalSamples = 0;
};

struct Probe
{
    juce::AudioPluginInstance& plugin;
    bool instrument = false;
    int channels = 2;
};

/** One pass of steady noise through the plugin, timing every block after a warm-up. */
Pass runPass (Probe& probe, int blockSize, int numBlocks, float gain, bool moveParameters)
{
    Pass pass;
    auto& plugin = probe.plugin;

    {
        juce::MessageManagerLock lock;
        plugin.setPlayConfigDetails (probe.channels, probe.channels, sampleRate, blockSize);
        plugin.prepareToPlay (sampleRate, blockSize);
        plugin.reset();
    }

    juce::Random random (4242);
    juce::AudioBuffer<float> buffer (probe.channels, blockSize);
    juce::MidiBuffer midi;
    auto& parameters = plugin.getParameters();

    const auto warmUp = juce::jmax (8, numBlocks / 8);
    pass.micros.reserve ((size_t) numBlocks);

    for (int block = 0; block < warmUp + numBlocks; ++block)
    {
        for (int channel = 0; channel < probe.channels; ++channel)
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample (channel, i, 0.25f * gain * (random.nextFloat() * 2.0f - 1.0f));

        midi.clear();

        if (probe.instrument && (block % 40) == 0)
        {
            midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 1);
        }

        if (moveParameters && parameters.size() > 0)
            for (int n = 0; n < 6; ++n)
                if (auto* parameter = parameters[random.nextInt (parameters.size())])
                    if (parameter->isAutomatable())
                        parameter->setValue (random.nextFloat());

        const auto counted = block >= warmUp;

#if JUCE_MAC
        allocationsCounted.store (0);
        countedThread.store (pthread_self());
#endif
        const auto start = juce::Time::getHighResolutionTicks();
        plugin.processBlock (buffer, midi);
        const auto end = juce::Time::getHighResolutionTicks();
#if JUCE_MAC
        countedThread.store (nullptr);
        const auto allocationsInBlock = allocationsCounted.load();
#endif

        if (! counted)
            continue;

        const auto seconds = juce::Time::highResolutionTicksToSeconds (end - start);
        pass.micros.push_back (seconds * 1.0e6);
        pass.totalSeconds += seconds;
        pass.totalSamples += blockSize;

#if JUCE_MAC
        if (allocationsInBlock > 0)
            ++pass.blocksAllocating;

        pass.totalAllocations += allocationsInBlock;
        pass.worstAllocations = juce::jmax (pass.worstAllocations, allocationsInBlock);
#endif
    }

    {
        juce::MessageManagerLock lock;
        plugin.releaseResources();
    }

    return pass;
}

double median (std::vector<double> values)
{
    if (values.empty())
        return 0.0;

    std::sort (values.begin(), values.end());
    return values[values.size() / 2];
}

double percentile (std::vector<double> values, double fraction)
{
    if (values.empty())
        return 0.0;

    std::sort (values.begin(), values.end());
    return values[(size_t) juce::jlimit (0.0, (double) values.size() - 1.0, fraction * (double) values.size())];
}

double secondsPerSample (const Pass& pass)
{
    return pass.totalSamples > 0 ? pass.totalSeconds / (double) pass.totalSamples : 0.0;
}
} // namespace

BehaviourReport probeBehaviour (juce::AudioPluginInstance& plugin, const std::atomic<bool>& cancel)
{
    BehaviourReport report;
    const auto started = juce::Time::getMillisecondCounterHiRes();

    Probe probe { plugin };
    probe.instrument = looksLikeInstrument (plugin);
    report.instrument = probe.instrument;

    {
        // Plain stereo on the main buses, with any sidechain switched off, as the renderer does.
        juce::MessageManagerLock lock;
        auto layout = plugin.getBusesLayout();
        const auto stereo = juce::AudioChannelSet::stereo();

        for (int bus = 0; bus < layout.inputBuses.size(); ++bus)
            layout.inputBuses.getReference (bus) = bus == 0 && ! probe.instrument ? stereo : juce::AudioChannelSet::disabled();

        for (int bus = 0; bus < layout.outputBuses.size(); ++bus)
            layout.outputBuses.getReference (bus) = bus == 0 ? stereo : juce::AudioChannelSet::disabled();

        if (plugin.checkBusesLayoutSupported (layout))
            plugin.setBusesLayout (layout);

        probe.channels = juce::jmax (1, plugin.getTotalNumOutputChannels());
    }

#if JUCE_MAC
    malloc_logger = countAllocation;
    report.allocationsMeasured = true;
#endif

    const auto steady = runPass (probe, 512, 600, 1.0f, false);

    if (cancel.load())
    {
#if JUCE_MAC
        malloc_logger = nullptr;
#endif
        report.error = "stopped";
        return report;
    }

    const auto moving = runPass (probe, 512, 300, 1.0f, true);

#if JUCE_MAC
    malloc_logger = nullptr;
#endif

    const auto quiet  = runPass (probe, 512, 300, 1.0e-36f, false);
    const auto tiny   = runPass (probe, 16, 4000, 1.0f, false);

    const auto blocks = (double) juce::jmax<size_t> (1, steady.micros.size());
    report.allocationsPerBlock = steady.totalAllocations / blocks;
    report.fractionOfBlocksAllocating = steady.blocksAllocating / blocks;
    report.worstAllocationsInOneBlock = juce::jmax (steady.worstAllocations, moving.worstAllocations);
    report.allocationsPerBlockAutomated = moving.totalAllocations / (double) juce::jmax<size_t> (1, moving.micros.size());

    const auto blockSeconds = 512.0 / sampleRate;
    report.cpuPercent = 100.0 * (steady.totalSeconds / blocks) / blockSeconds;

    const auto mid = median (steady.micros);
    report.spikeRatio = mid > 0.0 ? percentile (steady.micros, 0.99) / mid : 0.0;

    const auto steadyCost = secondsPerSample (steady);
    report.smallBufferOverhead = steadyCost > 0.0 ? secondsPerSample (tiny) / steadyCost : 0.0;
    report.denormalSlowdown = steadyCost > 0.0 ? secondsPerSample (quiet) / steadyCost : 0.0;

    report.seconds = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;
    report.ok = true;
    return report;
}

juce::String BehaviourReport::toText (const juce::String& pluginName) const
{
    juce::String text;
    text << pluginName << (instrument ? "  (instrument)" : "") << "\n";

    if (! ok)
        return text + "could not be measured: " + error + "\n";

    if (allocationsMeasured)
        text << "allocations on audio thread: " << mbs::num (allocationsPerBlock, 2) << " per block, "
             << mbs::num (fractionOfBlocksAllocating * 100.0, 0) << "% of blocks, worst " << worstAllocationsInOneBlock
             << " in one block; with parameters moving " << mbs::num (allocationsPerBlockAutomated, 2) << " per block\n";
    else
        text << "allocations on audio thread: not measurable on this system\n";

    text << "cpu at 48k/512: " << mbs::num (cpuPercent, 2) << "% of a core\n"
         << "slowest 1% of blocks vs median: " << mbs::num (spikeRatio, 1) << "x\n"
         << "16-sample blocks vs 512, cost per sample: " << mbs::num (smallBufferOverhead, 1) << "x\n"
         << "near-silent vs normal input: " << mbs::num (denormalSlowdown, 2) << "x\n"
         << "measured in " << mbs::num (seconds, 1) << " s\n";
    return text;
}

juce::String BehaviourReport::toMachine() const
{
    juce::StringArray parts;
    const auto add = [&parts] (const char* key, double value) { parts.add (juce::String (key) + "=" + juce::String (value, 6)); };

    add ("ok", ok ? 1 : 0);
    add ("inst", instrument ? 1 : 0);
    add ("meas", allocationsMeasured ? 1 : 0);
    add ("apb", allocationsPerBlock);
    add ("frac", fractionOfBlocksAllocating);
    add ("worst", worstAllocationsInOneBlock);
    add ("apba", allocationsPerBlockAutomated);
    add ("cpu", cpuPercent);
    add ("spike", spikeRatio);
    add ("small", smallBufferOverhead);
    add ("denorm", denormalSlowdown);
    add ("secs", seconds);
    return "BEHAVIOUR" + juce::String (behaviourVersion) + " " + parts.joinIntoString (" ");
}

BehaviourReport BehaviourReport::fromMachine (const juce::String& line)
{
    BehaviourReport report;

    if (! line.startsWith ("BEHAVIOUR" + juce::String (behaviourVersion) + " "))
    {
        report.error = "unrecognised result";
        return report;
    }

    juce::StringArray pairs;
    pairs.addTokens (line.fromFirstOccurrenceOf (" ", false, false), " ", "");

    const auto value = [&pairs] (const char* key)
    {
        for (const auto& pair : pairs)
            if (pair.startsWith (juce::String (key) + "="))
                return pair.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();

        return 0.0;
    };

    report.ok = value ("ok") > 0.5;
    report.instrument = value ("inst") > 0.5;
    report.allocationsMeasured = value ("meas") > 0.5;
    report.allocationsPerBlock = value ("apb");
    report.fractionOfBlocksAllocating = value ("frac");
    report.worstAllocationsInOneBlock = (int) value ("worst");
    report.allocationsPerBlockAutomated = value ("apba");
    report.cpuPercent = value ("cpu");
    report.spikeRatio = value ("spike");
    report.smallBufferOverhead = value ("small");
    report.denormalSlowdown = value ("denorm");
    report.seconds = value ("secs");
    return report;
}
} // namespace vibecheck
