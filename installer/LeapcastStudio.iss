#define AppName "Leapcast Studio"
#define AppPublisher "Lefroge"

#ifndef AppVersion
  #define AppVersion "0.0.0-local"
#endif

#ifndef BuildRoot
  #define BuildRoot "..\deploy"
#endif

[Setup]
AppId={{A80DBFB8-59F3-45A6-A9F0-F9576C59C3D0}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
UninstallDisplayName={#AppName}
DefaultDirName={pf64}\Leapcast Studio
DefaultGroupName={#AppName}
OutputBaseFilename=LeapcastStudio-Setup-{#AppVersion}
VersionInfoVersion={#AppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
WizardResizable=no
DisableProgramGroupPage=yes
CloseApplications=yes
CloseApplicationsFilter=LeapcastStudio.exe
RestartApplications=no
SetupIconFile=..\resources\lefroge_chat_icon.ico
WizardImageFile=..\resources\installer-sidebar.bmp
WizardSmallImageFile=..\resources\installer-small.bmp
WizardImageStretch=yes
UninstallDisplayIcon={app}\LeapcastStudio.exe

[Files]
Source: "{#BuildRoot}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
Type: files; Name: "{app}\MultiChatStudio.exe"
Type: files; Name: "{autoprograms}\Multi-Chat Studio.lnk"
Type: files; Name: "{autodesktop}\Multi-Chat Studio.lnk"

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\LeapcastStudio.exe"; IconFilename: "{app}\LeapcastStudio.exe"; IconIndex: 0
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\LeapcastStudio.exe"; IconFilename: "{app}\LeapcastStudio.exe"; IconIndex: 0; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"

[Run]
Filename: "{app}\LeapcastStudio.exe"; Description: "Launch {#AppName}"; Flags: nowait runasoriginaluser
