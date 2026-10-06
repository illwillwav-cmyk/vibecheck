#include "MainComponent.h"

#include "ui/AppIcon.h"
#include "ui/BrandAssets.h"
#include "ui/MbsLookAndFeel.h"
#include "ui/Theme.h"
#include "vibecheck/UpdateCheck.h"

namespace
{
/** Opens the manual that ships with the app: inside the bundle on a Mac, beside the program on
    Windows. A build without one (run straight from the build folder, say) falls back to the copy
    published with the latest release. */
void openManual()
{
    const auto app = juce::File::getSpecialLocation (juce::File::currentApplicationFile);

   #if JUCE_MAC
    const auto manual = app.getChildFile ("Contents/Resources/VibeCheck Manual.pdf");
   #else
    const auto manual = app.getSiblingFile ("VibeCheck Manual.pdf");
   #endif

    if (manual.existsAsFile())
    {
        manual.startAsProcess();
        return;
    }

    if (const auto address = vibecheck::updateUrl(); address.endsWith ("latest.json"))
    {
        juce::URL (address.upToLastOccurrenceOf ("latest.json", false, false) + "VibeCheck-Manual.pdf").launchInDefaultBrowser();
        return;
    }

    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Manual not found",
                                            "The manual is installed with VibeCheck. This copy was started without one.");
}

/** "⌘3" on a Mac, "Ctrl+3" elsewhere. */
juce::String shortcutLabel (int number)
{
   #if JUCE_MAC
    return juce::String (juce::CharPointer_UTF8 ("\xE2\x8C\x98")) + juce::String (number);
   #else
    return "Ctrl+" + juce::String (number);
   #endif
}

constexpr int railWidth = 232;
constexpr int headerHeight = 84;

struct PageInfo
{
    const char* name;
    const char* title;
    const char* subtitle;
    mbs::Icon icon;
};

const PageInfo pages[] = {
    { "Plugin Manager",       "Plugin Manager",       "Everything installed on this Mac, scanned safely out of process.",        mbs::Icon::library },
    { "Graph Analyzer",       "Graph Analyzer",       "Push test signals through a plugin and measure what comes back.",         mbs::Icon::analyzer },
    { "Plugin Health",        "Plugin Health",        "Robustness checks: valid output, any buffer size, honest latency, saved settings.", mbs::Icon::check },
    { "AI Check",             "AI Check",             "Read each plugin's binary for fingerprints of machine-generated code.",   mbs::Icon::sparkle },
    { "A/B Compare",          "A/B Compare",          "Lay two plugins over the same measurements.",                             mbs::Icon::compare },
    { "Performance Profiler", "Performance Profiler", "Benchmark CPU load, latency and memory across buffer sizes.",             mbs::Icon::gauge },
};

constexpr int aiCheckPage = 3;

/** AI Check is built in only when asked for; see the option in CMakeLists.txt. */
constexpr bool aiCheckEnabled = VIBECHECK_AI_CHECK != 0;
} // namespace

/** Sun or moon, whichever theme a click would switch to. */
class MainComponent::ThemeToggle final : public juce::Button
{
public:
    ThemeToggle() : juce::Button ("Theme")
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("Switch between dark and light");
    }

    void paintButton (juce::Graphics& g, bool isOver, bool isDown) override
    {
        const auto& p = mbs::theme();
        const auto area = getLocalBounds().toFloat().reduced (0.5f);

        g.setColour (isDown ? p.cardHi.brighter (0.1f) : p.card);
        g.fillRoundedRectangle (area, 10.0f);
        g.setColour (isOver ? p.inkFaint : p.line);
        g.drawRoundedRectangle (area, 10.0f, 1.0f);

        mbs::drawIcon (g, p.isDark ? mbs::Icon::sun : mbs::Icon::moon,
                       area.withSizeKeepingCentre (16.0f, 16.0f), isOver ? p.ink : p.inkMuted, 1.9f);
    }
};

