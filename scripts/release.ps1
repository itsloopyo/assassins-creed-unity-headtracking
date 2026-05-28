param(
    [Parameter(Position = 0)]
    [string]$Version
)

$ErrorActionPreference = "Stop"

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Import-Module (Join-Path $ProjectRoot "cameraunlock-core\powershell\ReleaseWorkflow.psm1") -Force

if (-not $Version) {
    Write-Error "Usage: pixi run release <major|minor|patch|nightly|X.Y.Z>"
    exit 1
}

if ($Version -eq 'nightly') {
    & (Join-Path $PSScriptRoot 'release-nightly.ps1')
    exit $LASTEXITCODE
}

$constantsFile = Join-Path $ProjectRoot "src\core\constants.h"
$cmakeFile     = Join-Path $ProjectRoot "CMakeLists.txt"
$changelogFile = Join-Path $ProjectRoot "CHANGELOG.md"

$currentVersionMatch = Select-String -Path $constantsFile -Pattern 'ACUHT_VERSION\s*=\s*"([^"]+)"'
if (-not $currentVersionMatch) {
    Write-Error "Could not read current version from constants.h"
    exit 1
}
$currentVersion = $currentVersionMatch.Matches[0].Groups[1].Value

$Version = Resolve-ReleaseVersion -Argument $Version -CurrentVersion $currentVersion
if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    Write-Error "Resolved version '$Version' is not semantic X.Y.Z"
    exit 1
}

Write-Host "Releasing v$Version (from v$currentVersion)..." -ForegroundColor Cyan

# Preconditions: on main, clean tree, tag does not exist.
Push-Location $ProjectRoot
try {
    $branch = (git rev-parse --abbrev-ref HEAD).Trim()
    if ($branch -ne 'main') {
        Write-Error "Releases must run from 'main' (current: '$branch')"
        exit 1
    }
    if (-not (Test-CleanGitStatus)) {
        Write-Error "Working tree is dirty. Commit or stash before releasing."
        exit 1
    }
    if (Test-GitTagExists -Tag "v$Version") {
        Write-Error "Tag v$Version already exists."
        exit 1
    }
} finally {
    Pop-Location
}

# Bump version in canonical sources.
$content = Get-Content $constantsFile -Raw
$content = $content -replace '(ACUHT_VERSION\s*=\s*")([^"]+)(")', "`${1}$Version`${3}"
Set-Content -Path $constantsFile -Value $content -NoNewline
Write-Host "  Updated constants.h -> $Version" -ForegroundColor Green

$cmakeContent = Get-Content $cmakeFile -Raw
$cmakeContent = $cmakeContent -replace '(project\(AssassinsCreedUnityHeadTracking VERSION )\d+\.\d+\.\d+', "`${1}$Version"
Set-Content -Path $cmakeFile -Value $cmakeContent -NoNewline
Write-Host "  Updated CMakeLists.txt -> $Version" -ForegroundColor Green

# Build release.
Write-Host "Building release..." -ForegroundColor Cyan
& cmake --build (Join-Path $ProjectRoot "build") --config Release
if ($LASTEXITCODE -ne 0) { Write-Error "Build failed"; exit 1 }

# Generate changelog entry.
Write-Host "Generating CHANGELOG entry..." -ForegroundColor Cyan
$null = New-ChangelogFromCommits -ChangelogPath $changelogFile -Version $Version

# Package.
Write-Host "Packaging..." -ForegroundColor Cyan
& (Join-Path $PSScriptRoot "package-release.ps1")
if ($LASTEXITCODE -ne 0) { Write-Error "Packaging failed"; exit 1 }

# Commit, tag, push.
Push-Location $ProjectRoot
try {
    Write-Host "Committing version bump..." -ForegroundColor Cyan
    git add $constantsFile $cmakeFile $changelogFile
    git commit -m "Release v$Version"
    if ($LASTEXITCODE -ne 0) { Write-Error "Commit failed"; exit 1 }

    Write-Host "Creating annotated tag v$Version..." -ForegroundColor Cyan
    New-ReleaseTag -Version $Version -Message "Release v$Version" -Branch 'main'
} finally {
    Pop-Location
}

Write-Host ""
Write-Host "Released v$Version. CI will publish artifacts from the v$Version tag." -ForegroundColor Green
