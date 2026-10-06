#include "HealthSuite.h"

#include "Measurements.h"
#include "audio/OfflineRenderer.h"
#include "audio/TestSignal.h"

#include <cmath>
#include <set>

namespace vibecheck
{
namespace
{
constexpr double standardRate = 48000.0;

struct Context
{
    juce::AudioPluginInstance& plugin;
    bool instrument = false;
    juce::Random random { 20261004 };
    std::vector<float> original;

    explicit Context (juce::AudioPluginInstance& instance) : plugin (instance) {}

    juce::AudioProcessorParameter* bypass() const { return plugin.getBypassParameter(); }
};

struct Probe
{
    juce::AudioBuffer<float> input;
    RenderOptions options;
};

/** The thing to play through the plugin: noise for an effect, a held note for an instrument. */
Probe makeProbe (const Context& ctx, double seconds, double rate, int block, double amplitudeDb = -6.0,
                 int channels = 2, SignalType effectSignal = SignalType::whiteNoise, int note = 60)
{
    SignalSpec spec;
    spec.sampleRate = rate;
    spec.numChannels = channels;
    spec.numSamples = juce::jmax (64, (int) (rate * seconds));
    spec.amplitudeDb = amplitudeDb;
    spec.type = ctx.instrument ? SignalType::silence : effectSignal;

    Probe probe;
    probe.input = generate (spec);
    probe.options.sampleRate = rate;
    probe.options.blockSize = block;

    if (ctx.instrument)
    {
        probe.options.midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
        probe.options.midi.addEvent (juce::MidiMessage::noteOff (1, note), (int) (spec.numSamples * 0.8));
    }

    return probe;
}

RenderResult play (Context& ctx, const Probe& probe)
{
    return OfflineRenderer::render (ctx.plugin, probe.input, probe.options);
}

/** True when every sample is a real number. Also reports the loudest, in dBFS. */
bool allFinite (const juce::AudioBuffer<float>& buffer, double* peakDb = nullptr, int* badSample = nullptr)
{
    float peak = 0.0f;

    for (int c = 0; c < buffer.getNumChannels(); ++c)
    {
        const auto* samples = buffer.getReadPointer (c);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            if (! std::isfinite (samples[i]))
            {
                if (badSample != nullptr)
                    *badSample = i;

                return false;
            }

            peak = juce::jmax (peak, std::abs (samples[i]));
        }
    }

    if (peakDb != nullptr)
        *peakDb = juce::Decibels::gainToDecibels (peak, -200.0f);

    return true;
}

/** How far apart two renders are: the RMS of their difference against the RMS of the first, in dB. */
double differenceDb (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    const auto channels = juce::jmin (a.getNumChannels(), b.getNumChannels());
    const auto samples = juce::jmin (a.getNumSamples(), b.getNumSamples());
    double reference = 0.0, difference = 0.0;

    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < samples; ++i)
        {
            const double x = a.getSample (c, i), y = b.getSample (c, i);
            reference += x * x;
            difference += (x - y) * (x - y);
        }

    if (difference <= 0.0)
        return -300.0;

    if (reference <= 0.0)
        return 0.0;

    return 10.0 * std::log10 (difference / reference);
}

void setResult (HealthTest& t, Verdict verdict, const juce::String& text)
{
    t.verdict = verdict;
    t.result = text;
}

/** Puts every parameter somewhere: random, or flat out at one end. */
void scatterParameters (Context& ctx, int mode)   // 0 minimum, 1 maximum, 2 random
{
    for (auto* parameter : ctx.plugin.getParameters())
    {
        if (parameter == ctx.bypass())
            continue;

        float value = mode == 0 ? 0.0f : mode == 1 ? 1.0f : ctx.random.nextFloat();

        if (mode == 2 && parameter->isDiscrete() && parameter->getNumSteps() > 1)
            value = (float) ctx.random.nextInt (parameter->getNumSteps()) / (float) (parameter->getNumSteps() - 1);

        parameter->setValue (value);
    }
}

// --- The tests ------------------------------------------------------------------------------------