MainComponent::MainComponent (PluginScanner& scanner)
    : pluginScanner (scanner),
      pluginManagerTab (scanner),
      analysisTab (scanner),
      healthTab (scanner),
      vibeCheckTab (scanner),
      comparisonTab (scanner),
      performanceTab (scanner)
{
    setWantsKeyboardFocus (true);

    for (int page = 0; page < (int) std::size (pages); ++page)
        if (page != aiCheckPage || aiCheckEnabled)
            navPages.push_back (page);

    nav.resize (navPages.size());

    for (int i = 0; i < (int) nav.size(); ++i)
    {
        const auto& info = pages[navPages[(size_t) i]];
        nav[(size_t) i] = std::make_unique<mbs::NavButton> (info.name, info.icon, shortcutLabel (i + 1));
        nav[(size_t) i]->setButtonText (info.name);
        nav[(size_t) i]->onClick = [this, i] { setView (i); };
        addAndMakeVisible (*nav[(size_t) i]);
        addChildComponent (pageAt (i));
    }

    themeToggle = std::make_unique<ThemeToggle>();
    themeToggle->onClick = [this] { toggleTheme(); };
    addAndMakeVisible (*themeToggle);

    // A quiet notice at the foot of the rail when a newer version has been published. It appears only
    // if this build knows where to look, and the check is one small request made once at launch.
    mbs::setButtonStyle (manualButton, mbs::ButtonStyle::outline);
    manualButton.setTooltip ("Open the VibeCheck manual");
    manualButton.onClick = [] { openManual(); };
    addAndMakeVisible (manualButton);

    mbs::setButtonStyle (updateNotice, mbs::ButtonStyle::primary);
    updateNotice.setVisible (false);
    updateNotice.onClick = [this] { if (updatePage.isNotEmpty()) juce::URL (updatePage).launchInDefaultBrowser(); };
    addChildComponent (updateNotice);

    // --update-url= overrides the built-in address, for trying the notice; --no-update-check turns it off.
    auto address = vibecheck::updateUrl();

    for (const auto& argument : juce::JUCEApplicationBase::getCommandLineParameterArray())
        if (argument.startsWith ("--update-url="))
            address = argument.fromFirstOccurrenceOf ("=", false, false);

    if (address.isNotEmpty() && ! juce::JUCEApplicationBase::getCommandLineParameterArray().contains ("--no-update-check"))
        juce::Thread::launch ([address, safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            const auto info = vibecheck::checkForUpdate (address, ProjectInfo::versionString);

            if (! info.available)
                return;

            juce::MessageManager::callAsync ([safe, info]
            {
                if (safe == nullptr)
                    return;

                safe->updatePage = info.page;
                safe->updateNotice.setButtonText ("Update " + info.version);
                safe->updateNotice.setTooltip (info.notes.isNotEmpty() ? info.notes : "A newer version of VibeCheck is available");
                safe->updateNotice.setVisible (true);
                safe->resized();
            });
        });

    setView (0);

    // Look for newly installed plugins at every launch. Anything already known is skipped, so
    // this is quick, and it means a plugin installed since last time is simply there.
    scanning = true;
    startTimerHz (30);

    libraryScan.start (scanner,
                       [safe = juce::Component::SafePointer<MainComponent> (this)] (const vibecheck::ScanProgress& update)
                       {
                           if (safe == nullptr)
                               return;

                           safe->scanText = "Scanning " + update.formatName + ": " + update.pluginName;
                           safe->repaint (safe->chipBounds);
                       },
                       [safe = juce::Component::SafePointer<MainComponent> (this)] (int added)
                       {
                           if (safe == nullptr)
                               return;

                           safe->scanning = false;
                           safe->scanText = added > 0 ? juce::String (added) + (added == 1 ? " new plugin found" : " new plugins found")
                                                      : juce::String ("Library up to date");
                           safe->repaint (safe->chipBounds);

                           // The plugin menus follow the library on their own; only the sweep needs a nudge.
                           if (added > 0 && aiCheckEnabled)
                               safe->vibeCheckTab.sweepLibrary();
                       });

    splash = std::make_unique<VibeSplashScreen> ([this]
    {
        splash.reset();
        resized();
    });
    addAndMakeVisible (*splash);
    splash->grabKeyboardFocus();
}

MainComponent::~MainComponent()
{
    stopTimer();
}

juce::Component* MainComponent::pageAt (int index)
{
    switch (navPages[(size_t) juce::jlimit (0, (int) navPages.size() - 1, index)])
    {
        case 0:  return &pluginManagerTab;
        case 1:  return &analysisTab;
        case 2:  return &healthTab;
        case 3:  return &vibeCheckTab;
        case 4:  return &comparisonTab;
        default: return &performanceTab;
    }
}

