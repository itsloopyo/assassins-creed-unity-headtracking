$ErrorActionPreference = "Stop"

$GameExe = "ACU.exe"
$SteamFolderName = "Assassin's Creed Unity"
$UbisoftProductIds = @("720", "526", "4915", "4917")

function Find-GamePath {
    if ($env:ASSASSINS_CREED_UNITY_PATH -and (Test-Path (Join-Path $env:ASSASSINS_CREED_UNITY_PATH $GameExe))) {
        return $env:ASSASSINS_CREED_UNITY_PATH.TrimEnd('\', '/')
    }
    foreach ($productId in $UbisoftProductIds) {
        $key = "HKLM:\SOFTWARE\WOW6432Node\Ubisoft\Launcher\Installs\$productId"
        $installDir = (Get-ItemProperty -Path $key -Name "InstallDir" -ErrorAction SilentlyContinue).InstallDir
        if ($installDir -and (Test-Path (Join-Path $installDir $GameExe))) {
            return $installDir.TrimEnd('\', '/')
        }
    }
    $steamPath = (Get-ItemProperty -Path "HKLM:\SOFTWARE\WOW6432Node\Valve\Steam" -Name InstallPath -ErrorAction SilentlyContinue).InstallPath
    if (-not $steamPath) {
        $steamPath = (Get-ItemProperty -Path "HKLM:\SOFTWARE\Valve\Steam" -Name InstallPath -ErrorAction SilentlyContinue).InstallPath
    }
    if ($steamPath) {
        $candidate = Join-Path (Join-Path (Join-Path $steamPath "steamapps") "common") $SteamFolderName
        if (Test-Path (Join-Path $candidate $GameExe)) { return $candidate }
    }
    return $null
}

function Find-UbisoftConnectExe {
    $key = "HKLM:\SOFTWARE\WOW6432Node\Ubisoft\Launcher"
    $installDir = (Get-ItemProperty -Path $key -Name "InstallDir" -ErrorAction SilentlyContinue).InstallDir
    if ($installDir) {
        $candidate = Join-Path $installDir "upc.exe"
        if (Test-Path $candidate) { return $candidate }
        $candidate = Join-Path $installDir "UbisoftConnect.exe"
        if (Test-Path $candidate) { return $candidate }
    }
    foreach ($p in @(
        "C:\Program Files (x86)\Ubisoft\Ubisoft Game Launcher\upc.exe",
        "C:\Program Files\Ubisoft\Ubisoft Game Launcher\upc.exe",
        "C:\Program Files (x86)\Ubisoft\Ubisoft Game Launcher\UbisoftConnect.exe"
    )) {
        if (Test-Path $p) { return $p }
    }
    return $null
}

$gamePath = Find-GamePath
if (-not $gamePath) {
    Write-Error "Could not find Assassin's Creed Unity. Set ASSASSINS_CREED_UNITY_PATH."
    exit 1
}

if (Get-Process -Name "ACU" -ErrorAction SilentlyContinue) {
    Write-Host "ACU.exe is already running." -ForegroundColor Yellow
    exit 0
}

$upcRunning = [bool](Get-Process -Name "upc","UbisoftConnect" -ErrorAction SilentlyContinue)
if (-not $upcRunning) {
    $upcExe = Find-UbisoftConnectExe
    if (-not $upcExe) {
        Write-Error "Ubisoft Connect not installed or not found. ACU needs it running for the ownership check."
        exit 1
    }
    Write-Host "Starting Ubisoft Connect (minimized): $upcExe" -ForegroundColor Cyan
    Start-Process -FilePath $upcExe -WindowStyle Minimized | Out-Null

    $deadline = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $deadline) {
        if (Get-Process -Name "upc","UbisoftConnect" -ErrorAction SilentlyContinue) { break }
        Start-Sleep -Milliseconds 250
    }
    if (-not (Get-Process -Name "upc","UbisoftConnect" -ErrorAction SilentlyContinue)) {
        Write-Error "Ubisoft Connect failed to start within 30s."
        exit 1
    }
    Start-Sleep -Seconds 2
}

$acuExe = Join-Path $gamePath $GameExe
Write-Host "Launching: $acuExe" -ForegroundColor Green
Start-Process -FilePath $acuExe -WorkingDirectory $gamePath
