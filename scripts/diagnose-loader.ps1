# Diagnoses whether the Ultimate ASI Loader proxy and our .asi actually load
# into ACU.exe. Run this before launching the game from the Xbox app.
# It waits for ACU.exe to start, captures one snapshot, prints to console,
# and writes the same output to %TEMP%\acu-loader-diagnose.txt.

$ErrorActionPreference = 'Continue'
$out = Join-Path $env:TEMP 'acu-loader-diagnose.txt'
"=== ACU loader diagnosis $(Get-Date -Format 'u') ===" | Tee-Object -FilePath $out

Write-Host "Waiting for ACU.exe... launch the game now."
while (-not (Get-Process -Name ACU -ErrorAction SilentlyContinue)) {
    Start-Sleep -Milliseconds 250
}
# Give Windows a moment to finish populating the module list.
Start-Sleep -Seconds 2

$p = Get-Process -Name ACU | Select-Object -First 1
$pp = Get-CimInstance Win32_Process -Filter "ProcessId=$($p.Id)"
$parent = Get-CimInstance Win32_Process -Filter "ProcessId=$($pp.ParentProcessId)" -ErrorAction SilentlyContinue

"PID:        $($p.Id)" | Tee-Object -FilePath $out -Append
"Path:       $($p.Path)" | Tee-Object -FilePath $out -Append
"Started:    $($p.StartTime)" | Tee-Object -FilePath $out -Append
"CmdLine:    $($pp.CommandLine)" | Tee-Object -FilePath $out -Append
"Parent:     $($parent.Name) [$($parent.ExecutablePath)]" | Tee-Object -FilePath $out -Append

"" | Tee-Object -FilePath $out -Append
"--- dinput8.dll resolution ---" | Tee-Object -FilePath $out -Append
$dinput = $p.Modules | Where-Object { $_.ModuleName -ieq 'dinput8.dll' }
if ($dinput) {
    "Loaded from: $($dinput.FileName)" | Tee-Object -FilePath $out -Append
    "Version:     $($dinput.FileVersionInfo.FileVersion)" | Tee-Object -FilePath $out -Append
    "Company:     $($dinput.FileVersionInfo.CompanyName)" | Tee-Object -FilePath $out -Append
    if ($dinput.FileName -like '*System32*' -or $dinput.FileName -like '*SysWOW64*') {
        "VERDICT:     SYSTEM dinput8 loaded -- our proxy was NOT picked up" | Tee-Object -FilePath $out -Append
    } else {
        "VERDICT:     local proxy loaded -- ASI loader is in the process" | Tee-Object -FilePath $out -Append
    }
} else {
    "dinput8.dll is NOT loaded into ACU.exe at all." | Tee-Object -FilePath $out -Append
}

"" | Tee-Object -FilePath $out -Append
"--- Our .asi ---" | Tee-Object -FilePath $out -Append
$asi = $p.Modules | Where-Object { $_.ModuleName -like 'AssassinsCreed*' -or $_.FileName -like '*HeadTracking*' }
if ($asi) {
    foreach ($m in $asi) { "Loaded: $($m.FileName)" | Tee-Object -FilePath $out -Append }
} else {
    "AssassinsCreedUnityHeadTracking.asi is NOT loaded into ACU.exe." | Tee-Object -FilePath $out -Append
}

"" | Tee-Object -FilePath $out -Append
"--- Process mitigation policy ---" | Tee-Object -FilePath $out -Append
try {
    Get-ProcessMitigation -Id $p.Id 2>&1 | Tee-Object -FilePath $out -Append
} catch {
    "Get-ProcessMitigation failed: $_" | Tee-Object -FilePath $out -Append
}

"" | Tee-Object -FilePath $out -Append
"--- Other proxy candidates already loaded (for choosing alternative) ---" | Tee-Object -FilePath $out -Append
$candidates = @('dxgi.dll','d3d11.dll','winmm.dll','version.dll','dsound.dll')
foreach ($c in $candidates) {
    $m = $p.Modules | Where-Object { $_.ModuleName -ieq $c }
    if ($m) { "$c -> $($m.FileName)" | Tee-Object -FilePath $out -Append }
}

"" | Tee-Object -FilePath $out -Append
"=== Done. Full output also at: $out ===" | Tee-Object -FilePath $out -Append
