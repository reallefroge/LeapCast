param(
    [string]$QtRoot = "C:\Qt\6.8.3\msvc2022_64",
    [string]$Configuration = "Release"
)
$ErrorActionPreference = "Stop"
$ProjectRoot = $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "build"
$DeployDir = Join-Path $ProjectRoot "deploy"
cmake -S $ProjectRoot -B $BuildDir -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=$QtRoot
cmake --build $BuildDir --config $Configuration
New-Item -ItemType Directory -Force $DeployDir | Out-Null
Copy-Item (Join-Path $BuildDir "$Configuration\MultiChatStudio.exe") $DeployDir -Force
& (Join-Path $QtRoot "bin\windeployqt.exe") --release --no-translations --compiler-runtime (Join-Path $DeployDir "MultiChatStudio.exe")
$Iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
if (!(Test-Path $Iscc)) { throw "Inno Setup 6 was not found." }
& $Iscc (Join-Path $ProjectRoot "installer\MultiChatStudio.iss")

