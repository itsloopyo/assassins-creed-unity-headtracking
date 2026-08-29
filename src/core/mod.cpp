#include "pch.h"
#include "mod.h"
#include "logger.h"
#include "path_utils.h"
#include "hooks/input_hook.h"
#include "hooks/camera_hook.h"

#include <cameraunlock/math/smoothing_utils.h>
#include <cameraunlock/time/qpc_clock.h>

namespace ACUHT {

// Ignore GetProcessedRotation calls within this window - the camera hook
// may fire multiple times per frame (e.g. reflection, main view) and we
// only want one pass through the pipeline per frame.
constexpr uint64_t kRotationCacheThresholdUs = 1000;

Mod& Mod::Instance() {
    static Mod instance;
    return instance;
}

bool Mod::Initialize() {
    if (m_initialized.load()) return true;

    Logger::Instance().Info("%s v%s initializing...", ACUHT_MOD_NAME, ACUHT_VERSION);

    if (!LoadConfig()) {
        Logger::Instance().Warning("Using default configuration");
    }

    // Only ever raises verbosity: a Debug build stays verbose whatever the INI
    // says, which is what a Debug build is for.
    if (m_config.verboseLogging) Logger::Instance().SetMinLevel(LogLevel::Debug);

    cameraunlock::SensitivitySettings sensitivity;
    sensitivity.yaw   = m_config.yawMultiplier;
    sensitivity.pitch = m_config.pitchMultiplier;
    sensitivity.roll  = m_config.rollMultiplier;
    sensitivity.invert_yaw   = m_config.invertYaw;
    sensitivity.invert_pitch = m_config.invertPitch;
    sensitivity.invert_roll  = m_config.invertRoll;
    m_session.GetProcessor().SetSensitivity(sensitivity);

    Logger::Instance().Info(
        "Sensitivity: yaw=%.2f pitch=%.2f roll=%.2f, smoothing local=%.2f remote=%.2f",
        sensitivity.yaw, sensitivity.pitch, sensitivity.roll,
        m_config.localSmoothing, m_config.remoteSmoothing);

    m_session.SetMode(m_config.positionEnabled
                          ? cameraunlock::TrackingMode::RotationAndPosition
                          : cameraunlock::TrackingMode::RotationOnly);
    m_worldSpaceYaw = m_config.worldSpaceYaw;

    // Position carries both smoothing values in the slot the single one used to
    // hold, so position and rotation share the connection-selected value.
    cameraunlock::PositionSettings posSettings = cameraunlock::PositionSettings::Symmetric(
        m_config.positionSensitivityX, m_config.positionSensitivityY, m_config.positionSensitivityZ,
        m_config.positionLimitX, m_config.positionLimitY, m_config.positionLimitZ,
        m_config.positionLimitZBack, m_config.localSmoothing, m_config.remoteSmoothing,
        m_config.positionInvertX, m_config.positionInvertY, m_config.positionInvertZ);
    m_session.GetPositionProcessor().SetSettings(posSettings);

    // Both values go to rotation and position; the session picks one per
    // connection from the address the packets arrive from.
    static_assert(cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver>::kHasRemoteConnection,
                  "receiver must classify connection locality, or smoothing "
                  "silently stays on the local parameter forever");
    m_session.SetLocalSmoothing(m_config.localSmoothing);
    m_session.SetRemoteSmoothing(m_config.remoteSmoothing);
    m_isRemoteConnection = !m_udpReceiver.IsRemoteConnection();  // force the first log

    if (!InitializeHooks()) {
        Logger::Instance().Warning("Some hooks failed - mod may have limited functionality");
    }

    m_udpReceiver.SetLog([](const std::string& msg) {
        Logger::Instance().Info("%s", msg.c_str());
    });
    if (m_udpReceiver.Start(m_config.udpPort)) {
        Logger::Instance().Info("UDP receiver started on port %d", m_config.udpPort);
    } else {
        Logger::Instance().Info("UDP receiver could not bind port %d yet; retrying in background",
                                m_config.udpPort);
    }

    if (m_config.autoEnable) {
        m_enabled.store(true);
        SetCameraHookEnabled(true);
        Logger::Instance().Info("Head tracking auto-enabled");
    } else {
        SetCameraHookEnabled(false);
    }

    m_initialized.store(true);
    Logger::Instance().Info("Initialization complete (camera=%s input=%s)",
                            m_cameraHookInstalled ? "OK" : "FAILED",
                            m_inputHookInstalled  ? "OK" : "FAILED");
    return true;
}

void Mod::Shutdown() {
    if (!m_initialized.load()) return;
    Logger::Instance().Info("Shutting down...");
    m_udpReceiver.Stop();
    ShutdownHooks();
    m_initialized.store(false);
    Logger::Instance().Info("Shutdown complete");
}

bool Mod::LoadConfig() {
    std::string configPath = GetModulePath("HeadTracking.ini");
    if (!m_config.Load(configPath.c_str())) {
        m_config.SetDefaults();
        m_config.Save(configPath.c_str());
        return false;
    }
    return true;
}

bool Mod::InitializeHooks() {
    m_cameraHookInstalled = InstallCameraHook();
    if (!m_cameraHookInstalled) {
        Logger::Instance().Warning(
            "Camera hook failed to start - tracking will not affect the view. "
            "Check HeadTracking.log for the specific reason (wrong process / version mismatch).");
    }

    m_inputHookInstalled = InstallInputHook();
    if (!m_inputHookInstalled) {
        Logger::Instance().Warning("Input hook failed - hotkeys won't work");
    }

    return m_inputHookInstalled;
}

void Mod::ShutdownHooks() {
    if (m_inputHookInstalled)  { RemoveInputHook();  m_inputHookInstalled = false; }
    if (m_cameraHookInstalled) { RemoveCameraHook(); m_cameraHookInstalled = false; }
}

void Mod::SetEnabled(bool enabled) {
    bool wasEnabled = m_enabled.exchange(enabled);
    if (wasEnabled != enabled) {
        SetCameraHookEnabled(enabled);
        Logger::Instance().Info("Head tracking %s", enabled ? "enabled" : "disabled");
    }
}

void Mod::Toggle() { SetEnabled(!m_enabled.load()); }

void Mod::CycleTrackingMode() {
    cameraunlock::TrackingMode mode = m_session.CycleMode();
    const char* name =
        mode == cameraunlock::TrackingMode::RotationAndPosition ? "6DOF (rotation + position)" :
        mode == cameraunlock::TrackingMode::RotationOnly        ? "3DOF rotational"            :
                                                                  "3DOF positional";
    Logger::Instance().Info("Tracking mode: %s", name);
}

void Mod::ToggleYawMode() {
    m_worldSpaceYaw = !m_worldSpaceYaw;
    Logger::Instance().Info("Yaw mode: %s",
                            m_worldSpaceYaw ? "world-space (horizon-locked)" : "camera-local");
}

bool Mod::GetProcessedRotation(float& yaw, float& pitch, float& roll) {
    uint64_t now = cameraunlock::time::QpcNowMicros();
    if (m_lastProcessTime == 0 || (now - m_lastProcessTime) >= kRotationCacheThresholdUs) {
        m_lastProcessTime = now;
        // The session re-reads the receiver's locality every Update, so a
        // tracker swap (local OpenTrack <-> phone on WiFi) picks up the other
        // smoothing parameter without a restart. This only reports the change.
        m_session.Update(m_frameClock.Tick());
        LogConnectionLocality();
    }
    return m_session.GetRotation(yaw, pitch, roll);
}

void Mod::LogConnectionLocality() {
    const bool isRemote = m_session.IsRemoteConnection();
    if (isRemote == m_isRemoteConnection) return;
    m_isRemoteConnection = isRemote;

    const double effective = cameraunlock::math::GetEffectiveSmoothing(
        m_config.localSmoothing, m_config.remoteSmoothing, isRemote);
    Logger::Instance().Info("Tracker connection is %s; smoothing=%.2f",
                            isRemote ? "remote" : "local", effective);
}

bool Mod::GetPositionOffset(float& x, float& y, float& z) {
    return m_session.GetPositionOffset(x, y, z);
}

} // namespace ACUHT
