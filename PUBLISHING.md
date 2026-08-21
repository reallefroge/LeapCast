# Publishing Leapcast Studio

The release process is designed so maintainers never edit version numbers in
C++, CMake, Inno Setup, or GitHub Actions by hand. The root `VERSION` file is
the single source of truth.

## Normal release

Start from a clean `main` branch containing all changes for the release:

```powershell
git pull --ff-only
.\scripts\prepare-release.ps1 -Version 2.0.3 -Push
```

Replace `2.0.3` with the next semantic version. The script:

1. refuses to run if uncommitted files could be lost;
2. verifies that the new version is greater than the current version;
3. changes only `VERSION`;
4. creates a release commit and annotated `v<version>` tag; and
5. pushes the commit and tag when `-Push` is supplied.

The pushed tag starts the **Windows build and release** workflow. GitHub:

1. verifies that the tag matches `VERSION`;
2. builds the native C++/Qt application on Windows;
3. gathers the Qt and compiler runtime files;
4. creates `LeapcastStudio-Setup-<version>.exe`;
5. creates a matching SHA-256 checksum file; and
6. publishes both files in a GitHub Release with generated release notes.

The application's update checker reads that Release and offers the new
installer the next time an older version starts.

Before publishing, format the GitHub Release body using
[`docs/UPDATE_NOTES_STYLE.md`](docs/UPDATE_NOTES_STYLE.md). The application
uses the first non-empty line as the compact summary and displays the complete
Markdown notes in the update dialog.

## Review before publishing

Omit `-Push` to create the release commit and tag locally:

```powershell
.\scripts\prepare-release.ps1 -Version 2.0.3
```

After reviewing the commit and tag, publish them:

```powershell
git push origin HEAD
git push origin v2.0.3
```

## Test a build without releasing

Open the repository's **Actions** tab, choose **Windows build and release**,
and select **Run workflow**. A manual run builds the installer and stores it as
a temporary workflow artifact, but it does not create a public Release.

Every push to `main` and every pull request also builds a testable installer.
This catches packaging failures before a version tag is published.

## Build locally

From a Visual Studio Developer PowerShell:

```powershell
.\build-installer.ps1
```

The script locates Qt through `qmake`, compiles the application, rebuilds a
clean deployment folder, runs `windeployqt`, creates the installer, and writes
both deliverables to `artifacts`.

If Qt is not on `PATH`, pass its root explicitly:

```powershell
.\build-installer.ps1 -QtRoot "C:\Qt\6.8.3\msvc2022_64"
```

## Recovery

- **The tag and VERSION do not match:** delete or correct the local tag before
  pushing. The workflow intentionally refuses mismatched public versions.
- **A normal build fails:** fix the source on `main` and verify the workflow
  before preparing another version.
- **A tagged build fails after the tag was pushed:** fix the problem, move the
  tag only if no public Release was successfully distributed, then rerun the
  workflow. Avoid replacing an installer that users may already have.
- **Windows SmartScreen appears:** new unsigned executables can show a
  reputation warning. Consistently avoiding it requires Authenticode signing.