void MainComponent::timerCallback()
{
    pulse += 0.12f;

    // Once the scan is over, the chip stays; only the dot stops moving.
    repaint (chipBounds.expanded (4));

    if (! scanning && pulse > 6.4f)
        stopTimer();
}

void MainComponent::skipSplash()
{
    if (splash != nullptr)
    {
        splash->setVisible (false);
        splash.reset();
    }
}

void MainComponent::toggleTheme()
{
    mbs::setDarkTheme (! mbs::isDarkTheme());
    mbs::saveThemePreference (pluginScanner.getSettings());

    if (auto* look = dynamic_cast<MbsLookAndFeel*> (&getLookAndFeel()))
        look->refreshColours();

    if (auto* top = getTopLevelComponent())
    {
        top->sendLookAndFeelChange();
        top->repaint();
    }

    pulse = 0.0f;
    startTimerHz (30);
}

void MainComponent::setView (int index)
{
    index = juce::jlimit (0, (int) nav.size() - 1, index);

    if (index == currentIndex)
        return;

    const auto first = currentIndex < 0;

    if (currentIndex >= 0)
        pageAt (currentIndex)->setVisible (false);

    currentIndex = index;

    for (int i = 0; i < (int) nav.size(); ++i)
        nav[(size_t) i]->setSelected (i == index);

    auto* page = pageAt (index);
    page->setBounds (pageBounds);

    if (first || splash != nullptr)
        page->setVisible (true);
    else
        juce::Desktop::getInstance().getAnimator().fadeIn (page, 160);

    repaint();
}

void MainComponent::selectTab (const juce::String& name)
{
    // Which page is meant, by its name or by a shorter word for it.
    int wanted = -1;

    for (int page = 0; page < (int) std::size (pages); ++page)
        if (juce::String (pages[page].name).containsIgnoreCase (name) || name.containsIgnoreCase (pages[page].name))
            wanted = page;

    if (wanted < 0)
    {
        if (name.containsIgnoreCase ("Scan"))        wanted = 0;
        else if (name.containsIgnoreCase ("Analy"))  wanted = 1;
        else if (name.containsIgnoreCase ("Health")) wanted = 2;
        else if (name.containsIgnoreCase ("AI") || name.containsIgnoreCase ("Vibe")) wanted = aiCheckPage;
        else if (name.containsIgnoreCase ("Compar") || name.containsIgnoreCase ("A/B")) wanted = 4;
        else if (name.containsIgnoreCase ("Perf") || name.containsIgnoreCase ("Profil")) wanted = 5;
    }

    // A page that is switched off has no button, so asking for it does nothing.
    for (int i = 0; i < (int) navPages.size(); ++i)
        if (navPages[(size_t) i] == wanted)
            setView (i);
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown())
    {
        const auto c = key.getTextCharacter();

        if (c >= '1' && c < '1' + (juce::juce_wchar) nav.size())
        {
            setView (c - '1');
            return true;
        }
    }

    return false;
}

