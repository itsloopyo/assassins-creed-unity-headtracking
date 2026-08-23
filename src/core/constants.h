#pragma once

#include <cstdint>

namespace ACUHT {

inline constexpr const char* ACUHT_VERSION = "0.0.0";
inline constexpr const char* ACUHT_MOD_NAME = "Assassin's Creed Unity Head Tracking";

inline constexpr const char* ACU_GAME_EXE = "ACU.exe";

inline constexpr uint16_t ACUHT_DEFAULT_UDP_PORT = 4242;

// Default hotkey virtual key codes (nav cluster)
inline constexpr int DEFAULT_TOGGLE_KEY          = 0x23;  // VK_END
inline constexpr int DEFAULT_POSITION_TOGGLE_KEY = 0x21;  // VK_PRIOR (Page Up)
inline constexpr int DEFAULT_YAW_MODE_KEY        = 0x22;  // VK_NEXT  (Page Down)

// Chord alternatives (Ctrl+Shift+<letter>, Y/U/G/H/J cluster)
inline constexpr int CHORD_TOGGLE_KEY   = 0x59;  // Y
inline constexpr int CHORD_POSITION_KEY = 0x47;  // G
inline constexpr int CHORD_YAWMODE_KEY  = 0x48;  // H
inline constexpr int CHORD_CULLGUARD_KEY = 0x4A; // J - toggle NPC cull guard band

// Debug-only: arm a one-shot in-memory frustum scan (Ctrl+Shift+U). Compiled
// in only for ACUHT_DEBUG_FRUSTUM_SCAN builds; see camera_hook.cpp.
inline constexpr int CHORD_DEBUG_SCAN_KEY = 0x55;  // U

} // namespace ACUHT
