# Thin shim. Determine version, delegate to the shared publisher.
# See cameraunlock-core/powershell/NightlyRelease.psm1 for what it does.

[CmdletBinding()]
param(
    [switch]$AllowDirty
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')

Import-Module (Join-Path $ProjectRoot 'cameraunlock-core\powershell\NightlyRelease.psm1') -Force

$constantsFile = Join-Path $ProjectRoot 'src\core\constants.h'
$versionMatch = Select-String -Path $constantsFile -Pattern 'ACUHT_VERSION\s*=\s*"([^"]+)"'
if (-not $versionMatch) {
    throw "Could not extract version from $constantsFile"
}
$version = $versionMatch.Matches[0].Groups[1].Value

Publish-NightlyBuild `
    -ModId 'assassins-creed-unity' `
    -ModName 'AssassinsCreedUnityHeadTracking' `
    -Version $version `
    -ProjectRoot $ProjectRoot `
    -AllowDirty:$AllowDirty
