#pragma once

#include <cstdint>

namespace ACUHT {

struct Config {
    // Network
    uint16_t udpPort = ACUHT_DEFAULT_UDP_PORT;

    // Sensitivity
    float yawMultiplier   = 1.0f;
    float pitchMultiplier = 1.0f;
    float rollMultiplier  = 1.0f;

    // ACU follows the player's yaw via mouse and reads its camera quat with
    // an axis convention that comes out mirrored relative to OpenTrack's
    // tracker yaw. invertYaw default = true.
    bool invertYaw   = true;
    bool invertPitch = false;
    bool invertRoll  = false;

    // Smoothing is picked per connection from the packet source address: a
    // tracker on this machine (loopback) uses localSmoothing, a remote network
    // device uses remoteSmoothing. Both cover rotation and position.
    float localSmoothing = 0.0f;
    float remoteSmoothing = 0.15f;

    // Hotkeys (nav cluster)
    int toggleKey          = DEFAULT_TOGGLE_KEY;
    int positionToggleKey  = DEFAULT_POSITION_TOGGLE_KEY;
    int yawModeKey         = DEFAULT_YAW_MODE_KEY;

    // Chord alternatives (Ctrl+Shift+<letter>)
    int chordToggleKey   = CHORD_TOGGLE_KEY;
    int chordPositionKey = CHORD_POSITION_KEY;
    int chordYawModeKey  = CHORD_YAWMODE_KEY;

    // Position (6DOF). ACU's third-person follow camera sits close to Arno's
    // back, so the defaults here are conservative - increase only if you
    // find you hit the limit too quickly.
    float positionSensitivityX = 1.0f;
    float positionSensitivityY = 1.0f;
    float positionSensitivityZ = 1.0f;
    float positionLimitX = 0.20f;
    float positionLimitY = 0.15f;
    float positionLimitZ = 0.25f;
    float positionLimitZBack = 0.05f;
    bool positionInvertX = true;
    bool positionInvertY = false;
    bool positionInvertZ = false;
    bool positionEnabled = true;

    // General
    bool autoEnable = true;
    bool worldSpaceYaw = true;  // horizon-locked yaw, best for third-person
    // Off by default. Turning it on adds the camera-discovery, vtable-swap and
    // cull-frustum diagnostics that are only useful when reporting a problem.
    bool verboseLogging = false;

    bool cullGuardEnabled = true;
    float cullGuardBiasMeters = 500.0f;

    bool Load(const char* path);
    bool Save(const char* path) const;
    void SetDefaults();
    void Validate();
};

} // namespace ACUHT
