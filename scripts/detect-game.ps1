$ErrorActionPreference = "Stop"

$GameExe = "ACU.exe"
$SteamFolderName = "Assassin's Creed Unity"
$EnvVarName = "ASSASSINS_CREED_UNITY_PATH"
# Ubisoft Connect product IDs for ACU. 720 is the base game registration
# on the current Ubisoft Connect client; older IDs also linger on accounts
# that migrated from Uplay. Check them all and pick whichever is installed.
$UbisoftProductIds = @("720", "526", "4915", "4917")

Write-Host "Searching for Assassin's Creed Unity..." -ForegroundColor Cyan

if ($env:ASSASSINS_CREED_UNITY_PATH -and (Test-Path (Join-Path $env:ASSASSINS_CREED_UNITY_PATH $GameExe))) {
    Write-Host "Found via ${EnvVarName}: $env:ASSASSINS_CREED_UNITY_PATH" -ForegroundColor Green
    exit 0
}

# Ubisoft Connect (primary - this is the Ubisoft Connect version)
foreach ($productId in $UbisoftProductIds) {
    $key = "HKLM:\SOFTWARE\WOW6432Node\Ubisoft\Launcher\Installs\$productId"
    $installDir = (Get-ItemProperty -Path $key -Name "InstallDir" -ErrorAction SilentlyContinue).InstallDir
    if ($installDir -and (Test-Path (Join-Path $installDir $GameExe))) {
        Write-Host "Found via Ubisoft Connect (product $productId): $installDir" -ForegroundColor Green
        exit 0
    }
}

# Steam (ACU also ships on Steam)
$steamPath = (Get-ItemProperty -Path "HKLM:\SOFTWARE\WOW6432Node\Valve\Steam" -Name InstallPath -ErrorAction SilentlyContinue).InstallPath
if (-not $steamPath) {
    $steamPath = (Get-ItemProperty -Path "HKLM:\SOFTWARE\Valve\Steam" -Name InstallPath -ErrorAction SilentlyContinue).InstallPath
}
if ($steamPath) {
    $candidate = Join-Path (Join-Path (Join-Path $steamPath "steamapps") "common") $SteamFolderName
    if (Test-Path (Join-Path $candidate $GameExe)) {
        Write-Host "Found via Steam: $candidate" -ForegroundColor Green
        exit 0
    }
    $vdf = Join-Path (Join-Path $steamPath "steamapps") "libraryfolders.vdf"
    if (Test-Path $vdf) {
        $content = Get-Content $vdf -Raw
        $paths = [regex]::Matches($content, '"path"\s+"([^"]+)"') | ForEach-Object { $_.Groups[1].Value -replace '\\\\', '\' }
        foreach ($lib in $paths) {
            $candidate = Join-Path (Join-Path (Join-Path $lib "steamapps") "common") $SteamFolderName
            if (Test-Path (Join-Path $candidate $GameExe)) {
                Write-Host "Found via Steam library: $candidate" -ForegroundColor Green
                exit 0
            }
        }
    }
}

# Common Ubisoft launcher install paths (manual / non-registry installs)
$candidates = @(
    "C:\Program Files (x86)\Ubisoft\Ubisoft Game Launcher\games\Assassin's Creed Unity",
    "C:\Program Files\Ubisoft\Ubisoft Game Launcher\games\Assassin's Creed Unity",
    "D:\Ubisoft\Ubisoft Game Launcher\games\Assassin's Creed Unity",
    "D:\Games\Ubisoft\Assassin's Creed Unity"
)
foreach ($p in $candidates) {
    if (Test-Path (Join-Path $p $GameExe)) {
        Write-Host "Found via common Ubisoft path: $p" -ForegroundColor Green
        exit 0
    }
}

Write-Host "Assassin's Creed Unity not found." -ForegroundColor Red
Write-Host "Set $EnvVarName environment variable to your game folder." -ForegroundColor Yellow
exit 1
