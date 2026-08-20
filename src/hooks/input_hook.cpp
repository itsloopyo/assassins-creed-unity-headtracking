#include "pch.h"
#include "input_hook.h"
#include "core/mod.h"
#include "core/logger.h"
#include "hooks/camera_hook.h"

#include <cameraunlock/input/hotkey_poller.h>
#include <cameraunlock/input/chord_hotkeys.h>

namespace ACUHT {

namespace {
cameraunlock::input::HotkeyPoller g_poller;
}

bool InstallInputHook() {
    if (g_poller.IsRunning()) return true;

    using cameraunlock::input::ChordGuarded;

    const Config& config = Mod::Instance().GetConfig();

    // Nav-cluster keys.
    g_poller.SetToggleKey(config.toggleKey, [] { Mod::Instance().Toggle(); });
    g_poller.AddHotkey(config.positionToggleKey, [] { Mod::Instance().CycleTrackingMode(); });
    g_poller.AddHotkey(config.yawModeKey, [] { Mod::Instance().ToggleYawMode(); });

    // Chord alternatives (Ctrl+Shift+<letter>); ChordGuarded gates each
    // action on the modifier state.
    g_poller.AddHotkey(config.chordToggleKey,   ChordGuarded([] { Mod::Instance().Toggle(); }));
    g_poller.AddHotkey(config.chordPositionKey, ChordGuarded([] { Mod::Instance().CycleTrackingMode(); }));
    g_poller.AddHotkey(config.chordYawModeKey,  ChordGuarded([] { Mod::Instance().ToggleYawMode(); }));

    // Cull guard has no nav-cluster default (those four are taken); chord-only.
    g_poller.AddHotkey(CHORD_CULLGUARD_KEY, ChordGuarded([] { ToggleCullGuard(); }));

#ifdef ACUHT_DEBUG_FRUSTUM_SCAN
    g_poller.AddHotkey(CHORD_DEBUG_SCAN_KEY, ChordGuarded([] { DebugArmFrustumScan(); }));
#endif

    if (!g_poller.Start(16)) {
        Logger::Instance().Error("HotkeyPoller failed to start");
        return false;
    }

    Logger::Instance().Info("Input hook installed (nav + Ctrl+Shift chord bindings)");
    return true;
}

void RemoveInputHook() {
    if (!g_poller.IsRunning()) return;
    g_poller.Stop();
    Logger::Instance().Info("Input hook removed");
}

} // namespace ACUHT
