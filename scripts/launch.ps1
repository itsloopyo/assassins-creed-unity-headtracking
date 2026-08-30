$ErrorActionPreference = "Stop"

$scriptDir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir

Import-Module (Join-Path $projectRoot "cameraunlock-core\powershell\GamePathDetection.psm1") -Force

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

$gameId   = 'assassins-creed-unity'
$config   = Get-GameConfig -GameId $gameId
$gamePath = Find-GamePath -GameId $gameId
if (-not $gamePath) {
    Write-GameNotFoundError -GameName $config.DisplayName -EnvVar $config.EnvVar -SteamFolder $config.SteamFolder
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

$acuExe = Join-Path $gamePath $config.Executable
Write-Host "Launching: $acuExe" -ForegroundColor Green
Start-Process -FilePath $acuExe -WorkingDirectory $gamePath