struct Definition
{
    juce::String id, title, explain;
    bool effectsOnly = false, instrumentsOnly = false;
    std::function<void (Context&, HealthTest&)> run;
};

std::vector<Definition> definitions()
{
    std::vector<Definition> list;

    list.push_back ({ "prepare", "Starts up and renders",
                      "If a plugin cannot prepare and process audio at ordinary settings, nothing else matters.",
                      false, false,
                      [] (Context& ctx, HealthTest& t)
                      {
                          auto probe = makeProbe (ctx, 0.5, standardRate, 512);
                          const auto result = play (ctx, probe);

                          if (! result.ok)
                              return setResult (t, Verdict::fail, "Could not render: " + result.error);

                          setResult (t, Verdict::pass, "Rendered half a second of audio " + juce::String (juce::roundToInt (result.timesFasterThanRealtime))
                                                           + "x faster than realtime, " + juce::String (result.numInputChannels) + " in, "
                                                           + juce::String (result.numOutputChannels) + " out.");
                      } });

    list.push_back ({ "silence", "Silent when nothing is playing",
                      "A plugin that makes noise with no input and no notes adds hiss or hum to every project it is in.",
                      false, false,
                      [] (Context& ctx, HealthTest& t)
                      {
                          SignalSpec spec;
                          spec.type = SignalType::silence;
                          spec.sampleRate = standardRate;
                          spec.numSamples = (int) standardRate;
                          const auto result = OfflineRenderer::render (ctx.plugin, generate (spec), RenderOptions {});

                          if (! result.ok)
                              return setResult (t, Verdict::fail, "Could not render: " + result.error);

                          const auto levels = measureLevels (result.output, 0);
                          const auto peak = levels.peakDb;

                          if (peak < -90.0)
                              setResult (t, Verdict::pass, peak <= -150.0 ? juce::String ("Dead silent.") : "Quiet: peaks at " + juce::String (juce::roundToInt (peak)) + " dBFS.");
                          else if (peak < -60.0)
                              setResult (t, Verdict::warn, "Faint noise: peaks at " + juce::String (juce::roundToInt (peak)) + " dBFS with nothing in.");
                          else
                              setResult (t, Verdict::fail, "Makes sound by itself: peaks at " + juce::String (juce::roundToInt (peak)) + " dBFS with nothing in.");
                      } });

    list.push_back ({ "finite", "Never produces invalid numbers",
                      "A single NaN or infinity in a plugin's output can silence a whole mix or blow up every plugin after it.",
                      false, false,
                      [] (Context& ctx, HealthTest& t)
                      {
                          juce::StringArray problems;
                          double worst = -200.0;

                          const auto check = [&] (Probe probe, const juce::String& name)
                          {
                              const auto result = play (ctx, probe);

                              if (! result.ok)
                              {
                                  problems.add (name + ": render failed");
                                  return;
                              }

                              double peak = 0.0;

                              if (! allFinite (result.output, &peak))
                                  problems.add (name + ": produced NaN or infinity");
                              else
                                  worst = juce::jmax (worst, peak);
                          };

                          check (makeProbe (ctx, 0.5, standardRate, 512, -6.0), "noise");
                          check (makeProbe (ctx, 0.3, standardRate, 512, 0.0, 2, SignalType::impulse), "impulse");
                          check (makeProbe (ctx, 0.5, standardRate, 512, 18.0), "very hot noise");

                          if (ctx.instrument)
                          {
                              check (makeProbe (ctx, 0.5, standardRate, 512, -6.0, 2, SignalType::silence, 24), "lowest notes");
                              check (makeProbe (ctx, 0.5, standardRate, 512, -6.0, 2, SignalType::silence, 108), "highest notes");
                          }

                          if (! problems.isEmpty())
                              return setResult (t, Verdict::fail, problems.joinIntoString ("; ") + ".");

                          setResult (t, worst > 40.0 ? Verdict::warn : Verdict::pass,
                                     worst > 40.0 ? "Finite, but output reached " + juce::String (juce::roundToInt (worst)) + " dBFS on hot input."
                                                  : "Every sample was a real number, including at +18 dBFS in.");
                      } });

    list.push_back ({ "determinism", "Gives the same result every time",
                      "Run twice from a reset, a plugin should sound identical. If it does not, bounces differ from playback.",
                      false, false,
                      [] (Context& ctx, HealthTest& t)
                      {
                          auto probe = makeProbe (ctx, 0.6, standardRate, 512, -12.0);
                          const auto first = play (ctx, probe);
                          const auto second = play (ctx, probe);

                          if (! first.ok || ! second.ok)
                              return setResult (t, Verdict::fail, "Could not render twice.");

                          const auto difference = differenceDb (first.output, second.output);

                          if (difference <= -100.0)
                              setResult (t, Verdict::pass, difference <= -250.0 ? juce::String ("Bit-for-bit identical.") : "Identical to within " + juce::String (juce::roundToInt (difference)) + " dB.");
                          else
                              setResult (t, Verdict::warn, "Two identical runs differ by " + juce::String (juce::roundToInt (difference))
                                                               + " dB. Random modulation, dither or state that reset does not clear.");
                      } });

    list.push_back ({ "blocksize", "Same sound at any buffer size",
                      "Hosts pick the buffer size. A plugin whose sound changes with it has a bug that only shows on some setups.",
                      false, false,
                      [] (Context& ctx, HealthTest& t)
                      {
                          auto reference = makeProbe (ctx, 0.5, standardRate, 512, -12.0);
                          const auto base = play (ctx, reference);

                          if (! base.ok)
                              return setResult (t, Verdict::fail, "Could not render the reference.");

                          // Is it even repeatable? If not, differences between buffer sizes prove nothing.
                          const auto again = play (ctx, reference);

                          if (again.ok && differenceDb (base.output, again.output) > -60.0)
                              return setResult (t, Verdict::info, "Skipped: the output varies between identical runs, so buffer sizes cannot be compared fairly.");

                          double worst = -300.0;
                          int worstBlock = 512;

                          // The smallest buffer real hosts use is 16 or 32 samples; the rest are odd
                          // sizes that do not divide evenly into anything.
                          for (const auto block : { 16, 37, 333, 2048 })
                          {
                              auto probe = makeProbe (ctx, 0.5, standardRate, block, -12.0);
                              const auto result = play (ctx, probe);

                              if (! result.ok)
                                  return setResult (t, Verdict::fail, "Could not render with a " + juce::String (block) + "-sample buffer.");

                              const auto d = differenceDb (base.output, result.output);

                              if (d > worst) { worst = d; worstBlock = block; }
                          }

                          if (worst <= -70.0)
                              setResult (t, Verdict::pass, "Buffers of 16, 37, 333 and 2048 samples all match 512 to within " + juce::String (juce::roundToInt (worst)) + " dB.");
                          else if (worst <= -30.0)
                              setResult (t, Verdict::warn, "A " + juce::String (worstBlock) + "-sample buffer differs by " + juce::String (juce::roundToInt (worst))
                                                               + " dB. Small, but it should be zero; parameter smoothing tied to the buffer is a common cause.");
                          else
                              setResult (t, Verdict::fail, "A " + juce::String (worstBlock) + "-sample buffer sounds different (" + juce::String (juce::roundToInt (worst))
                                                               + " dB from 512). The processing depends on how the host chops up the audio.");
                      } });

    list.push_back ({ "rates", "Works at every sample rate",
                      "People record at 44.1, 48, 96 and 192 kHz. A plugin that breaks at one of them breaks those sessions.",
                      false, false,
                      [] (Context& ctx, HealthTest& t)
                      {
                          juce::StringArray unsupported, broken;
                          std::vector<double> levels;

                          for (const auto rate : { 8000.0, 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 })
                          {
                              auto probe = makeProbe (ctx, 0.3, rate, 512, -12.0, 2, SignalType::sine);
                              probe.options.sampleRate = rate;
                              const auto result = play (ctx, probe);

                              if (! result.ok)
                              {
                                  unsupported.add (juce::String (rate / 1000.0, 1) + " kHz");
                                  continue;
                              }

                              if (! allFinite (result.output))
                                  broken.add (juce::String (rate / 1000.0, 1) + " kHz");
                              else if (! ctx.instrument)
                                  levels.push_back (measureLevels (result.output, 0, (int) (rate * 0.1)).rmsDb);
                          }

                          if (! broken.isEmpty())
                              return setResult (t, Verdict::fail, "Produced invalid numbers at " + broken.joinIntoString (", ") + ".");

                          double spread = 0.0;

                          if (levels.size() >= 2)
                              spread = *std::max_element (levels.begin(), levels.end()) - *std::min_element (levels.begin(), levels.end());

                          if (! unsupported.isEmpty())
                              setResult (t, Verdict::warn, "Refused to run at " + unsupported.joinIntoString (", ") + ".");
                          else if (spread > 1.0)
                              setResult (t, Verdict::warn, "Finite everywhere, but a 1 kHz tone comes out up to " + juce::String (spread, 1) + " dB different between rates.");
                          else
                              setResult (t, Verdict::pass, "8, 22.05, 44.1, 48, 96 and 192 kHz all run" + (levels.size() >= 2 ? " with the same level within " + juce::String (spread, 1) + " dB." : juce::String (".")));
                      } });

    list.push_back ({ "latency", "Reports its latency honestly",
                      "A DAW lines tracks up using the latency a plugin reports. A wrong figure leaves that track audibly out of time.",
                      true, false,
                      [] (Context& ctx, HealthTest& t)
                      {
                          SignalSpec spec;
                          spec.type = SignalType::impulse;
                          spec.sampleRate = standardRate;
                          spec.numSamples = 16384;
                          spec.amplitudeDb = 0.0;

                          RenderOptions options;
                          options.compensateLatency = false;
                          options.tailSamples = (int) standardRate;

                          const auto result = OfflineRenderer::render (ctx.plugin, generate (spec), options);

                          if (! result.ok)
                              return setResult (t, Verdict::fail, "Could not render an impulse.");

                          const auto reported = result.reportedLatencySamples;
                          const auto* samples = result.output.getReadPointer (0);
                          int measured = 0;
                          float peak = 0.0f;

                          for (int i = 0; i < result.output.getNumSamples(); ++i)
                              if (std::abs (samples[i]) > peak)
                              {
                                  peak = std::abs (samples[i]);
                                  measured = i;
                              }

                          if (peak < 1.0e-6f)
                              return setResult (t, Verdict::info, "The impulse produced no output, so latency could not be measured.");

                          const auto tail = measureTail (result.output, 0, standardRate);

                          if (tail.ok && tail.tailMs > 250.0)
                              return setResult (t, Verdict::info, "Reports " + juce::String (reported) + " samples. Skipped the check: a long tail means the loudest point is not the delay.");

                          const auto tolerance = juce::jmax (2, reported / 10);

                          if (std::abs (measured - reported) <= tolerance)
                              setResult (t, Verdict::pass, "Reports " + juce::String (reported) + " samples; the impulse comes out at sample " + juce::String (measured) + ".");
                          else if (reported == 0)
                              setResult (t, Verdict::warn, "Reports no latency, but the impulse comes out " + juce::String (measured) + " samples late ("
                                                               + juce::String (1000.0 * measured / standardRate, 2) + " ms). A DAW will not compensate for it.");
                          else
                              setResult (t, Verdict::warn, "Reports " + juce::String (reported) + " samples, but the impulse comes out at sample " + juce::String (measured)
                                                               + ". Either the figure is wrong or a dry path bypasses the delay.");
                      } });

    list.push_back ({ "denormals", "No slowdown on near-silent input",
                      "Tiny numbers called denormals can make a plugin many times slower during quiet passages and tails.",
                      true, false,
                      [] (Context& ctx, HealthTest& t)
                      {
                          auto normal = makeProbe (ctx, 1.5, standardRate, 512, -60.0);
                          auto denormal = normal;
                          denormal.input.applyGain (1.0e-36f);   // 1e-39: inside the denormal range of a float

                          double normalTime = 1.0e9, denormalTime = 1.0e9;

                          play (ctx, normal);   // warm up

                          for (int pass = 0; pass < 2; ++pass)
                          {
                              const auto a = play (ctx, normal);
                              const auto b = play (ctx, denormal);

                              if (! a.ok || ! b.ok)
                                  return setResult (t, Verdict::fail, "Could not render.");

                              normalTime = juce::jmin (normalTime, a.renderSeconds);
                              denormalTime = juce::jmin (denormalTime, b.renderSeconds);
                          }

                          const auto ratio = denormalTime / juce::jmax (1.0e-6, normalTime);

                          if (ratio > 4.0)
                              setResult (t, Verdict::warn, "Takes " + juce::String (ratio, 1) + "x longer on near-silent input. Many hosts disable denormals, but not all do.");
                          else
                              setResult (t, Verdict::pass, "Near-silent input costs " + juce::String (ratio, 1) + "x the time of normal input.");
                      } });

    list.push_back ({ "layouts", "Channel layouts",
                      "Mono tracks, stereo buses and surround sessions each ask a plugin for a different layout.",
                      false, false,
                      [] (Context& ctx, HealthTest& t)
                      {
                          const auto supports = [&ctx] (const juce::AudioChannelSet& set)
                          {
                              auto layout = ctx.plugin.getBusesLayout();

                              for (int b = 0; b < layout.inputBuses.size(); ++b)
                                  layout.inputBuses.getReference (b) = b == 0 ? set : juce::AudioChannelSet::disabled();

                              for (int b = 0; b < layout.outputBuses.size(); ++b)
                                  layout.outputBuses.getReference (b) = b == 0 ? set : juce::AudioChannelSet::disabled();

                              return ctx.plugin.checkBusesLayoutSupported (layout);
                          };

                          juce::StringArray supported;

                          if (supports (juce::AudioChannelSet::mono()))         supported.add ("mono");
                          if (supports (juce::AudioChannelSet::stereo()))       supported.add ("stereo");
                          if (supports (juce::AudioChannelSet::create5point1())) supported.add ("5.1");
                          if (supports (juce::AudioChannelSet::create7point1())) supported.add ("7.1");

                          if (supported.isEmpty())
                              return setResult (t, Verdict::warn, "No standard layout was accepted.");

                          if (supported.contains ("mono"))
                          {
                              auto probe = makeProbe (ctx, 0.3, standardRate, 512, -12.0, 1);
                              const auto result = play (ctx, probe);

                              if (! result.ok || ! allFinite (result.output))
                                  return setResult (t, Verdict::fail, "Claims to support mono but fails when run in mono.");
                          }

                          setResult (t, Verdict::pass, "Supports " + supported.joinIntoString (", ") + (supported.contains ("mono") ? "; mono renders cleanly." : "."));
                      } });

    list.push_back ({ "tail", "Decays instead of ringing forever",
                      "A plugin whose tail never dies keeps the host busy and can build up into runaway feedback.",
                      true, false,
                      [] (Context& ctx, HealthTest& t)
                      {
                          SignalSpec spec;
                          spec.type = SignalType::impulse;
                          spec.sampleRate = standardRate;
                          spec.numSamples = 8192;
                          spec.amplitudeDb = 0.0;

                          RenderOptions options;
                          options.tailSamples = (int) (standardRate * 5.0);

                          const auto result = OfflineRenderer::render (ctx.plugin, generate (spec), options);

                          if (! result.ok)
                              return setResult (t, Verdict::fail, "Could not render an impulse.");

                          const auto tail = measureTail (result.output, 0, standardRate);

                          if (! tail.ok)
                              return setResult (t, Verdict::info, "The impulse produced no output.");

                          if (tail.truncated)
                              setResult (t, Verdict::warn, "Still ringing after 5 seconds. Self-oscillation, or feedback at or above 100%.");
                          else
                              setResult (t, Verdict::pass, tail.tailMs < 5.0 ? juce::String ("No tail: the response ends at once.")
                                                                             : "Dies away in " + juce::String (juce::roundToInt (tail.tailMs)) + " ms.");
                      } });

    list.push_back ({ "midi", "Plays notes and lets go of them",
                      "An instrument that stays silent, or never stops a note, is broken for the person playing it.",
                      false, true,
                      [] (Context& ctx, HealthTest& t)
                      {
                          const auto length = (int) standardRate;
                          Probe probe;
                          SignalSpec spec;
                          spec.type = SignalType::silence;
                          spec.sampleRate = standardRate;
                          spec.numSamples = length;
                          probe.input = generate (spec);
                          probe.options.midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
                          probe.options.midi.addEvent (juce::MidiMessage::noteOff (1, 60), length / 2);
                          probe.options.tailSamples = (int) (standardRate * 7.0);

                          const auto result = play (ctx, probe);

                          if (! result.ok)
                              return setResult (t, Verdict::fail, "Could not render: " + result.error);

                          const auto held = measureLevels (result.output, 0, 0);

                          if (held.peakDb < -80.0)
                              return setResult (t, Verdict::fail, "Played middle C and heard nothing (peak " + juce::String (juce::roundToInt (held.peakDb)) + " dBFS).");

                          // The last second, long after the key came up.
                          juce::AudioBuffer<float> end (result.output.getNumChannels(), (int) standardRate);

                          for (int c = 0; c < end.getNumChannels(); ++c)
                              end.copyFrom (c, 0, result.output, c, result.output.getNumSamples() - end.getNumSamples(), end.getNumSamples());

                          const auto after = measureLevels (end, 0, 0);

                          if (after.peakDb > -70.0)
                              setResult (t, Verdict::warn, "Still sounding " + juce::String (juce::roundToInt (after.peakDb)) + " dBFS seven seconds after the key was released. A stuck note, or a very long release.");
                          else
                              setResult (t, Verdict::pass, "Middle C sounds (peak " + juce::String (juce::roundToInt (held.peakDb)) + " dBFS) and is silent after release.");
                      } });

    list.push_back ({ "parameters", "Parameters are well formed",
                      "Duplicate or empty parameter names make automation lanes and MIDI mapping confusing for everyone.",
                      false, false,
                      [] (Context& ctx, HealthTest& t)
                      {
                          const auto& parameters = ctx.plugin.getParameters();

                          if (parameters.isEmpty())
                              return setResult (t, Verdict::info, "This plugin exposes no parameters.");

                          std::set<juce::String> seen;
                          int duplicates = 0, empty = 0, emptyText = 0, outOfRange = 0;

                          for (auto* parameter : parameters)
                          {
                              const auto name = parameter->getName (64);

                              if (name.trim().isEmpty())
                                  ++empty;
                              else if (! seen.insert (name).second)
                                  ++duplicates;

                              if (parameter->getText (0.0f, 32).isEmpty() && parameter->getText (1.0f, 32).isEmpty())
                                  ++emptyText;

                              if (parameter->getDefaultValue() < 0.0f || parameter->getDefaultValue() > 1.0f)
                                  ++outOfRange;
                          }

                          juce::StringArray problems;

                          if (duplicates)  problems.add (juce::String (duplicates) + " duplicate name" + (duplicates == 1 ? "" : "s"));
                          if (empty)       problems.add (juce::String (empty) + " with no name");
                          if (emptyText)   problems.add (juce::String (emptyText) + " that print no value");
                          if (outOfRange)  problems.add (juce::String (outOfRange) + " with a default outside its range");

                          if (problems.isEmpty())
                              setResult (t, Verdict::pass, juce::String (parameters.size()) + " parameters, all named and all printing a value.");
                          else
                              setResult (t, Verdict::warn, juce::String (parameters.size()) + " parameters: " + problems.joinIntoString (", ") + ".");
                      } });

    list.push_back ({ "state", "Saves and restores its settings",
                      "When a project reopens, the plugin must come back exactly as it was left. This is the most common real-world bug.",
                      false, false,
                      [] (Context& ctx, HealthTest& t)
                      {
                          if (ctx.plugin.getParameters().isEmpty())
                              return setResult (t, Verdict::info, "No parameters to save.");

                          juce::MessageManagerLock lock;

                          scatterParameters (ctx, 2);
                          std::vector<float> chosen;

                          for (auto* parameter : ctx.plugin.getParameters())
                              chosen.push_back (parameter->getValue());

                          juce::MemoryBlock state;
                          ctx.plugin.getStateInformation (state);

                          scatterParameters (ctx, 2);
                          ctx.plugin.setStateInformation (state.getData(), (int) state.getSize());

                          int differing = 0, counted = 0;
                          size_t index = 0;

                          for (auto* parameter : ctx.plugin.getParameters())
                          {
                              if (parameter != ctx.bypass() && ! parameter->isMetaParameter())
                              {
                                  ++counted;

                                  if (std::abs (parameter->getValue() - chosen[index]) > 0.02f)
                                      ++differing;
                              }

                              ++index;
                          }

                          if (state.getSize() == 0)
                              setResult (t, Verdict::fail, "Saved no state at all: a project would reopen with default settings.");
                          else if (differing == 0)
                              setResult (t, Verdict::pass, "Saved " + juce::File::descriptionOfSizeInBytes ((juce::int64) state.getSize()) + " of state; all "
                                                               + juce::String (counted) + " parameters came back exactly.");
                          else if (counted > 0 && differing == counted)
                              setResult (t, Verdict::fail, "None of the " + juce::String (counted) + " parameters came back. The plugin writes state but ignores it on load, so a reopened project loses every setting.");
                          else if (differing * 10 <= counted)
                              setResult (t, Verdict::warn, juce::String (differing) + " of " + juce::String (counted) + " parameters did not come back after a restore.");
                          else
                              setResult (t, Verdict::fail, juce::String (differing) + " of " + juce::String (counted) + " parameters came back wrong after a restore.");
                      } });

    list.push_back ({ "fuzz", "Survives random settings",
                      "Users twist knobs to extremes. A plugin should stay finite and sane even with every control at once on 0, 1 or anything between.",
                      false, false,
                      [] (Context& ctx, HealthTest& t)
                      {
                          if (ctx.plugin.getParameters().isEmpty())
                              return setResult (t, Verdict::info, "No parameters to vary.");

                          double worst = -200.0;
                          int bad = 0, failed = 0;
                          constexpr int rounds = 18;

                          for (int round = 0; round < rounds; ++round)
                          {
                              scatterParameters (ctx, round == 0 ? 0 : round == 1 ? 1 : 2);

                              auto probe = makeProbe (ctx, 0.4, standardRate, 512, -12.0);
                              const auto result = play (ctx, probe);

                              if (! result.ok)
                              {
                                  ++failed;
                                  continue;
                              }

                              double peak = 0.0;

                              if (! allFinite (result.output, &peak))
                                  ++bad;
                              else
                                  worst = juce::jmax (worst, peak);
                          }

                          if (bad > 0)
                              setResult (t, Verdict::fail, "Produced NaN or infinity with " + juce::String (bad) + " of " + juce::String (rounds) + " random settings.");
                          else if (failed > 0)
                              setResult (t, Verdict::warn, "Failed to render with " + juce::String (failed) + " of " + juce::String (rounds) + " random settings.");
                          else if (worst > 24.0)
                              setResult (t, Verdict::warn, "Stayed finite, but at some settings the output reached +" + juce::String (juce::roundToInt (worst)) + " dBFS from a -12 dBFS input.");
                          else
                              setResult (t, Verdict::pass, rounds == 18 ? "18 settings (all at 0, all at 1, 16 random) stayed finite; the loudest output was " + juce::String (juce::roundToInt (worst)) + " dBFS."
                                                                        : "Stayed finite.");
                      } });

    return list;
}
} // namespace