void MainComponent::paint (juce::Graphics& g)
{
    const auto& p = mbs::theme();
    g.fillAll (p.paper);

    // --- Rail ----------------------------------------------------------------------------------
    g.setColour (p.sidebar);
    g.fillRect (railBounds);
    g.setColour (p.line);
    g.fillRect (railBounds.getRight() - 1, 0, 1, getHeight());

    auto brand = railBounds.withHeight (84).reduced (22, 0).withTrimmedTop (4);
    const auto tile = brand.removeFromLeft (38).withSizeKeepingCentre (38, 38).toFloat();

    // The mark sits in a small tile, the same shape the app icon has.
    g.setGradientFill (juce::ColourGradient (p.cardHi.brighter (p.isDark ? 0.05f : 0.0f), tile.getX(), tile.getY(), p.card, tile.getRight(), tile.getBottom(), false));
    g.fillRoundedRectangle (tile, 10.0f);
    g.setColour (p.line);
    g.drawRoundedRectangle (tile.reduced (0.5f), 10.0f, 1.0f);
    mbs::drawLogoMark (g, tile.reduced (6.0f, 6.0f), p.accent, p.accent2);

    brand.removeFromLeft (12);
    g.setColour (p.ink);
    g.setFont (mbs::brandFont (18.0f, true));
    g.drawText ("VibeCheck", brand.removeFromTop (brand.getHeight() / 2 + 6).withTrimmedTop (6), juce::Justification::bottomLeft);
    g.setColour (p.inkFaint);
    g.setFont (mbs::monoFont (10.5f));
    g.drawText ("v" + juce::String (ProjectInfo::versionString), brand, juce::Justification::topLeft);

    g.setColour (p.inkFaint);
    g.setFont (mbs::brandFont (10.0f, true));
    mbs::drawTracked (g, "Workspace", juce::Rectangle<int> (railBounds.getX() + 24, 92, railWidth - 48, 18), 1.6f,
                      juce::Justification::centredLeft);

    // Foot of the rail: the studio mark.
    if (const auto mark = mbs::wordmark(); mark.isValid())
    {
        const auto w = 112;
        const auto h = juce::roundToInt ((float) w * (float) mark.getHeight() / (float) mark.getWidth());
        g.setOpacity (0.55f);
        g.drawImage (mark, juce::Rectangle<int> (railBounds.getX() + 24, railBounds.getBottom() - h - 24, w, h).toFloat(),
                     juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid);
        g.setOpacity (1.0f);
    }

    // --- Header --------------------------------------------------------------------------------
    if (currentIndex >= 0)
    {
        auto head = headerBounds.reduced (mbs::pagePadding, 0).withTrimmedTop (4);

        g.setColour (p.ink);
        g.setFont (mbs::brandFont (24.0f, true));
        g.drawText (pages[navPages[(size_t) currentIndex]].title, head.removeFromTop (head.getHeight() / 2 + 6), juce::Justification::bottomLeft);

        g.setColour (p.inkMuted);
        g.setFont (mbs::brandFont (13.5f));
        g.drawText (pages[navPages[(size_t) currentIndex]].subtitle, head.withTrimmedRight (chipBounds.getWidth() + 80), juce::Justification::topLeft, true);
    }

    // The scan chip: a dot that breathes while the library is being checked.
    if (! chipBounds.isEmpty())
    {
        const auto chip = chipBounds.toFloat();
        g.setColour (p.card);
        g.fillRoundedRectangle (chip, chip.getHeight() * 0.5f);
        g.setColour (p.line);
        g.drawRoundedRectangle (chip.reduced (0.5f), chip.getHeight() * 0.5f, 1.0f);

        const auto dotColour = scanning ? p.accent : p.good;
        const auto dot = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ chip.getX() + 16.0f, chip.getCentreY() });

        if (scanning)
        {
            const auto ring = 0.5f + 0.5f * std::sin (pulse);
            g.setColour (dotColour.withAlpha (0.35f * (1.0f - ring)));
            g.fillEllipse (dot.expanded (2.0f + 5.0f * ring));
        }

        g.setColour (dotColour);
        g.fillEllipse (dot);

        g.setColour (p.inkMuted);
        g.setFont (mbs::brandFont (12.0f));
        g.drawText (scanText, chipBounds.withTrimmedLeft (30).withTrimmedRight (12), juce::Justification::centredLeft, true);
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    railBounds = area.removeFromLeft (railWidth);
    headerBounds = area.removeFromTop (headerHeight);
    pageBounds = area.reduced (mbs::pagePadding, 0).withTrimmedBottom (mbs::pagePadding);

    auto navArea = railBounds.withTrimmedTop (114).reduced (0, 0);

    for (auto& button : nav)
        button->setBounds (navArea.removeFromTop (44));

    // Above the studio mark at the foot of the rail.
    manualButton.setBounds (railBounds.getX() + 22, railBounds.getBottom() - 98, railWidth - 44, 34);
    updateNotice.setBounds (railBounds.getX() + 22, railBounds.getBottom() - 140, railWidth - 44, 34);

    auto head = headerBounds.reduced (mbs::pagePadding, 0).withTrimmedTop (4);
    themeToggle->setBounds (head.removeFromRight (38).withSizeKeepingCentre (38, 38));
    head.removeFromRight (10);

    chipBounds = head.removeFromRight (290).withSizeKeepingCentre (290, 32);

    if (currentIndex >= 0)
        pageAt (currentIndex)->setBounds (pageBounds);

    if (splash != nullptr)
        splash->setBounds (getLocalBounds());
}
