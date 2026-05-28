@echo off
:: ============================================
:: Assassin's Creed Unity - Uninstall
:: ============================================
:: Thin wrapper - uninstall body lives in cameraunlock-core/scripts/uninstall-body.cmd
:: (one body, framework-aware via FRAMEWORK_TYPE).

:: --- CONFIG BLOCK ---
set "GAME_ID=assassins-creed-unity"
set "MOD_DISPLAY_NAME=Assassin's Creed Unity Head Tracking"
set "MOD_DLLS=AssassinsCreedUnityHeadTracking.asi HeadTracking.ini"
set "MOD_INTERNAL_NAME=AssassinsCreedUnityHeadTracking"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=ASILoader"
set "LEGACY_DLLS="

:: --- Loader-specific config (leave the ones that don't apply blank) ---
:: MonoCecil: used to find + restore the original Assembly-CSharp.dll.
set "MANAGED_SUBFOLDER="
set "ASSEMBLY_DLL="
:: MonoCecil: extra files to also remove from MANAGED_SUBFOLDER (config/log
:: files left behind by the mod itself).
set "MANAGED_EXTRAS="
:: ASILoader: filename the ASI DLL was renamed to. Defaults to winmm.dll.
set "ASI_LOADER_NAME=dinput8.dll"
:: --- END CONFIG BLOCK ---

set "WRAPPER_DIR=%~dp0"
set "_BODY=%WRAPPER_DIR%shared\uninstall-body.cmd"
if not exist "%_BODY%" set "_BODY=%WRAPPER_DIR%..\cameraunlock-core\scripts\uninstall-body.cmd"
if not exist "%_BODY%" (
    echo ERROR: uninstall-body.cmd not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo If this is the dev tree, run: git submodule update --init --recursive
    exit /b 1
)
call "%_BODY%" %*
exit /b %errorlevel%