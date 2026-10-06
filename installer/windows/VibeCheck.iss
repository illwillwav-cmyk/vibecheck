; Inno Setup 6.3+ script for the VibeCheck installer on Windows.
;
; Build with:   iscc /DAppVersion=0.5.0 /DSourceDir=..\..\build-win\VibeCheck_artefacts\Release VibeCheck.iss
; (build_windows.ps1 and the GitHub workflow do this for you.)

#ifndef AppVersion
  #define AppVersion "0.5.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\build-win\VibeCheck_artefacts\Release"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

[Setup]
AppId={{FFF6A09E-7355-4A8A-99F8-8F1D409E4584}
AppName=VibeCheck
AppVersion={#AppVersion}
AppVerName=VibeCheck {#AppVersion}
AppPublisher=Make Believe Studios
AppPublisherURL=https://github.com/
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\VibeCheck
DefaultGroupName=VibeCheck
DisableProgramGroupPage=yes
DisableDirPage=auto
OutputDir={#OutputDir}
OutputBaseFilename=VibeCheck-{#AppVersion}-Windows-Setup
SetupIconFile=..\..\assets\icon\VibeCheck.ico
UninstallDisplayIcon={app}\VibeCheck.exe
UninstallDisplayName=VibeCheck
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Installs for the current user without an administrator prompt; the wizard offers to install for
; everyone instead, which does ask.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
InfoBeforeFile=README-Windows.txt
CloseApplications=yes
RestartApplications=no
ChangesEnvironment=yes
#ifdef SignTool
SignTool={#SignTool}
SignedUninstaller=yes
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked
Name: "addtopath"; Description: "Add the &command line tool (vibecheck) to PATH"; GroupDescription: "Command line:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\VibeCheck.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "vibecheck.cmd"; DestDir: "{app}"; Flags: ignoreversion
Source: "README-Windows.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\VibeCheck"; Filename: "{app}\VibeCheck.exe"; Comment: "Measure, check and compare audio plugins"
Name: "{autodesktop}\VibeCheck"; Filename: "{app}\VibeCheck.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\VibeCheck.exe"; Description: "Launch VibeCheck"; Flags: nowait postinstall skipifsilent

[Code]
const
  EnvironmentKey = 'Environment';

function IsAdminInstall(): Boolean;
begin
  Result := IsAdminInstallMode;
end;

function PathRootKey(): Integer;
begin
  if IsAdminInstall() then Result := HKEY_LOCAL_MACHINE else Result := HKEY_CURRENT_USER;
end;

function PathSubKey(): String;
begin
  if IsAdminInstall() then
    Result := 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment'
  else
    Result := EnvironmentKey;
end;

procedure AddToPath(const Folder: String);
var
  Existing: String;
begin
  if not RegQueryStringValue(PathRootKey(), PathSubKey(), 'Path', Existing) then
    Existing := '';

  if Pos(';' + Lowercase(Folder) + ';', ';' + Lowercase(Existing) + ';') > 0 then
    Exit;

  if (Existing <> '') and (Copy(Existing, Length(Existing), 1) <> ';') then
    Existing := Existing + ';';

  RegWriteExpandStringValue(PathRootKey(), PathSubKey(), 'Path', Existing + Folder);
end;

procedure RemoveFromPath(const Folder: String);
var
  Existing, Wrapped: String;
  At: Integer;
begin
  if not RegQueryStringValue(PathRootKey(), PathSubKey(), 'Path', Existing) then
    Exit;

  Wrapped := ';' + Existing + ';';
  At := Pos(';' + Lowercase(Folder) + ';', Lowercase(Wrapped));

  if At = 0 then
    Exit;

  Delete(Wrapped, At, Length(Folder) + 1);
  Existing := Copy(Wrapped, 2, Length(Wrapped) - 2);
  RegWriteExpandStringValue(PathRootKey(), PathSubKey(), 'Path', Existing);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('addtopath') then
    AddToPath(ExpandConstant('{app}'));
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RemoveFromPath(ExpandConstant('{app}'));
end;
