# Publishing Multi-Chat Studio

Public users do not run `build-installer.ps1` and do not install developer
tools. They download `MultiChatStudio-Setup-19.0.0.exe` from the Releases page.
That installer contains the application and its required Qt runtime.

## First public release

1. Create a public GitHub repository.
2. Upload the contents of this folder, including `.github`.
3. Open the repository's **Actions** tab and run **Build Windows Installer**.
4. Download the installer artifact and test it on a normal Windows 10/11 PC.
5. After testing, create and push a tag such as `v19.0.0`.

The tag build automatically creates a GitHub Release and attaches the Windows
installer. Future users only visit Releases, download the installer, and run
it. No Python, CMake, Visual Studio, Qt SDK, or Inno Setup installation is
required on their computers.

Windows may initially display a SmartScreen reputation warning because a new
open-source executable is unsigned. Avoiding that warning consistently requires
an Authenticode code-signing certificate; it is unrelated to bundled runtimes.

