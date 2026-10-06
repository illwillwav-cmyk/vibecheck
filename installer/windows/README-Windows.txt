VibeCheck for Windows
=====================

VibeCheck measures audio plugins, checks that they behave, and compares them. On Windows it works
with VST3 plugins. The manual is installed with it: Start menu, VibeCheck Manual.

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

COMMAND LINE
------------
Tick "Add the command line tool to PATH" in the installer, then in a new terminal:

  vibecheck --selftest
  vibecheck --health="C:\Program Files\Common Files\VST3\Thing.vst3"

UNINSTALLING
------------
Settings > Apps > Installed apps > VibeCheck > Uninstall. Your saved settings and cached scores in
%APPDATA%\VibeCheck are left in place; delete that folder if you want them gone.
