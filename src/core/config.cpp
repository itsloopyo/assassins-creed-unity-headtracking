#include "pch.h"
#include "config.h"
#include "logger.h"
#include <cameraunlock/config/ini_reader.h>
#include <algorithm>

namespace ACUHT {

void Config::SetDefaults() {
    *this = Config{};
}

void Config::Validate() {
    yawMultiplier   = std::clamp(yawMultiplier,   0.1f, 5.0f);
    pitchMultiplier = std::clamp(pitchMultiplier, 0.1f, 5.0f);
    rollMultiplier  = std::clamp(rollMultiplier,  0.0f, 2.0f);
    smoothing       = std::clamp(smoothing,       0.0f, 1.0f);

    positionSensitivityX = std::clamp(positionSensitivityX, 0.1f, 10.0f);
    positionSensitivityY = std::clamp(positionSensitivityY, 0.1f, 10.0f);
    positionSensitivityZ = std::clamp(positionSensitivityZ, 0.1f, 10.0f);

    positionLimitX     = std::clamp(positionLimitX,     0.01f, 2.0f);
    positionLimitY     = std::clamp(positionLimitY,     0.01f, 2.0f);
    positionLimitZ     = std::clamp(positionLimitZ,     0.01f, 2.0f);
    positionLimitZBack = std::clamp(positionLimitZBack, 0.01f, 2.0f);
    positionSmoothing  = std::clamp(positionSmoothing,  0.0f,  0.99f);
    cullGuardBiasMeters = std::clamp(cullGuardBiasMeters, 0.0f, 2000.0f);

    if (udpPort < 1024) {
        Logger::Instance().Warning("UDP port %d is reserved, using default %d",
                                   udpPort, ACUHT_DEFAULT_UDP_PORT);
        udpPort = ACUHT_DEFAULT_UDP_PORT;
    }
}

bool Config::Load(const char* path) {
    SetDefaults();

    cameraunlock::IniReader ini;
    if (!ini.Open(path)) {
        Logger::Instance().Warning("Could not load config from %s, using defaults", path);
        return false;
    }

    int rawPort = ini.ReadInt("Network", "UDPPort", udpPort);
    if (rawPort < 0 || rawPort > 65535) {
        Logger::Instance().Warning(
            "UDP port %d in config is out of range [0, 65535], using default %d",
            rawPort, ACUHT_DEFAULT_UDP_PORT);
        udpPort = ACUHT_DEFAULT_UDP_PORT;
    } else {
        udpPort = static_cast<uint16_t>(rawPort);
    }

    yawMultiplier   = ini.ReadFloat("Sensitivity", "YawMultiplier",   yawMultiplier);
    pitchMultiplier = ini.ReadFloat("Sensitivity", "PitchMultiplier", pitchMultiplier);
    rollMultiplier  = ini.ReadFloat("Sensitivity", "RollMultiplier",  rollMultiplier);
    smoothing       = ini.ReadFloat("Sensitivity", "Smoothing",       smoothing);
    invertYaw       = ini.ReadBool( "Sensitivity", "InvertYaw",       invertYaw);
    invertPitch     = ini.ReadBool( "Sensitivity", "InvertPitch",     invertPitch);
    invertRoll      = ini.ReadBool( "Sensitivity", "InvertRoll",      invertRoll);

    toggleKey         = ini.ReadHex("Hotkeys", "ToggleKey",         toggleKey);
    recenterKey       = ini.ReadHex("Hotkeys", "RecenterKey",       recenterKey);
    positionToggleKey = ini.ReadHex("Hotkeys", "PositionToggleKey", positionToggleKey);
    yawModeKey        = ini.ReadHex("Hotkeys", "YawModeKey",        yawModeKey);
    chordToggleKey    = ini.ReadHex("Hotkeys", "ChordToggleKey",    chordToggleKey);
    chordRecenterKey  = ini.ReadHex("Hotkeys", "ChordRecenterKey",  chordRecenterKey);
    chordPositionKey  = ini.ReadHex("Hotkeys", "ChordPositionKey",  chordPositionKey);
    chordYawModeKey   = ini.ReadHex("Hotkeys", "ChordYawModeKey",   chordYawModeKey);

    positionSensitivityX = ini.ReadFloat("Position", "SensitivityX", positionSensitivityX);
    positionSensitivityY = ini.ReadFloat("Position", "SensitivityY", positionSensitivityY);
    positionSensitivityZ = ini.ReadFloat("Position", "SensitivityZ", positionSensitivityZ);
    positionLimitX       = ini.ReadFloat("Position", "LimitX",       positionLimitX);
    positionLimitY       = ini.ReadFloat("Position", "LimitY",       positionLimitY);
    positionLimitZ       = ini.ReadFloat("Position", "LimitZ",       positionLimitZ);
    positionLimitZBack   = ini.ReadFloat("Position", "LimitZBack",   positionLimitZBack);
    positionSmoothing    = ini.ReadFloat("Position", "Smoothing",    positionSmoothing);
    positionInvertX      = ini.ReadBool( "Position", "InvertX",      positionInvertX);
    positionInvertY      = ini.ReadBool( "Position", "InvertY",      positionInvertY);
    positionInvertZ      = ini.ReadBool( "Position", "InvertZ",      positionInvertZ);
    positionEnabled      = ini.ReadBool( "Position", "Enabled",      positionEnabled);

    autoEnable         = ini.ReadBool("General", "AutoEnable",         autoEnable);
    worldSpaceYaw      = ini.ReadBool("General", "WorldSpaceYaw",      worldSpaceYaw);
    cameraHookLogging  = ini.ReadBool("General", "CameraHookLogging",  cameraHookLogging);

    cullGuardEnabled = ini.ReadBool( "Culling", "GuardEnabled",    cullGuardEnabled);
    cullGuardBiasMeters = ini.ReadFloat("Culling", "GuardBiasMeters", cullGuardBiasMeters);

    Validate();
    Logger::Instance().Info("Config loaded from %s", path);
    return true;
}

bool Config::Save(const char* path) const {
    std::ofstream file(path);
    if (!file.is_open()) {
        Logger::Instance().Error("Failed to save config to %s", path);
        return false;
    }

    file << "; Assassin's Creed Unity Head Tracking Configuration\n";
    file << "; Delete this file to reset to defaults\n\n";

    file << "[Network]\n";
    file << "; UDP port for OpenTrack data (default: 4242)\n";
    file << "UDPPort=" << udpPort << "\n\n";

    file << "[Sensitivity]\n";
    file << "YawMultiplier=" << yawMultiplier << "\n";
    file << "PitchMultiplier=" << pitchMultiplier << "\n";
    file << "RollMultiplier=" << rollMultiplier << "\n";
    file << "; 0.0 = minimum (baseline 0.15 floor applied internally), 1.0 = heavy\n";
    file << "Smoothing=" << smoothing << "\n";
    file << "; ACU's camera quaternion convention is mirrored vs OpenTrack yaw.\n";
    file << "InvertYaw="   << (invertYaw   ? "true" : "false") << "\n";
    file << "InvertPitch=" << (invertPitch ? "true" : "false") << "\n";
    file << "InvertRoll="  << (invertRoll  ? "true" : "false") << "\n\n";

    file << "[Position]\n";
    file << "; Position tracking sensitivity (1.0 = 1:1)\n";
    file << "SensitivityX=" << positionSensitivityX << "\n";
    file << "SensitivityY=" << positionSensitivityY << "\n";
    file << "SensitivityZ=" << positionSensitivityZ << "\n";
    file << "; Limits in meters. ACU's follow camera sits close to Arno -\n";
    file << "; keep forward/backward limits conservative to avoid clipping.\n";
    file << "LimitX=" << positionLimitX << "\n";
    file << "LimitY=" << positionLimitY << "\n";
    file << "LimitZ=" << positionLimitZ << "\n";
    file << "LimitZBack=" << positionLimitZBack << "\n";
    file << "Smoothing=" << positionSmoothing << "\n";
    file << "InvertX=" << (positionInvertX ? "true" : "false") << "\n";
    file << "InvertY=" << (positionInvertY ? "true" : "false") << "\n";
    file << "InvertZ=" << (positionInvertZ ? "true" : "false") << "\n";
    file << "Enabled=" << (positionEnabled ? "true" : "false") << "\n\n";

    file << "[Hotkeys]\n";
    file << "; Virtual key codes in hex. Nav-cluster defaults:\n";
    file << "ToggleKey=0x"         << std::hex << toggleKey         << "    ; End\n";
    file << "RecenterKey=0x"       << std::hex << recenterKey       << "    ; Home\n";
    file << "PositionToggleKey=0x" << std::hex << positionToggleKey << "    ; Page Up\n";
    file << "YawModeKey=0x"        << std::hex << yawModeKey        << "    ; Page Down\n";
    file << std::dec;
    file << "; Chord alternatives for laptops without a nav cluster:\n";
    file << "; Ctrl+Shift+<letter> (letter VK in hex below)\n";
    file << "ChordToggleKey=0x"   << std::hex << chordToggleKey   << "    ; Y\n";
    file << "ChordRecenterKey=0x" << std::hex << chordRecenterKey << "    ; T\n";
    file << "ChordPositionKey=0x" << std::hex << chordPositionKey << "    ; G\n";
    file << "ChordYawModeKey=0x"  << std::hex << chordYawModeKey  << "    ; H\n\n";
    file << std::dec;

    file << "[General]\n";
    file << "AutoEnable=" << (autoEnable ? "true" : "false") << "\n";
    file << "; Horizon-locked yaw (true) is best for third-person.\n";
    file << "WorldSpaceYaw=" << (worldSpaceYaw ? "true" : "false") << "\n";
    file << "; Set false once camera offsets are confirmed - trims log noise.\n";
    file << "CameraHookLogging=" << (cameraHookLogging ? "true" : "false") << "\n";
    file << "\n[Culling]\n";
    file << "; Widens actor visibility culling so head turns do not reveal empty crowd edges.\n";
    file << "GuardEnabled=" << (cullGuardEnabled ? "true" : "false") << "\n";
    file << "; Side-plane outward bias in metres. Render FOV is unchanged.\n";
    file << "GuardBiasMeters=" << cullGuardBiasMeters << "\n";

    file.close();
    Logger::Instance().Info("Config saved to %s", path);
    return true;
}

} // namespace ACUHT