int HealthReport::count (Verdict verdict) const
{
    int n = 0;

    for (const auto& test : tests)
        n += test.verdict == verdict ? 1 : 0;

    return n;
}

juce::String HealthReport::headline() const
{
    if (count (Verdict::fail) > 0)
        return "Has problems";

    return count (Verdict::warn) > 1 ? "Mostly healthy" : "Healthy";
}

juce::String HealthReport::toText (const juce::String& pluginName) const
{
    juce::String text;
    text << "VibeCheck health report\n" << pluginName << (instrument ? " (instrument)" : "") << "\n\n"
         << headline() << ": " << count (Verdict::pass) << " passed, " << count (Verdict::warn) << " warnings, "
         << count (Verdict::fail) << " failed, " << count (Verdict::info) << " notes\n";

    for (const auto& test : tests)
    {
        const auto mark = test.verdict == Verdict::pass ? "PASS" : test.verdict == Verdict::warn ? "WARN"
                        : test.verdict == Verdict::fail ? "FAIL" : test.verdict == Verdict::info ? "INFO" : "    ";
        text << "\n[" << mark << "] " << test.title << "\n       " << test.result << "\n";
    }

    return text;
}

bool looksLikeInstrument (const juce::AudioPluginInstance& plugin)
{
    return plugin.getPluginDescription().isInstrument || (plugin.acceptsMidi() && plugin.getTotalNumInputChannels() == 0);
}

