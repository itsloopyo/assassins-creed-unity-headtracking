$ErrorActionPreference = "Stop"
$exe = "C:\Program Files (x86)\Ubisoft\Ubisoft Game Launcher\games\Assassin's Creed Unity\ACU.exe"
$bytes = [System.IO.File]::ReadAllBytes($exe)
$text = [System.Text.Encoding]::ASCII.GetString($bytes)
$candidates = @('dinput8.dll','version.dll','winmm.dll','dxgi.dll','d3d11.dll',
                'wininet.dll','dbghelp.dll','xinput1_3.dll','xinput9_1_0.dll',
                'xlive.dll','bink2w64.dll','vorbisfile.dll','msacm32.dll',
                'imm32.dll','d3d9.dll','dinput.dll','dsound.dll')
Write-Host "Searching ACU.exe for proxy-DLL candidate names..." -ForegroundColor Cyan
foreach ($c in $candidates) {
    if ($text.ToLowerInvariant().Contains($c.ToLowerInvariant())) {
        Write-Host "  FOUND: $c" -ForegroundColor Green
    }
}
