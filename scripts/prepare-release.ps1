[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version,
    [switch]$Push
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ProjectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $ProjectRoot
try {
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        throw "Git was not found."
    }

    $Branch = (git branch --show-current).Trim()
    if ($LASTEXITCODE -ne 0) { throw "Could not determine the current Git branch." }
    if ($Branch -ne "main") {
        throw "Releases must be prepared from main. Current branch: $Branch"
    }

    $DirtyFiles = git status --porcelain
    if ($LASTEXITCODE -ne 0) { throw "Could not inspect the Git working tree." }
    if ($DirtyFiles) {
        throw "Commit or stash existing changes before preparing a release."
    }

    $CurrentVersion = (Get-Content (Join-Path $ProjectRoot "VERSION") -Raw).Trim()
    if ([version]$Version -le [version]$CurrentVersion) {
        throw "The new version ($Version) must be greater than the current version ($CurrentVersion)."
    }

    $Tag = "v$Version"
    git rev-parse --verify --quiet "refs/tags/$Tag" | Out-Null
    if ($LASTEXITCODE -eq 0) {
        throw "Tag $Tag already exists."
    }

    Set-Content -Path (Join-Path $ProjectRoot "VERSION") -Value $Version -Encoding ascii -NoNewline
    git add VERSION
    git commit -m "Release $Tag"
    if ($LASTEXITCODE -ne 0) { throw "Could not create the release commit." }

    git tag -a $Tag -m "Leapcast Studio $Version"
    if ($LASTEXITCODE -ne 0) { throw "Could not create tag $Tag." }

    if ($Push) {
        git push origin HEAD
        if ($LASTEXITCODE -ne 0) { throw "Could not push the release commit." }
        git push origin $Tag
        if ($LASTEXITCODE -ne 0) { throw "Could not push $Tag." }
        Write-Host "Published $Tag. GitHub Actions is building the Windows installer."
    } else {
        Write-Host "Prepared $Tag locally."
        Write-Host "Review it, then publish with:"
        Write-Host "  git push origin HEAD"
        Write-Host "  git push origin $Tag"
    }
}
finally {
    Pop-Location
}
