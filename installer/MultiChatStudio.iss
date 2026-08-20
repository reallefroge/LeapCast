#define AppName "Multi-Chat Studio"
#define AppVersion "19.0.0"
#define AppPublisher "Lefroge"
#define BuildRoot "..\deploy"

[Setup]
AppId={{A80DBFB8-59F3-45A6-A9F0-F9576C59C3D0}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\Multi-Chat Studio
DefaultGroupName={#AppName}
OutputBaseFilename=MultiChatStudio-Setup-{#AppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
WizardStyle=modern
SetupIconFile=..\packaging\lefroge_chat_icon.ico
UninstallDisplayIcon={app}\MultiChatStudio.exe

[Files]
Source: "{#BuildRoot}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\MultiChatStudio.exe"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\MultiChatStudio.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"

[Run]
Filename: "{app}\MultiChatStudio.exe"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent

