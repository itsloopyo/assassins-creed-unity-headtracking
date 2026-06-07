@echo off
:: ============================================
:: Assassin's Creed Unity - Install
:: ============================================
:: Thin wrapper - install body lives in cameraunlock-core/scripts/install-body-asi.cmd.

:: --- CONFIG BLOCK ---
set "GAME_ID=assassins-creed-unity"
set "MOD_DISPLAY_NAME=Assassin's Creed Unity Head Tracking"
set "MOD_DLLS=AssassinsCreedUnityHeadTracking.asi HeadTracking.ini"
set "MOD_INTERNAL_NAME=AssassinsCreedUnityHeadTracking"
set "MOD_VERSION=0.0.0"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=ASILoader"
set "ASI_LOADER_NAME=dinput8.dll"
set "MOD_CONTROLS=Controls:&echo   Home/Ctrl+Shift+T - Recenter head tracking&echo   End/Ctrl+Shift+Y  - Toggle head tracking on/off&echo   PgUp/Ctrl+Shift+G - Toggle position tracking&echo   PgDn/Ctrl+Shift+H - Toggle world/local yaw"
:: ASI_LOADER_NAME is the filename the ASI DLL is renamed to. DL2 and most
:: modern games use winmm.dll; older ones use dinput8.dll or xinput1_3.dll.
:: vendor/ultimate-asi-loader/dinput8.dll is the bundled source; we copy it
:: to ASI_LOADER_NAME in EXE_DIR. Bump it via `pixi run update-deps`.
:: --- END CONFIG BLOCK ---

set "WRAPPER_DIR=%~dp0"
set "_BODY=%WRAPPER_DIR%shared\install-body-asi.cmd"
if not exist "%_BODY%" set "_BODY=%WRAPPER_DIR%..\cameraunlock-core\scripts\install-body-asi.cmd"
if not exist "%_BODY%" (
    echo ERROR: install-body-asi.cmd not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo If this is the dev tree, run: git submodule update --init --recursive
    exit /b 1
)
call "%_BODY%" %*
exit /b %errorlevel%