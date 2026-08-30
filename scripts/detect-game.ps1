#!/usr/bin/env pwsh
#Requires -Version 5.1
# Resolve the Assassin's Creed Unity install path through cameraunlock-core's
# canonical detection module. The detection data (env var, Steam app id and
# folder, Ubisoft Connect product ids, executable) lives in
# cameraunlock-core/data/games.json under the `assassins-creed-unity` key.

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir

Import-Module (Join-Path $projectRoot "cameraunlock-core\powershell\GamePathDetection.psm1") -Force

$gameId   = 'assassins-creed-unity'
$gamePath = Find-GamePath -GameId $gameId

if ($gamePath) {
    Write-Host "Found: $gamePath" -ForegroundColor Green
    exit 0
}

$config = Get-GameConfig -GameId $gameId
Write-GameNotFoundError -GameName $config.DisplayName -EnvVar $config.EnvVar -SteamFolder $config.SteamFolder
exit 1
