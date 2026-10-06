VibeCheck for Windows
=====================

VibeCheck measures audio plugins, checks that they behave, compares them, and reads their files
for signs that they were machine-generated. On Windows it works with VST3 plugins.

FIRST TIME YOU RUN THE INSTALLER
--------------------------------
This installer is not signed with a paid code-signing certificate, so Windows SmartScreen may say
"Windows protected your PC".

  1. Click "More info".
  2. Click "Run anyway".

You only have to do this once. If your browser or an antivirus tool held the download back, the
same applies: it is flagging that the file is new and unsigned, not that anything is wrong with it.
The checksum file next to the installer lets you confirm you have the file that was sent to you:

  PowerShell:   Get-FileHash .\VibeCheck-<version>-Windows-Setup.exe -Algorithm SHA256

WHERE YOUR PLUGINS ARE
----------------------
VibeCheck looks in the standard places: C:\Program Files\Common Files\VST3 and
%LOCALAPPDATA%\Programs\Common\VST3. Open the Plugin Manager page and press Scan.

THE AI CHECK ON WINDOWS
-----------------------
Windows plugins carry no symbol table, so the check reads what is there instead: the C++ class names
the compiler leaves behind, imported functions, strings (including the company name and copyright in
the version resource), and the signature. It has less to go on than on a Mac, so expect more
"insufficient evidence" and fewer firm scores. The Deep check (what the plugin does on the audio
thread) works the same way, except that counting memory allocations is only available on macOS.

COMMAND LINE
------------
Tick "Add the command line tool to PATH" in the installer, then in a new terminal:

  vibecheck --selftest
  vibecheck --vibecheck="C:\Program Files\Common Files\VST3\Thing.vst3"
  vibecheck --export=mine.json
  vibecheck --merge=folder --out=master.csv

UNINSTALLING
------------
Settings > Apps > Installed apps > VibeCheck > Uninstall. Your saved settings and cached scores in
%APPDATA%\VibeCheck are left in place; delete that folder if you want them gone.
