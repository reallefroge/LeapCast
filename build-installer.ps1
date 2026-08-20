[CmdletBinding()]
param(
    [string]$QtRoot = "",
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ProjectRoot = $PSScriptRoot
$VersionFile = Join-Path $ProjectRoot "VERSION"
$Version = (Get-Content $VersionFile -Raw).Trim()

if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    throw "VERSION must contain a semantic version such as 19.0.3."
}

if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    $Qmake = Get-Command qmake.exe -ErrorAction SilentlyContinue
    if (-not $Qmake) {
        $Qmake = Get-Command qmake -ErrorAction SilentlyContinue
    }
    if (-not $Qmake) {
        throw "Qt was not found. Pass -QtRoot or add qmake to PATH."
    }
    $QtRoot = (& $Qmake.Source -query QT_INSTALL_PREFIX).Trim()
}

$Cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
if (-not $Cmake) {
    $Cmake = Get-Command cmake -ErrorAction SilentlyContinue
}
if (-not $Cmake) {
    throw "CMake was not found."
}

$BuildDir = Join-Path $ProjectRoot "build"
$DeployDir = Join-Path $ProjectRoot "deploy"
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $ProjectRoot "artifacts"
}

if (Test-Path $DeployDir) {
    Remove-Item $DeployDir -Recurse -Force
}
New-Item -ItemType Directory -Force $DeployDir, $OutputDirectory | Out-Null

Write-Host "Building Multi-Chat Studio v$Version..."
& $Cmake.Source -S $ProjectRoot -B $BuildDir -G "Visual Studio 17 2022" -A x64 "-DCMAKE_PREFIX_PATH=$QtRoot"
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }

& $Cmake.Source --build $BuildDir --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw "Compilation failed." }

$Executable = Join-Path $BuildDir "$Configuration\MultiChatStudio.exe"
if (-not (Test-Path $Executable)) {
    throw "The compiled application was not found at $Executable."
}
Copy-Item $Executable (Join-Path $DeployDir "MultiChatStudio.exe") -Force

$WinDeployQt = Join-Path $QtRoot "bin\windeployqt.exe"
if (-not (Test-Path $WinDeployQt)) {
    throw "windeployqt was not found under $QtRoot."
}
$DeployMode = if ($Configuration -eq "Debug") { "--debug" } else { "--release" }
& $WinDeployQt $DeployMode --no-translations --compiler-runtime (Join-Path $DeployDir "MultiChatStudio.exe")
if ($LASTEXITCODE -ne 0) { throw "Qt deployment failed." }

$IsccCandidates = @(
    (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"),
    (Join-Path ${env:ProgramFiles} "Inno Setup 6\ISCC.exe")
)
$IsccCommand = Get-Command ISCC.exe -ErrorAction SilentlyContinue
$Iscc = if ($IsccCommand) {
    $IsccCommand.Source
} else {
    $IsccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $Iscc) {
    throw "Inno Setup 6 was not found."
}

$InstallerScript = Join-Path $ProjectRoot "installer\MultiChatStudio.iss"
& $Iscc "/DAppVersion=$Version" "/DBuildRoot=$DeployDir" "/O$OutputDirectory" $InstallerScript
if ($LASTEXITCODE -ne 0) { throw "Installer creation failed." }

$Installer = Get-Item (Join-Path $OutputDirectory "MultiChatStudio-Setup-$Version.exe")
$Hash = (Get-FileHash $Installer.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
$ChecksumPath = "$($Installer.FullName).sha256"
Set-Content -Path $ChecksumPath -Value "$Hash  $($Installer.Name)" -Encoding ascii

Write-Host ""
Write-Host "Installer: $($Installer.FullName)"
Write-Host "SHA-256:  $ChecksumPath"
