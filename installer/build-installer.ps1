[CmdletBinding()]
param(
    [string]$QtRoot = "",
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$OutputDirectory = "",
    [string]$TwitchClientId = $env:LEAPCAST_TWITCH_CLIENT_ID,
    # Release installers update themselves. Pass -NoAutoUpdate to build a local
    # test installer that can never be replaced by the published release.
    [switch]$NoAutoUpdate,
    # Kept so an older workflow that passes this switch still runs. A missing
    # Client ID is now tolerated by default, so this is a no-op.
    [switch]$AllowMissingTwitchClientId,
    # Refuse to build when no Twitch Client ID is available. Set automatically
    # for tag builds, which is what a published release is.
    [switch]$RequireTwitchClientId
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ProjectRoot = $PSScriptRoot
$VersionFile = Join-Path $ProjectRoot "VERSION"
$Version = (Get-Content $VersionFile -Raw).Trim()

if ($Version -notmatch '^\d+\.\d+\.\d+(\.\d+)?$') {
    throw "VERSION must contain three or four numeric parts, such as 3.0.9 or 3.0.9.1."
}

if ([string]::IsNullOrWhiteSpace($TwitchClientId)) {
    # A published release must carry the bundled Twitch application ID, so a tag
    # build still refuses outright. Everything else - local test builds, ordinary
    # branch pushes in CI - continues without one, and the guard does not depend
    # on the workflow remembering to pass a switch.
    $BuildingRelease = $RequireTwitchClientId -or ($env:GITHUB_REF_TYPE -eq "tag")
    if ($BuildingRelease) {
        throw "LEAPCAST_TWITCH_CLIENT_ID is required for a release build. Add a repository variable named TWITCH_CLIENT_ID under Settings -> Secrets and variables -> Actions -> Variables, then tag again."
    }
    Write-Host "" 
    Write-Host "WARNING: building WITHOUT a bundled Twitch application ID." -ForegroundColor Yellow
    Write-Host "         This is a TEST installer. Twitch Connect will report that the" -ForegroundColor Yellow
    Write-Host "         build has no Twitch application ID; every other feature works." -ForegroundColor Yellow
    Write-Host "         Set the TWITCH_CLIENT_ID repository variable before publishing." -ForegroundColor Yellow
    Write-Host ""
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

# Never let a previous-version installer remain in the publish directory.
# The release workflow must upload only the installer matching VERSION.
Get-ChildItem -Path $OutputDirectory -Filter "LeapcastStudio-Setup-*.exe*" -File -ErrorAction SilentlyContinue |
    Remove-Item -Force

Write-Host "Building Leapcast Studio v$Version..."
$AutoUpdateFlag = if ($NoAutoUpdate) { "OFF" } else { "ON" }
if ($NoAutoUpdate) {
    Write-Host "Automatic updates are DISABLED in this build. Omit -NoAutoUpdate for a normal release installer." -ForegroundColor Yellow
}
& $Cmake.Source -S $ProjectRoot -B $BuildDir -G "Visual Studio 17 2022" -A x64 `
    "-DCMAKE_PREFIX_PATH=$QtRoot" "-DLEAPCAST_TWITCH_CLIENT_ID=$TwitchClientId" `
    "-DLEAPCAST_AUTO_UPDATE=$AutoUpdateFlag"
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }

& $Cmake.Source --build $BuildDir --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw "Compilation failed." }

$Executable = Join-Path $BuildDir "$Configuration\LeapcastStudio.exe"
if (-not (Test-Path $Executable)) {
    throw "The compiled application was not found at $Executable."
}
Copy-Item $Executable (Join-Path $DeployDir "LeapcastStudio.exe") -Force

$WinDeployQt = Join-Path $QtRoot "bin\windeployqt.exe"
if (-not (Test-Path $WinDeployQt)) {
    throw "windeployqt was not found under $QtRoot."
}
$DeployMode = if ($Configuration -eq "Debug") { "--debug" } else { "--release" }
& $WinDeployQt $DeployMode --no-translations --compiler-runtime (Join-Path $DeployDir "LeapcastStudio.exe")
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

$InstallerScript = Join-Path $ProjectRoot "installer\LeapcastStudio.iss"
& $Iscc "/DAppVersion=$Version" "/DBuildRoot=$DeployDir" "/O$OutputDirectory" $InstallerScript
if ($LASTEXITCODE -ne 0) { throw "Installer creation failed." }

$Installer = Get-Item (Join-Path $OutputDirectory "LeapcastStudio-Setup-$Version.exe")
$Hash = (Get-FileHash $Installer.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
$ChecksumPath = "$($Installer.FullName).sha256"
Set-Content -Path $ChecksumPath -Value "$Hash  $($Installer.Name)" -Encoding ascii

Write-Host ""
Write-Host "Installer: $($Installer.FullName)"
Write-Host "SHA-256:  $ChecksumPath"
