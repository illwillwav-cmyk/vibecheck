# VibeCheck

A standalone plugin host that does two things to an audio plugin: measures its DSP the way
Plugin Doctor does, and inspects its binary for signs that it was written by an AI.

Six pages: a plugin manager, a graph analyzer, a plugin health check, an AI check, an A/B compare
and a performance profiler (Command+1 to Command+6). Measurements run offline, the AI check only
reads files, and there is an installer package and a disk image (see Installing).

- **Graph Analyzer** measures distortion, frequency and phase response, dynamics, latency, noise,
  aliasing and tail. A Parameters window sets any control before measuring; instruments are played
  with MIDI instead of fed audio; an optional sweep plots distortion against level and frequency;
  results copy as text or save as an image.
- **Plugin Health** is a robustness check in the spirit of pluginval, in plain language (below).
- **AI Check** reads each plugin file for fingerprints of generated code.
- **A/B Compare** puts two plugins through the same measurements side by side.
- **Performance Profiler** is a guided three-step page: choose a plugin, describe your session
  (buffer size, sample rate, copies), and read how heavy it is, with a clickable heat map of every
  setup and how many copies fit on a core.

## Build

Requires CMake 3.22+ and Xcode (or its command line tools) to build. The built app needs neither:
it reads plugin binaries itself rather than calling `nm`, `otool` or `strings`. JUCE is fetched automatically by CPM on the
first configure; nothing is installed globally.

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
open build/VibeCheck_artefacts/RelWithDebInfo/VibeCheck.app
```

Currently targets macOS (Apple silicon, or universal with `UNIVERSAL=1` below) with VST3 and AU. The CMake and host layers are arranged so
Windows and CLAP can be added without restructuring.

## Download

Releases are built by GitHub Actions and published on the repository's Releases page, with the
Windows and Mac installers under fixed file names. `docs/index.html` is a download page that shows
the right button for each visitor (turn on GitHub Pages for the `/docs` folder). A running copy of
the app checks once at launch for a newer version by reading a small `latest.json` from the latest
release, and shows an "Update" button if there is one; it sends nothing about the user, and
`--no-update-check` disables it. To publish a version run `installer/release.sh 0.6.0`.
[docs/RELEASING.md](docs/RELEASING.md) has the one-time setup.

## Installing

`installer/build_installer.sh` builds the app and packages it two ways:

```
installer/build_installer.sh
# dist/VibeCheck-<version>.pkg    guided installer: welcome, read me, optional command line tool
# dist/VibeCheck-<version>.dmg    drag-to-Applications disk image, with an uninstaller beside the app
```

With no options it makes an ad hoc signed build that runs on the Mac that built it. To produce
something other people can open without a Gatekeeper warning, give it your Apple developer
identities and a notarytool keychain profile:

```
SIGN_APP="Developer ID Application: Your Name (TEAMID)" \
SIGN_PKG="Developer ID Installer: Your Name (TEAMID)" \
NOTARY_PROFILE="my-notary-profile" \
UNIVERSAL=1 \
installer/build_installer.sh
```

The app is signed with the hardened runtime and `installer/VibeCheck.entitlements`. Those
entitlements matter: a plugin host loads code signed by other developers, and the hardened runtime
refuses all of it unless library validation is switched off.

The installer always puts the app in `/Applications` (it is not relocatable, so it never updates a
stray copy elsewhere), offers a `vibecheck` command in `/usr/local/bin`, and registers the app
with Launch Services. `installer/uninstall.sh` removes the app, the command, the receipts and,
with `--purge`, the settings; the disk image carries a double-clickable copy.

The icon and the disk image backdrop are drawn by the app itself from `assets/brand`, so they
cannot drift from the brand files: after changing `src/ui/AppIcon.cpp`, run
`installer/make_assets.sh` and commit the results in `assets/icon`.

Not covered by an automated check: arranging the disk image window needs permission to automate
Finder. If that is refused the script says so and the image is still valid, with the icons in
Finder's default places.

## Windows

VibeCheck builds for Windows 10/11 (64-bit) with MSVC. It hosts VST3 plugins; AudioUnit is a Mac
format. The installer is made with [Inno Setup](https://jrsoftware.org/isinfo.php):

```
installer\windows\build_windows.ps1
# dist\VibeCheck-<version>-Windows-Setup.exe   installer: per-user by default, optional desktop
#                                              shortcut, optional `vibecheck` command on PATH
# dist\VibeCheck-<version>-Windows-x64.zip     portable folder, no install needed
# dist\VibeCheck-<version>-Windows.sha256      checksums
```

It needs Visual Studio 2022 with the C++ workload, CMake and Inno Setup 6.3 or newer. The C++ runtime is
linked statically, so nobody needs the Visual C++ redistributable. The installer is unsigned unless
you set `SIGN_PFX` and `SIGN_PASSWORD`, so Windows SmartScreen asks once ("More info", then "Run
anyway"); `installer/windows/README-Windows.txt` is installed beside the app and says so.

`.github/workflows/windows.yml` does the same on a GitHub runner for every push, runs `--selftest`,
and keeps the installer as a downloadable artifact. Pushing a tag such as `v0.5.0` also attaches the
installer to a release. That is the way to build it without a Windows machine.

What differs from the Mac build:

- **AI check.** A Windows binary has no symbol table, so the engine reads the PE file directly
  (`src/vibecheck/PEReader.cpp`): exports, imported DLLs and functions, strings in both narrow and
  UTF-16 (the version resource holds the company name and copyright), the C++ class names MSVC
  leaves in its run-time type information, and whether an Authenticode signature is present. Having
  no symbols is treated as normal rather than as "stripped", and the scalar-maths check is skipped,
  since Windows has no Accelerate to compare against. Expect firmer scores on the strings and class
  names than on code habits.
- **Deep check.** Everything except the allocation count, which uses a macOS allocator hook.
- **Command line.** The program is a GUI application, so it attaches to the terminal that started
  it to print. `vibecheck.cmd` runs it and waits, which is what the installer puts on PATH.

Written without a Windows machine: the PE reader is covered by `--selftest` against a hand-built
file, and the sources were checked for Mac-only code and missing standard headers, but the Windows
build itself has not been compiled by the author. The first CI run is the real test.

## Command line

```
VibeCheck --selftest               # check the analysis maths against known answers, exit
VibeCheck --scan                   # scan every plugin, print the results, exit
VibeCheck --analyze="MB EQ"        # load one plugin, render test signals, print the numbers, exit
VibeCheck --tab=Analysis           # launch with a particular tab selected
VibeCheck --vibecheck="MB EQ"      # inspect a plugin's binary and print the report, exit
VibeCheck --vibecheck=/path/to/Thing.vst3       # works on plugins that are not installed
VibeCheck --demo="AUDistortion" --signal=sine   # load and measure in the GUI, without clicking
VibeCheck --health="MB EQ"         # run the health suite on a plugin, print it, exit 1 if anything fails
VibeCheck --healthdemo="MB EQ"     # run the health suite in the GUI, without clicking
VibeCheck --sweeps --demo=...      # also run the distortion sweeps
VibeCheck --vibedemo="MB EQ"       # run a vibe check in the GUI, without clicking
VibeCheck --comparedemo="AUDelay|AUDistortion"   # load two plugins and compare them in the GUI
VibeCheck --perfdemo="AUDistortion"              # load one plugin and benchmark it in the GUI
VibeCheck --behaviour="MB EQ"      # run a plugin and measure its audio thread (allocations, CPU, spikes)
VibeCheck --vibedemo="MB EQ" --deep # a vibe check followed by a deep check, in the GUI
VibeCheck --export=mine.json       # weigh the whole library and write the shareable results file
VibeCheck --merge=folder --out=master.json   # fold many exports into one master list (.csv works too)
VibeCheck --render-assets=/tmp/art # draw the app icon and disk image backdrop, then exit
VibeCheck --snapshot=/tmp/ui.png   # launch, save a PNG of the window once idle, exit
VibeCheck --theme=light            # start in the light half of the palette
VibeCheck --splash                 # keep the splash up even when a demo switch is driving
```

These exist so the app can be verified without a human at the keyboard. `--snapshot` also works
around macOS Screen Recording permissions, which a build script does not have. `--analyze` gives
up after 120 seconds, since a plugin can wedge while loading.

## How scanning works

Probing a plugin means loading third-party code that is entitled to crash or hang. Scanning
therefore runs **out-of-process**: the app relaunches its own binary as a worker
(`--vibecheckPluginScan:<uid>`), sends it one plugin at a time, and waits up to 30 seconds for a
reply. If the worker dies or stalls, the plugin is blacklisted, a fresh worker starts, and the
scan continues. This follows JUCE's own `AudioPluginHost` coordinator/worker pattern.

Plugin instantiation for audio and UI (Phase 2) runs in-process, as it must.

## Measurement

Measurements are rendered offline, with no audio device: the same input gives the same output
every time, at whatever sample rate the test asks for, and a dropout cannot corrupt an FFT. A real
device is only needed later, for auditioning.

The renderer forces plain stereo on the main buses and disables any extra bus, so a plugin with a
sidechain does not receive the stimulus on the wrong input. It renders past the end of the input
to capture tails, and trims the plugin's reported latency from the head so input and output line
up sample for sample.

One run (`--analyze`, or the Run measurements button) renders five stimuli and reads a good deal
more than distortion from them: magnitude, phase and group delay at 1 kHz, the 3 dB corners, how
long the plugin rings after an impulse, harmonic content split into even and odd orders (even
means asymmetric saturation, odd means symmetric clipping), compressor threshold and ratio read
from the transfer curve, DC offset, crest factor and self-noise. Each of those has a check in
`--selftest` against a signal whose answer is known.

Signals available: impulse (magnitude and phase), sine (harmonic distortion), SMPTE dual sine at
60 Hz and 7 kHz (intermodulation), a ramped tone (compressor curves), white noise, and silence as
a control for self-noise.

The FFT is double precision throughout, because a single-precision transform bottoms out near
-140 dB, close enough to real harmonic levels to matter. Tones are measured across the whole main
lobe of a Blackman-Harris window, corrected by the window's RMS gain rather than its coherent
gain - without that correction every level reads 3.01 dB high.

`--selftest` checks all of this against signals whose answers are known in advance: a tone with a
1% second harmonic must read 1.000% THD, one sample of delay must read -90 degrees at 12 kHz, and
so on. Analysis code that is only ever pointed at real plugins cannot be checked, because a wrong
plot still looks plausible.

## Plugin health

`--health` and the Plugin Health page run thirteen checks (an instrument gets the ones that apply
to it, plus a MIDI check). Each reports pass, warning or failure with a plain sentence about what
was found and why it matters:

starts up and renders; silent when nothing is playing; never produces NaN or infinity (including at
+18 dBFS in); gives the same result on every run; sounds the same at buffer sizes 16, 37, 333 and
2048 as at 512; runs at 8 to 192 kHz; reports its latency honestly (an impulse is rendered with no
compensation and the loudest sample compared with the reported figure); no slowdown on denormal
input; supports the standard channel layouts and renders in mono; decays instead of ringing
forever; has well-formed parameters; saves and restores its state; survives 18 random or extreme
parameter settings. A plugin that cannot render at all stops the run there.

The command-line form exits with status 1 when any test fails, so a plugin's build can be gated on
it. Plugins run in-process, so a plugin that crashes will close VibeCheck: that is a result in
itself, but it is why the page says so.

Run against `fixtures/`, it found that the careful reference plugin (`crafted-plugin`) has empty
`getStateInformation` and `setStateInformation` bodies, so it never restores its settings.

## Deep check: what the audio thread does

The binary can be made to look like anyone's work. The audio thread cannot hide how it behaves,
so **Deep check** (a button on the AI Check page, or `--behaviour=`) loads the plugin in a child
process, so that a crash or a hang cannot take the app down, and feeds it several seconds of audio
while counting what it asks the operating system for. It measures:

- **Allocations on the audio thread**, using the macOS allocator's own hook, with the parameters
  still and then moving. Real-time code must not allocate; generated code often builds vectors and
  strings inside `processBlock`. One of the plugins this was built against allocated on every
  block, and on average 1,697 times per block while a parameter moved.
- **CPU cost** at 48 kHz / 512 samples, the **slowest 1% of blocks** against the median, the
  **cost per sample at 16-sample blocks** against 512, and the **slowdown on near-silent input**.

Performance is evidence, not proof. Oversampling and convolution are expensive on purpose, and
FFT plugins do work once per block, so these weigh less than allocation: 25 points for allocating
in most blocks, 15 when only automation makes it, 12 for an unusually expensive plugin, 4 to 6
for the rest. A plugin that makes no allocations at all is listed as a point the other way. Results
are cached against the file's modification time and folded into both the list score and the detail
panel, so the two always agree. Allocation counting works on macOS only; elsewhere it says so and
scores nothing for it. Plugins that need hardware or a licence to run (the UAD ones, for example)
time out and are reported as not measured.

## Sharing results: export and merge

**Export** on the AI Check page (or `--export=`) writes every plugin's name, maker, format,
version, a short fingerprint of its binary, its score and the fingerprints behind it, and any deep
check numbers. It contains no file paths, user names or anything about the machine. `--merge=`
takes exports, a comma-separated list, or a folder of them, and writes one master list in which
the same plugin from different people is one row with the median score, the range, and how many
people reported it. Rows made by an older rule set are not mixed in with newer ones. A file that
is not an export, or that a newer VibeCheck wrote, is skipped with a warning.

## On the AI detection score

Worth being clear about what this can and cannot do.

The heuristics read what the binary exposes: metadata, symbol names, string tables, which library
functions are referenced. That is informative for unwrapped, unstripped plugins. It is **useless**
against a plugin whose code is encrypted or stripped — which includes every PACE/iLok-wrapped
plugin, i.e. most of a typical commercial library. A stock JUCE template also looks the same
whether a human or a model typed it.

So VibeCheck reports a score **with the evidence behind it and a separate confidence reading**,
and where the binary is opaque it says "insufficient evidence" rather than inventing a number.

What it looks for: placeholder identity fields (including AudioUnit codes still on JUCE's
`Manu` and `Dem0`, and filler copyright text), untouched JUCE template class names, text that
reads like it came from a chat assistant, build paths that run through an AI tool's own folder
(`/.claude`, `CLAUDE.md`, `.cursorrules` and similar, which assertions bake into a binary),
ALL CAPS parameter IDs that read as audio parameters, a VST3 that lists no website or email, and
scalar maths with nothing vectorised anywhere. A few weak ones count a little: generic class names
read from the symbol table, an ad hoc code signature, a generic embedded background image.
Tutorial leftovers (default file names, the parameter-tree setup, `getRawParameterValue`, a timer
editor, the default version) are each small, but three of them together add 12 and four add 20,
because that combination is how a generated project that never left the tutorial looks. iPlug2
plugins that ship the framework's own example controls get 8. A lone ALL CAPS string no longer
counts at all: UAD, SonoBus and Apple binaries all carry strings like `RATE`.

Verdicts: under 10% reads as hand-written, 10-30% some template smell, 30-55% substantial
fingerprints, above that machine-generated. The edges were set against labelled plugins: four
that are known to be vibe-coded and the rest of the library as the negatives. On static evidence
alone the four score 11 to 46 (AL-1 46, TEQ-6P 31, Hot Glass 20, PitchNet 11) and no plugin of
the other 490 that were readable scored above 7. With a deep check they read AL-1 71, TEQ-6P 43
(it costs 12% of a core), Hot Glass 20 and PitchNet 15, so two of the four still sit in the "some
template smell" band: their binaries and their behaviour give little away. That is four positives, so treat the numbers as a calibration, not
a measured error rate; `fixtures/gearspacesuite` holds ten more AI-written plugins (38 to 74%
statically) for growing the set.

Pointing the other way, and listed rather than scored: a signature from an Apple developer team,
the plugin's own web address, and licensing or update machinery.

The score is checked against your own library. The first version flagged SonoBus, an open-source
hand-written plugin, because `SonobusPluginEditor.cpp` contained the substring `PluginEditor.cpp`;
file names are now matched whole. Cached scores carry a heuristics version, so a stale score from
an older rule set is never shown beside a fresh one.

Heuristics were deliberately removed or weakened after testing, because they fired on a
hand-written control: the presence of `GenericAudioProcessorEditor` (in every JUCE binary whether
it is used or not), the absence of SIMD (plenty of good plugins have no need of it), the
`getStateInformation` and program-management overrides (JUCE declares them pure virtual, so every
plugin defines them and a symbol table cannot show whether the body is empty), JUCE's own
`CriticalSection` (it is in every JUCE binary), and an ALL CAPS ID matched only by a title-case
twin (a plugin called "Sway" contains both "Sway" and "SWAY" whatever its author did). The
tutorial's parameter-tree setup and `getRawParameterValue` still count, but lightly, since people
copy the tutorial as readily as models do. A signal that detects
the framework, or that punishes the absence of evidence, measures nothing. Where a family cannot
be judged the report says so instead of scoring zero.

`fixtures/` holds two plugins with known provenance - one wearing every fingerprint, one written
carefully - because no shipping binary can settle whether a detector works. They currently score
98% and 3%, at identical confidence, from the same framework and build settings. (Before the
heuristics were corrected the hand-written one scored 60%; `--selftest` now holds a synthetic
hand-written plugin under 10%.)

## Look

Ink and paper swap between the dark and light themes (the choice is remembered), tinted warm so
the whole thing reads as graystone. On top of them is a restrained accent family: a stone for what
is primary or selected, a cool slate for the second trace, and green, amber and red only to say
whether a result is good, borderline or bad. The logo is seven rounded bars whose centres run along
a V, drawn from geometry (`drawLogoMark`, and `assets/brand/vibecheck_mark.svg`). Every page is built from the same few parts in `src/ui`: cards, headline stat tiles, pills,
a search field, an indeterminate busy bar and an icon set (`Widgets`), and a look and feel that
rounds every stock control. Graphs share one engine (`Plots`) with a nice-number grid, filled
traces, legend chips, an animated reveal and a hover read-out.

Pages change with Command+1 to Command+5. Number formatting everywhere goes through `mbs::num`,
because `juce::String (double, 0)` does not mean zero decimals: it prints every significant digit.

Marks in `assets/brand/` are copied from the studio's asset library and embedded in the binary.
The type is Helvetica Neue, which is the documented substitute for Nimbus Sans in the studio's own
font stack - Nimbus ships as a web font the app cannot load.

The splash asks the only question the app really answers. Any automation switch steps past it;
`--splash` keeps it up.

## Versioning

`.githooks/pre-commit` bumps the patch version in `CMakeLists.txt` on every commit. Enable it once
per clone:

```
git config core.hooksPath .githooks
```

## Licensing note

Built against JUCE 8, which carries its own licence terms. Check that your use falls within them.