std::vector<HealthTest> plannedHealthTests (bool instrument)
{
    std::vector<HealthTest> planned;

    for (const auto& definition : definitions())
    {
        if ((definition.effectsOnly && instrument) || (definition.instrumentsOnly && ! instrument))
            continue;

        HealthTest test;
        test.id = definition.id;
        test.title = definition.title;
        test.explain = definition.explain;
        planned.push_back (std::move (test));
    }

    return planned;
}

HealthReport runHealthSuite (juce::AudioPluginInstance& plugin,
                             const std::atomic<bool>& cancel,
                             const std::function<void (int, int, const HealthTest&)>& onTest,
                             const std::function<void (const juce::String&)>& onStart)
{
    Context ctx { plugin };
    ctx.instrument = looksLikeInstrument (plugin);

    HealthReport report;
    report.instrument = ctx.instrument;

    // Remember where every parameter was, so testing leaves the plugin as it found it.
    for (auto* parameter : plugin.getParameters())
        ctx.original.push_back (parameter->getValue());

    auto planned = plannedHealthTests (ctx.instrument);
    const auto total = (int) planned.size();
    const auto started = juce::Time::getMillisecondCounterHiRes();
    int index = 0;

    for (const auto& definition : definitions())
    {
        if ((definition.effectsOnly && ctx.instrument) || (definition.instrumentsOnly && ! ctx.instrument))
            continue;

        if (cancel.load())
        {
            report.stopped = true;
            break;
        }

        if (onStart != nullptr)
            onStart (definition.title);

        HealthTest test;
        test.id = definition.id;
        test.title = definition.title;
        test.explain = definition.explain;

        const auto testStarted = juce::Time::getMillisecondCounterHiRes();

        try
        {
            definition.run (ctx, test);
        }
        catch (const std::exception& e)
        {
            setResult (test, Verdict::fail, juce::String ("Threw an exception: ") + e.what());
        }

        if (test.verdict == Verdict::pending)
            setResult (test, Verdict::info, "No result.");

        test.milliseconds = juce::Time::getMillisecondCounterHiRes() - testStarted;
        report.tests.push_back (test);

        if (onTest != nullptr)
            onTest (index, total, test);

        ++index;

        // Nothing else means anything if the plugin will not even render.
        if (definition.id == "prepare" && test.verdict == Verdict::fail)
            break;
    }

    for (size_t i = 0; i < ctx.original.size() && i < (size_t) plugin.getParameters().size(); ++i)
        plugin.getParameters()[(int) i]->setValue (ctx.original[i]);

    report.seconds = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;
    return report;
}
} // namespace vibecheck
