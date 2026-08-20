# Multi-Chat Studio C++

Native C++20/Qt 6 rewrite of the Lefroge multi-platform creator console for
Windows 10 and Windows 11. The frog splash and other visual assets are compiled
into the executable with Qt resources; they are not loose runtime files.

## For streamers and moderators

Download `MultiChatStudio-Setup-19.0.0.exe` from the project's GitHub Releases
page. Run the installer, then open Multi-Chat Studio from the Start menu. No
Python, CMake, Visual Studio, Qt SDK, or separate image files are required.

The sections below are only for developers compiling the source code.

## Build prerequisites

- Visual Studio 2022 with Desktop development with C++
- Qt 6.5 or newer (MSVC 2022 x64)
- CMake 3.24 or newer
- Inno Setup 6 for the installer

## Configure and build

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64"
cmake --build build --config Release
```

Run `windeployqt` into `deploy`, copy `MultiChatStudio.exe` there, and compile
`installer/MultiChatStudio.iss`. The resulting installer contains the Qt
runtime; end users do not install Qt or Python.

## Native subsystem status

Implemented in C++: settings, message/event models, audit persistence,
bypass-resistant AutoMod, Twitch IRC and viewer polling, Twitch Helix actions,
YouTube/Shorts discovery and InnerTube chat, YouTube moderation requests,
TikTok WebEngine event bridge and moderation requests, Streamlabs Socket.IO,
source controls, chat rendering, pop-out alerts, viewer totals, and the local
OBS chat/viewer endpoints.

No Python interpreter, Python module, or Python subprocess is used by this
project. Authentication tokens and client IDs remain user-provided settings;
they are never compiled into the repository.

Run `build-installer.ps1` from a Visual Studio Developer PowerShell to build,
deploy the Qt runtime, and create the installer.

Repository maintainers can build without installing tools locally: the
included GitHub Actions workflow compiles on Windows, bundles Qt, creates the
installer, and attaches it to tagged releases. See `PUBLISHING.md`.
