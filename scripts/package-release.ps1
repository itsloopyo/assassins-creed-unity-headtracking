$ErrorActionPreference = "Stop"

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$ModName = "AssassinsCreedUnityHeadTracking"
$BuildDir = Join-Path (Join-Path $ProjectRoot "bin") "Release"
$ReleaseDir = Join-Path $ProjectRoot "release"

Import-Module (Join-Path $ProjectRoot "cameraunlock-core\powershell\ReleaseWorkflow.psm1") -Force

# Extract version from constants.h
$constantsFile = Join-Path (Join-Path (Join-Path $ProjectRoot "src") "core") "constants.h"
$versionMatch = Select-String -Path $constantsFile -Pattern 'ACUHT_VERSION\s*=\s*"([^"]+)"'
if (-not $versionMatch) {
    Write-Error "Could not extract version from constants.h"
    exit 1
}
$version = $versionMatch.Matches[0].Groups[1].Value
Write-Host "Packaging $ModName v$version" -ForegroundColor Cyan

# Validate build
$asiPath = Join-Path $BuildDir "$ModName.asi"
if (-not (Test-Path $asiPath)) {
    Write-Error "Build output missing: $asiPath. Run 'pixi run build-release' first."
    exit 1
}

$vendorAsiDir = Join-Path $ProjectRoot "vendor/ultimate-asi-loader"
$vendorAsiDll = Join-Path $vendorAsiDir "dinput8.dll"
if (-not (Test-Path $vendorAsiDll)) {
    throw "Bundled ASI loader missing: $vendorAsiDll. Run 'pixi run update-deps' first."
}

# launcher-manifest.json is the contract the Lopari launcher reads at the ZIP
# root. delivery_mode "install_cmd" keeps us on the legacy script path; the
# version is stamped to match constants.h at package time.
$launcherManifestPath = Join-Path $ProjectRoot "launcher-manifest.json"
if (-not (Test-Path $launcherManifestPath)) {
    throw "launcher-manifest.json not found at: $launcherManifestPath"
}
$launcherManifest = Get-Content $launcherManifestPath -Raw | ConvertFrom-Json
$launcherManifest.mod_info.version = $version

if (-not (Test-Path $ReleaseDir)) {
    New-Item -ItemType Directory -Path $ReleaseDir | Out-Null
}

# --- Installer ZIP (GitHub Release) ---
$installerZipName = "$ModName-v$version-installer.zip"
$installerZipPath = Join-Path $ReleaseDir $installerZipName
$stagingDir = Join-Path $env:TEMP "$ModName-installer-staging"

if (Test-Path $stagingDir) { Remove-Item $stagingDir -Recurse -Force }
New-Item -ItemType Directory -Path $stagingDir | Out-Null

# plugins/ holds the ASI + default INI. install.cmd copies from here.
$pluginsDir = Join-Path $stagingDir "plugins"
New-Item -ItemType Directory -Path $pluginsDir | Out-Null
Copy-Item $asiPath $pluginsDir
Copy-Item (Join-Path $ProjectRoot "HeadTracking.ini") $pluginsDir

# Bundle Ultimate ASI Loader (MIT, see THIRD-PARTY-NOTICES.md) so install.cmd
# has no GitHub dependency at install time.
$stagedVendorDir = Join-Path $stagingDir "vendor\ultimate-asi-loader"
New-Item -ItemType Directory -Path $stagedVendorDir -Force | Out-Null
# All three are required. The LICENSE and README carry Ultimate ASI Loader's
# MIT notice and its provenance, and MIT requires them to travel with the
# binary - a silent skip would turn a licence violation into a green build.
foreach ($vendorFile in @("dinput8.dll", "LICENSE", "README.md")) {
    $src = Join-Path $vendorAsiDir $vendorFile
    if (-not (Test-Path $src)) {
        throw "Vendored loader file missing: $src. The installer ZIP must ship the loader's licence notice alongside its binary."
    }
    Copy-Item $src -Destination $stagedVendorDir -Force
}

# Top-level scripts and docs
Copy-Item (Join-Path $PSScriptRoot "install.cmd")   $stagingDir
Copy-Item (Join-Path $PSScriptRoot "uninstall.cmd") $stagingDir

# launcher-manifest.json at the ZIP root, version stamped to match the build.
$launcherManifest | ConvertTo-Json -Depth 10 |
    Set-Content -Path (Join-Path $stagingDir "launcher-manifest.json") -Encoding UTF8
foreach ($doc in @("README.md", "CHANGELOG.md", "THIRD-PARTY-NOTICES.md", "LICENSE")) {
    $src = Join-Path $ProjectRoot $doc
    if (-not (Test-Path $src)) {
        throw "Release document missing: $src. LICENSE and THIRD-PARTY-NOTICES.md must ship at the installer ZIP root."
    }
    Copy-Item $src $stagingDir
}

Copy-SharedBundle -StagingDir $stagingDir

if (Test-Path $installerZipPath) { Remove-Item $installerZipPath }
Compress-Archive -Path "$stagingDir\*" -DestinationPath $installerZipPath
Remove-Item $stagingDir -Recurse -Force
Write-Host "Created: $installerZipName" -ForegroundColor Green

# --- Nexus ZIP (extract-to-game-folder) ---
$nexusZipName = "$ModName-v$version-nexus.zip"
$nexusZipPath = Join-Path $ReleaseDir $nexusZipName
$nexusStaging = Join-Path $env:TEMP "$ModName-nexus-staging"

if (Test-Path $nexusStaging) { Remove-Item $nexusStaging -Recurse -Force }
New-Item -ItemType Directory -Path $nexusStaging | Out-Null
Copy-Item $asiPath $nexusStaging
Copy-Item (Join-Path $ProjectRoot "HeadTracking.ini") $nexusStaging

if (Test-Path $nexusZipPath) { Remove-Item $nexusZipPath }
# The Nexus ZIP is a binary distribution too: the licences of everything
# compiled into or bundled with the payload require their notices to travel
# with it, so LICENSE and THIRD-PARTY-NOTICES.md ship at its root.
foreach ($noticeDoc in @('LICENSE', 'THIRD-PARTY-NOTICES.md', 'README.md')) {
    $noticeSrc = Join-Path $ProjectRoot $noticeDoc
    if (-not (Test-Path $noticeSrc)) {
        throw "Required notice file not found: $noticeDoc. Every published ZIP is a binary distribution and must carry it."
    }
    Copy-Item $noticeSrc -Destination $nexusStaging -Force
    Write-Host "  $noticeDoc" -ForegroundColor Green
}
Compress-Archive -Path "$nexusStaging\*" -DestinationPath $nexusZipPath
Remove-Item $nexusStaging -Recurse -Force
Write-Host "Created: $nexusZipName" -ForegroundColor Green

Write-Host ""
Write-Host "Output: $ReleaseDir" -ForegroundColor Green
