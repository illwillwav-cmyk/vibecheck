@echo off
rem Runs VibeCheck from a terminal and waits for it, so its output appears in order.
rem   vibecheck --selftest
rem   vibecheck --health="C:\Program Files\Common Files\VST3\Thing.vst3"
start "" /wait "%~dp0VibeCheck.exe" %*
exit /b %ERRORLEVEL%
