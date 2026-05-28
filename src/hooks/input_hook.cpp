#include "pch.h"
#include "input_hook.h"
#include "core/mod.h"
#include "core/logger.h"
#include "hooks/camera_hook.h"

namespace ACUHT {

static std::thread g_inputThread;
static std::atomic<bool> g_stopFlag{false};
static std::atomic<bool> g_running{false};

struct KeyState {
    int primaryVk;
    int chordVk;
    bool primaryDown;
    bool chordDown;
};

static KeyState g_toggle    {DEFAULT_TOGGLE_KEY,          CHORD_TOGGLE_KEY,   false, false};
static KeyState g_recenter  {DEFAULT_RECENTER_KEY,        CHORD_RECENTER_KEY, false, false};
static KeyState g_position  {DEFAULT_POSITION_TOGGLE_KEY, CHORD_POSITION_KEY, false, false};
static KeyState g_yawMode   {DEFAULT_YAW_MODE_KEY,        CHORD_YAWMODE_KEY,  false, false};
// Cull guard has no nav-cluster default (those four are taken); chord-only.
// primaryVk 0 is not a real key, so GetAsyncKeyState never reports it pressed.
static KeyState g_cullGuard {0,                           CHORD_CULLGUARD_KEY, false, false};

// Returns true once on the rising edge of either the primary key (nav
// cluster) or the chord combo (Ctrl+Shift+<letter>). Using two separate
// latches per action so holding the primary doesn't starve the chord.
static bool EdgeTriggered(KeyState& state) {
    bool primary = (GetAsyncKeyState(state.primaryVk) & 0x8000) != 0;
    bool ctrl    = (GetAsyncKeyState(VK_CONTROL)      & 0x8000) != 0;
    bool shift   = (GetAsyncKeyState(VK_SHIFT)        & 0x8000) != 0;
    bool letter  = (GetAsyncKeyState(state.chordVk)   & 0x8000) != 0;
    bool chord   = ctrl && shift && letter;

    bool fired = false;
    if (primary && !state.primaryDown) fired = true;
    state.primaryDown = primary;

    if (chord && !state.chordDown) fired = true;
    state.chordDown = chord;

    return fired;
}

static void InputPollingThread() {
    while (!g_stopFlag.load(std::memory_order_relaxed)) {
        if (EdgeTriggered(g_toggle))   Mod::Instance().Toggle();
        if (EdgeTriggered(g_recenter)) Mod::Instance().Recenter();
        if (EdgeTriggered(g_position)) Mod::Instance().CycleTrackingMode();
        if (EdgeTriggered(g_yawMode))  Mod::Instance().ToggleYawMode();
        if (EdgeTriggered(g_cullGuard)) ToggleCullGuard();
#ifdef ACUHT_DEBUG_FRUSTUM_SCAN
        {
            static bool dbgDown = false;
            bool chord = (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
                         (GetAsyncKeyState(VK_SHIFT)   & 0x8000) &&
                         (GetAsyncKeyState(CHORD_DEBUG_SCAN_KEY) & 0x8000);
            if (chord && !dbgDown) DebugArmFrustumScan();
            dbgDown = chord;
        }
#endif
        Sleep(16);  // ~60Hz polling
    }
}

bool InstallInputHook() {
    if (g_running.load()) return true;

    const Config& config = Mod::Instance().GetConfig();
    g_toggle   = {config.toggleKey,         config.chordToggleKey,   false, false};
    g_recenter = {config.recenterKey,       config.chordRecenterKey, false, false};
    g_position = {config.positionToggleKey, config.chordPositionKey, false, false};
    g_yawMode  = {config.yawModeKey,        config.chordYawModeKey,  false, false};

    g_stopFlag.store(false);
    g_running.store(true);
    g_inputThread = std::thread(InputPollingThread);

    Logger::Instance().Info("Input hook installed (nav + Ctrl+Shift chord bindings)");
    return true;
}

void RemoveInputHook() {
    if (!g_running.load()) return;
    g_stopFlag.store(true);
    if (g_inputThread.joinable()) g_inputThread.join();
    g_running.store(false);
    Logger::Instance().Info("Input hook removed");
}

} // namespace ACUHT
