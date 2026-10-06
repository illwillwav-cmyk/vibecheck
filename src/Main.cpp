#include <JuceHeader.h>

#include "MainComponent.h"
#include "analysis/SelfTest.h"
#include "ui/AppIcon.h"
#include "ui/MbsLookAndFeel.h"
#include "ui/Theme.h"
#include "audio/TestSignal.h"
#include "host/ConsoleAttach.h"
#include "host/HeadlessAnalyze.h"
#include "host/HeadlessScan.h"
#include "host/HeadlessBehaviour.h"
#include "host/HeadlessHealth.h"
#include "host/HeadlessVibeCheck.h"
#include "host/PluginScanner.h"
#include "vibecheck/Export.h"
#include "vibecheck/Labels.h"
#include "vibecheck/LibrarySweep.h"
#include "vibecheck/SourceInspector.h"

#include <cstdio>
#include <iostream>
#include "host/ScanWorker.h"

namespace
{
/** Saves a PNG of the window shortly after launch. macOS screen capture needs a permission a
    build script does not have, so the app takes its own picture: useful for verifying a change
    without a human at the keyboard, and for capturing the analysis plots in later phases. */
class WindowSnapshot final : private juce::Timer
{
public:
    WindowSnapshot (juce::Component& componentToGrab,
                    juce::File destination,
                    std::function<bool()> readyToShoot,
                    std::function<void()> onFinished)
        : component (componentToGrab),
          file (std::move (destination)),
          ready (std::move (readyToShoot)),
          finished (std::move (onFinished))
    {
        startTimer (pollIntervalMs);
    }

private:
    void timerCallback() override
    {
        elapsedMs += pollIntervalMs;

        // Wait for any measurement to finish, so the picture shows a populated window rather
        // than an empty one, but never wait forever.
        if (ready != nullptr && ! ready() && elapsedMs < 120000)
            return;

        if (elapsedMs < settleMs)
            return;

        stopTimer();

        const auto image = component.createComponentSnapshot (component.getLocalBounds(), true, 2.0f);

        file.deleteFile();

        if (juce::FileOutputStream stream (file); stream.openedOk())
        {
            juce::PNGImageFormat png;
            png.writeImageToStream (image, stream);
        }

        finished();
    }

    static constexpr int pollIntervalMs = 250;
    static constexpr int settleMs = 1500;

    juce::Component& component;
    juce::File file;
    std::function<bool()> ready;
    std::function<void()> finished;
    int elapsedMs = 0;
};

/** Maps a --signal= name onto a stimulus. */
vibecheck::SignalType signalTypeFromName (const juce::String& name)
{
    for (const auto type : { vibecheck::SignalType::impulse,
                             vibecheck::SignalType::sine,
                             vibecheck::SignalType::dualSine,
                             vibecheck::SignalType::ramp,
                             vibecheck::SignalType::whiteNoise,
                             vibecheck::SignalType::silence })
        if (vibecheck::toString (type).replace (" ", "").equalsIgnoreCase (name.replace (" ", "")))
            return type;

    return vibecheck::SignalType::sine;
}

/** Returns the value of a --switch=value argument, or an empty string if it is absent.

    Reads the argument array rather than the joined command-line string: the joined string loses
    argument boundaries, so a value containing a space (a plugin called "MB EQ", say) would be
    truncated at the space. */
juce::String getSwitchValue (const juce::String& prefix)
{
    for (const auto& token : juce::JUCEApplication::getCommandLineParameterArray())
        if (token.startsWith (prefix))
            return token.fromFirstOccurrenceOf (prefix, false, false).unquoted();

    return {};
}
} // namespace

class VibeCheckApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    /** --merge=a.json,b.json,folder --out=master.json (or master.csv). Returns the exit code. */
    static int runMerge (const juce::String& inputs, const juce::String& out)
    {
        const auto files = vibecheck::exportFilesFrom (inputs);

        if (files.isEmpty())
        {
            std::cout << "no export files found in \"" << inputs << "\"" << std::endl;
            return 2;
        }

        const auto merged = vibecheck::mergeExports (files);

        for (const auto& problem : merged.problems)
            std::cout << "warning: " << problem << std::endl;

        std::cout << "read " << merged.filesRead << " of " << files.size() << " files: "
                  << merged.pluginsSeen << " rows became " << merged.pluginsMerged << " plugins" << std::endl;

        if (merged.filesRead == 0)
            return 1;

        const auto destination = juce::File::getCurrentWorkingDirectory().getChildFile (out.isNotEmpty() ? out : "master.json");
        const auto text = destination.hasFileExtension ("csv") ? vibecheck::masterToCsv (merged.master)
                                                                : juce::JSON::toString (merged.master, false);

        if (! destination.replaceWithText (text))
        {
            std::cout << "could not write " << destination.getFullPathName() << std::endl;
            return 1;
        }

        std::cout << "wrote " << destination.getFullPathName() << std::endl;
        return 0;
    }

    void initialise (const juce::String& commandLine) override
    {
        vibecheck::attachConsoleForCommandLine (commandLine);

       #if ! VIBECHECK_AI_CHECK
        // AI Check is switched off in this build, and so are the switches that belong to it.
        for (const auto* flag : { "--vibecheck=", "--vibedemo=", "--behaviour=", "--sourcecheck=", "--export=", "--merge=",
                                  "--evaluate", "--label-ai=", "--label-human=", "--label-clear=" })
            if (commandLine.contains (flag))
            {
                std::cout << "AI Check is switched off in this version of VibeCheck, so " << flag << " is not available." << std::endl;
                setApplicationReturnValue (2);
                quit();
                return;
            }
       #endif

        // The app relaunches its own binary to scan plugins. In that case there is no UI:
        // this process exists only to load one plugin at a time and report what it found.
        if (auto worker = createScanWorkerIfRequested (commandLine))
        {
            scanWorker = std::move (worker);
            return;
        }

        // Draws the app icon and the installer's artwork, then exits. Run by installer/make_assets.sh.
        if (const auto folder = getSwitchValue ("--render-assets="); folder.isNotEmpty())
        {
            setApplicationReturnValue (mbs::writeInstallerAssets (juce::File (folder)) ? 0 : 1);
            quit();
            return;
        }

        // Folds exports from several people into one master list. Needs no plugins and no window.
        if (const auto inputs = getSwitchValue ("--merge="); inputs.isNotEmpty())
        {
            setApplicationReturnValue (runMerge (inputs, getSwitchValue ("--out=")));
            quit();
            return;
        }

        // Measures the scores against labelled plugins in a master list or an export.
        if (const auto file = getSwitchValue ("--evaluate="); file.isNotEmpty())
        {
            const auto parsed = juce::JSON::parse (juce::File::getCurrentWorkingDirectory().getChildFile (file));

            if (! parsed["plugins"].isArray())
            {
                std::cout << "\"" << file << "\" is not a VibeCheck export or master list" << std::endl;
                setApplicationReturnValue (2);
            }
            else
            {
                std::cout << vibecheck::evaluateLabels (parsed["plugins"]) << std::endl;
            }

            quit();
            return;
        }

        // Reads a plugin's source code: a folder, or the address of a public GitHub repository.
        if (const auto target = getSwitchValue ("--sourcecheck="); target.isNotEmpty())
        {
            std::atomic<bool> never { false };
            const auto repository = vibecheck::parseRepository (target);
            const auto report = repository.isValid()
                                    ? vibecheck::inspectRepository (repository, never, [] (const juce::String& text) { std::cout << text << std::endl; })
                                    : vibecheck::inspectSourceFolder (juce::File::getCurrentWorkingDirectory().getChildFile (target));
            std::cout << report.toText() << std::endl;
            setApplicationReturnValue (report.ok ? 0 : 1);
            quit();
            return;
        }

        if (commandLine.contains ("--selftest"))
        {
            setApplicationReturnValue (vibecheck::runSelfTest());
            quit();
            return;
        }

        pluginScanner = std::make_unique<PluginScanner>();

        // The last theme is remembered between launches; --theme= overrides it for one run, which
        // the screenshot scripts use.
        if (const auto themeSwitch = getSwitchValue ("--theme="); themeSwitch.isNotEmpty())
            mbs::setDarkTheme (! themeSwitch.equalsIgnoreCase ("light"));
        else
            mbs::setDarkTheme (mbs::loadThemePreference (pluginScanner->getSettings(), true));

        lookAndFeel.refreshColours();
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        // Headless scan, for verifying the hosting foundation from a script or from CI.
        if (commandLine.contains ("--scan"))
        {
            headlessScan = std::make_unique<HeadlessScan> (*pluginScanner, [] { quit(); });
            headlessScan->start();
            return;
        }

        if (const auto target = getSwitchValue ("--vibecheck="); target.isNotEmpty())
        {
            headlessVibeCheck = std::make_unique<HeadlessVibeCheck> (*pluginScanner, target, [] { quit(); });
            headlessVibeCheck->start();
            return;
        }

        if (const auto target = getSwitchValue ("--health="); target.isNotEmpty())
        {
            headlessHealth = std::make_unique<HeadlessHealth> (*pluginScanner, target, [this] (int code)
                                                               {
                                                                   setApplicationReturnValue (code);
                                                                   quit();
                                                               });
            headlessHealth->start();
            return;
        }

        // Records what you know about a plugin: --label-ai="Name", --label-human="Name", --label-clear="Name".
        // Every installed build of that plugin (AudioUnit and VST3) shares the label.
        for (const auto& [flag, label] : { std::pair<const char*, vibecheck::Label> { "--label-ai=", vibecheck::Label::vibeCoded },
                                           { "--label-human=", vibecheck::Label::handWritten },
                                           { "--label-clear=", vibecheck::Label::none } })
            if (const auto name = getSwitchValue (flag); name.isNotEmpty())
            {
                int changed = 0;

                for (const auto& type : pluginScanner->getKnownPluginList().getTypes())
                    if (type.name.equalsIgnoreCase (name) || (name.endsWith ("*") && type.name.startsWithIgnoreCase (name.dropLastCharacters (1)))
                        || (name.startsWithIgnoreCase ("maker:") && type.manufacturerName.equalsIgnoreCase (name.substring (6))))
                    {
                        vibecheck::setLabel (pluginScanner->getSettings(), type, label);
                        ++changed;
                    }

                std::cout << (changed > 0 ? juce::String (changed) + " installed build" + (changed == 1 ? "" : "s") + " now: " + vibecheck::describe (label)
                                          : "nothing installed is called \"" + name + "\"") << std::endl;
                setApplicationReturnValue (changed > 0 ? 0 : 2);
                quit();
                return;
            }

        // Weighs the library and measures the scores against the plugins you have labelled.
        if (commandLine.contains ("--evaluate"))
        {
            const auto types = pluginScanner->getKnownPluginList().getTypes();
            headlessSweep = std::make_unique<vibecheck::LibrarySweep>();
            headlessSweep->start (types, pluginScanner->getSettings(), {},
                                  [this] (std::vector<vibecheck::SweepEntry> results)
                                  {
                                      const auto exported = vibecheck::buildExport (results, getApplicationVersion(),
                                                                                    vibecheck::allLabels (pluginScanner->getSettings()));
                                      std::cout << vibecheck::evaluateLabels (exported["plugins"]) << std::endl;
                                      quit();
                                  });
            return;
        }

        // Weighs the whole library and writes the shareable export, for scripts and for friends who
        // would rather not click: `VibeCheck --export=~/Desktop/mine.json`.
        if (const auto destination = getSwitchValue ("--export="); destination.isNotEmpty())
        {
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (destination);
            const auto types = pluginScanner->getKnownPluginList().getTypes();

            if (types.isEmpty())
            {
                std::cout << "no plugins known yet - run --scan first" << std::endl;
                setApplicationReturnValue (2);
                quit();
                return;
            }

            headlessSweep = std::make_unique<vibecheck::LibrarySweep>();
            headlessSweep->start (types, pluginScanner->getSettings(), {},
                                  [this, file] (std::vector<vibecheck::SweepEntry> results)
                                  {
                                      const auto written = vibecheck::writeExport (file, results, getApplicationVersion(),
                                                                                   vibecheck::allLabels (pluginScanner->getSettings()));
                                      std::cout << (written.wasOk() ? "wrote " + juce::String ((int) results.size()) + " plugins to " + file.getFullPathName()
                                                                    : written.getErrorMessage()) << std::endl;
                                      setApplicationReturnValue (written.wasOk() ? 0 : 1);
                                      quit();
                                  });
            return;
        }

        if (const auto target = getSwitchValue ("--behaviour="); target.isNotEmpty())
        {
            headlessBehaviour = std::make_unique<HeadlessBehaviour> (*pluginScanner, target, [this] (int code)
                                                                     {
                                                                         setApplicationReturnValue (code);
                                                                         quit();
                                                                     });
            headlessBehaviour->start();
            return;
        }

        if (const auto target = getSwitchValue ("--analyze="); target.isNotEmpty())
        {
            headlessAnalyze = std::make_unique<HeadlessAnalyze> (*pluginScanner, target, [] { quit(); });
            headlessAnalyze->start();
            return;
        }

        mainWindow = std::make_unique<MainWindow> (getApplicationName() + " v" + getApplicationVersion(),
                                                   *pluginScanner);

        auto* main = dynamic_cast<MainComponent*> (mainWindow->getContentComponent());
        const auto driven = getSwitchValue ("--demo=").isNotEmpty()
                            || getSwitchValue ("--vibedemo=").isNotEmpty()
                            || getSwitchValue ("--comparedemo=").isNotEmpty()
                            || getSwitchValue ("--perfdemo=").isNotEmpty()
                            || getSwitchValue ("--healthdemo=").isNotEmpty()
                            || getSwitchValue ("--tab=").isNotEmpty();

        if (main != nullptr && driven && ! commandLine.contains ("--splash"))
            main->skipSplash();

        if (const auto tab = getSwitchValue ("--tab="); tab.isNotEmpty())
            if (auto* tabTarget = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                tabTarget->selectTab (tab);

        auto* content = dynamic_cast<MainComponent*> (mainWindow->getContentComponent());

        if (const auto demo = getSwitchValue ("--demo="); demo.isNotEmpty() && content != nullptr)
        {
            content->getAnalysisTab().setSweeps (commandLine.contains ("--sweeps"));
            content->selectTab ("Analysis");
            content->getAnalysisTab().runDemo (demo, signalTypeFromName (getSwitchValue ("--signal=")));
        }

        if (const auto shot = getSwitchValue ("--paramshot="); shot.isNotEmpty() && content != nullptr)
            juce::Timer::callAfterDelay (5000, [content, shot]
            {
                content->getAnalysisTab().openParametersAndSnapshot (juce::File (shot), [] { quit(); });
            });

        if (const auto pair = getSwitchValue ("--comparedemo="); pair.isNotEmpty() && content != nullptr)
        {
            content->selectTab ("Compare");
            content->getComparisonTab().runDemo (pair.upToFirstOccurrenceOf ("|", false, false),
                                                 pair.fromFirstOccurrenceOf ("|", false, false));
        }

        if (const auto name = getSwitchValue ("--healthdemo="); name.isNotEmpty() && content != nullptr)
        {
            content->selectTab ("Health");
            content->getHealthTab().runDemo (name);
        }

        if (const auto name = getSwitchValue ("--perfdemo="); name.isNotEmpty() && content != nullptr)
        {
            content->selectTab ("Profiler");
            content->getPerformanceTab().runDemo (name);
        }

        if (const auto target = getSwitchValue ("--vibedemo="); target.isNotEmpty() && content != nullptr)
        {
            content->selectTab ("Vibe Check");
            if (const auto source = getSwitchValue ("--source="); source.isNotEmpty())
                content->getVibeCheckTab().runSourceDemo (target, source);
            else
                content->getVibeCheckTab().runDemo (target, commandLine.contains ("--deep"));
        }

        if (const auto path = getSwitchValue ("--snapshot="); path.isNotEmpty())
            snapshot = std::make_unique<WindowSnapshot> (*mainWindow->getContentComponent(),
                                                         juce::File (path),
                                                         [content] { return content == nullptr || content->isIdle(); },
                                                         [] { quit(); });
    }

    void shutdown() override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        snapshot.reset();
        mainWindow.reset();
        headlessScan.reset();
        headlessAnalyze.reset();
        headlessVibeCheck.reset();
        headlessHealth.reset();
        pluginScanner.reset();
        scanWorker.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, PluginScanner& scanner)
            : DocumentWindow (name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                  .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (scanner), true);
            setResizable (true, false);
            setResizeLimits (1000, 680, 10000, 10000);
            centreWithSize (1320, 840);
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    MbsLookAndFeel lookAndFeel;
    std::unique_ptr<PluginScanner> pluginScanner;
    std::unique_ptr<HeadlessScan> headlessScan;
    std::unique_ptr<HeadlessAnalyze> headlessAnalyze;
    std::unique_ptr<HeadlessVibeCheck> headlessVibeCheck;
    std::unique_ptr<HeadlessHealth> headlessHealth;
    std::unique_ptr<HeadlessBehaviour> headlessBehaviour;
    std::unique_ptr<vibecheck::LibrarySweep> headlessSweep;
    std::unique_ptr<WindowSnapshot> snapshot;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<juce::ChildProcessWorker> scanWorker;
};

START_JUCE_APPLICATION (VibeCheckApplication)
