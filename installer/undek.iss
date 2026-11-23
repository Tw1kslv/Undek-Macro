; Inno Setup script for Undek Macro.
;
; Build (from the repo root, after the release package is staged in dist\UndekMacro):
;   ISCC.exe /DMyAppVersion=1.28.1 installer\undek.iss
;
; The install is per-user on purpose: no admin prompt, no UAC, and the app still
; keeps its macros next to the exe the way the portable version does.

#define MyAppName "Undek Macro"
#define MyAppExeName "undek.exe"
#ifndef MyAppVersion
  #define MyAppVersion "1.28.1"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist\UndekMacro"
#endif

[Setup]
AppId={{6D3B1E7C-9A54-4B2E-8F71-2C5A7D9E4B10}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher=Undek
DefaultDirName={localappdata}\Programs\Undek Macro
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=UndekMacro-{#MyAppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\{#MyAppExeName}
SetupIconFile=..\resources\undek.ico
UninstallDisplayName={#MyAppName}
AllowNoIcons=yes

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Flags: unchecked
Name: "startmenu"; Description: "Add {#MyAppName} to the Start menu"; Flags: checkedonce
Name: "startup"; Description: "Start {#MyAppName} with Windows"; Flags: unchecked

[Files]
; Program and read-only content. User work (macros, profiles, bindings, logs,
; settings) is deliberately excluded so an upgrade or reinstall can't eat it.
Source: "{#SourceDir}\*"; DestDir: "{app}"; \
    Excludes: "macros\*,profiles\*,bindings\*,logs\*,settings.json,imgui.ini"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

; Give a fresh install a working default profile and binding file...
Source: "{#SourceDir}\profiles\default.json"; DestDir: "{app}\profiles"; \
    Flags: onlyifdoesntexist uninsneveruninstall
Source: "{#SourceDir}\bindings\default.json"; DestDir: "{app}\bindings"; \
    Flags: onlyifdoesntexist uninsneveruninstall

; ...and somewhere for macros to live.
[Dirs]
Name: "{app}\macros"; Flags: uninsneveruninstall
Name: "{app}\logs"

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: startmenu
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: string; ValueName: "UndekMacro"; ValueData: """{app}\{#MyAppExeName}"""; \
    Flags: uninsdeletevalue; Tasks: startup

[Run]
Filename: "{app}\{#MyAppExeName}"; \
    Description: "{cm:LaunchProgram,{#MyAppName}}"; \
    Flags: nowait postinstall skipifsilent
