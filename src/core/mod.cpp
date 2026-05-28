#include "pch.h"
#include "mod.h"
#include "logger.h"
#include "path_utils.h"
#include "hooks/hook_manager.h"
#include "hooks/input_hook.h"
#include "hooks/camera_hook.h"

#include <cameraunlock/math/quat4.h>
#include <cameraunlock/math/angle_utils.h>

namespace ACUHT {

// Skip the first ~0.5s of samples before auto-recentering - initial
// tracker output is usually garbage as OpenTrack warms up.
constexpr int kStabilizationFrameCount = 30;

// Ignore GetProcessedRotation calls within this window - the camera hook
// may fire multiple times per frame (e.g. reflection, main view) and we
// only want one pass through the pipeline per frame.
constexpr uint64_t kRotationCacheThresholdUs = 1000;

static uint64_t GetTimeMicros() {
    static LARGE_INTEGER freq = {};
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<uint64_t>(now.QuadPart * 1000000 / freq.QuadPart);
}

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

    cameraunlock::SensitivitySettings sensitivity;
    sensitivity.yaw   = m_config.yawMultiplier;
    sensitivity.pitch = m_config.pitchMultiplier;
    sensitivity.roll  = m_config.rollMultiplier;
    m_processor.SetSensitivity(sensitivity);
    m_processor.SetSmoothing(m_config.smoothing);

    Logger::Instance().Info("Sensitivity: yaw=%.2f pitch=%.2f roll=%.2f, smoothing=%.2f",
                            sensitivity.yaw, sensitivity.pitch, sensitivity.roll,
                            m_config.smoothing);

    m_trackingMode = m_config.positionEnabled ? TrackingMode::SixDof
                                              : TrackingMode::RotationOnly;
    m_worldSpaceYaw = m_config.worldSpaceYaw;

    cameraunlock::PositionSettings posSettings(
        m_config.positionSensitivityX, m_config.positionSensitivityY, m_config.positionSensitivityZ,
        m_config.positionLimitX, m_config.positionLimitY, m_config.positionLimitZ,
        m_config.positionLimitZBack, m_config.positionSmoothing,
        m_config.positionInvertX, m_config.positionInvertY, m_config.positionInvertZ);
    m_positionProcessor.SetSettings(posSettings);

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
    if (!HookManager::Instance().Initialize()) {
        Logger::Instance().Error("MinHook initialization failed");
        return false;
    }

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
    HookManager::Instance().Shutdown();
}

void Mod::SetEnabled(bool enabled) {
    bool wasEnabled = m_enabled.exchange(enabled);
    if (wasEnabled != enabled) {
        SetCameraHookEnabled(enabled);
        Logger::Instance().Info("Head tracking %s", enabled ? "enabled" : "disabled");
    }
}

void Mod::Toggle() { SetEnabled(!m_enabled.load()); }

void Mod::Recenter() {
    m_udpReceiver.Recenter();
    m_processor.Reset();
    m_poseInterpolator.Reset();
    m_lastProcessTime = 0;

    float px, py, pz;
    if (m_udpReceiver.GetPosition(px, py, pz)) {
        cameraunlock::PositionData posCenter(px, py, pz);
        m_positionProcessor.SetCenter(posCenter);
    }
    m_positionInterpolator.Reset();

    Logger::Instance().Info("View recentered");
}

void Mod::CycleTrackingMode() {
    switch (m_trackingMode) {
        case TrackingMode::SixDof:       m_trackingMode = TrackingMode::RotationOnly; break;
        case TrackingMode::RotationOnly: m_trackingMode = TrackingMode::PositionOnly; break;
        case TrackingMode::PositionOnly: m_trackingMode = TrackingMode::SixDof;       break;
    }

    if (!IsPositionActive()) {
        m_positionProcessor.Reset();
        m_positionInterpolator.Reset();
    }

    const char* name =
        m_trackingMode == TrackingMode::SixDof       ? "6DOF (rotation + position)" :
        m_trackingMode == TrackingMode::RotationOnly ? "3DOF rotational"            :
                                                       "3DOF positional";
    Logger::Instance().Info("Tracking mode: %s", name);
}

void Mod::ToggleYawMode() {
    m_worldSpaceYaw = !m_worldSpaceYaw;
    Logger::Instance().Info("Yaw mode: %s",
                            m_worldSpaceYaw ? "world-space (horizon-locked)" : "camera-local");
}

bool Mod::GetProcessedRotation(float& yaw, float& pitch, float& roll) {
    uint64_t now = GetTimeMicros();
    if (m_lastProcessTime > 0 && (now - m_lastProcessTime) < kRotationCacheThresholdUs) {
        yaw = m_cachedYaw;
        pitch = m_cachedPitch;
        roll = m_cachedRoll;
        return m_cachedValid;
    }

    float rawYaw, rawPitch, rawRoll;
    if (!m_udpReceiver.GetRotation(rawYaw, rawPitch, rawRoll)) {
        m_lastProcessTime = now;
        m_cachedValid = false;
        return false;
    }

    if (!m_hasCentered) {
        m_stabilizationFrames++;
        if (m_stabilizationFrames >= kStabilizationFrameCount) {
            m_hasCentered = true;
            Recenter();
            Logger::Instance().Info("Auto-recentered after %d frames", m_stabilizationFrames);
        }
    }

    float deltaTime = 0.016f;
    if (m_lastProcessTime > 0) {
        deltaTime = (now - m_lastProcessTime) / 1000000.0f;
        if (deltaTime > 0.1f)    deltaTime = 0.1f;
        if (deltaTime < 0.0001f) deltaTime = 0.0001f;
    }
    m_lastProcessTime = now;
    m_lastDeltaTime = deltaTime;

    int64_t receiveTs = m_udpReceiver.GetLastReceiveTimestamp();
    bool isNewPacket = (receiveTs != m_lastReceiveTimestamp);
    m_lastReceiveTimestamp = receiveTs;

    bool isNewSample = isNewPacket &&
        (rawYaw != m_lastRawYaw || rawPitch != m_lastRawPitch || rawRoll != m_lastRawRoll);
    if (isNewPacket) {
        m_lastRawYaw = rawYaw;
        m_lastRawPitch = rawPitch;
        m_lastRawRoll = rawRoll;
    }

    cameraunlock::InterpolatedPose interpolated = m_poseInterpolator.Update(
        rawYaw, rawPitch, rawRoll, isNewSample, deltaTime);

    cameraunlock::TrackingPose processed = m_processor.Process(
        interpolated.yaw, interpolated.pitch, interpolated.roll, deltaTime);

    yaw   = m_config.invertYaw   ? -processed.yaw   : processed.yaw;
    pitch = m_config.invertPitch ? -processed.pitch : processed.pitch;
    roll  = m_config.invertRoll  ? -processed.roll  : processed.roll;

    m_cachedYaw = yaw;
    m_cachedPitch = pitch;
    m_cachedRoll = roll;
    m_cachedValid = true;
    return true;
}

bool Mod::GetPositionOffset(float& x, float& y, float& z) {
    if (!IsPositionActive()) {
        x = y = z = 0.0f;
        return false;
    }

    uint64_t now = GetTimeMicros();
    if (m_lastPositionProcessTime > 0 &&
        (now - m_lastPositionProcessTime) < kRotationCacheThresholdUs) {
        x = m_cachedPosX;
        y = m_cachedPosY;
        z = m_cachedPosZ;
        return m_cachedPosValid;
    }
    m_lastPositionProcessTime = now;

    float rawX, rawY, rawZ;
    if (!m_udpReceiver.GetPosition(rawX, rawY, rawZ)) {
        x = y = z = 0.0f;
        m_cachedPosValid = false;
        return false;
    }

    float deltaTime = m_lastDeltaTime;
    int64_t receiveTs = m_udpReceiver.GetLastReceiveTimestamp();
    cameraunlock::PositionData rawPos(rawX, rawY, rawZ, receiveTs);
    cameraunlock::PositionData interpolatedPos = m_positionInterpolator.Update(rawPos, deltaTime);

    cameraunlock::math::Quat4 headRotQ = cameraunlock::math::Quat4::FromYawPitchRoll(
        m_cachedYaw   * static_cast<float>(cameraunlock::math::kDegToRad),
        m_cachedPitch * static_cast<float>(cameraunlock::math::kDegToRad),
        m_cachedRoll  * static_cast<float>(cameraunlock::math::kDegToRad));

    cameraunlock::math::Vec3 offset = m_positionProcessor.Process(interpolatedPos, headRotQ, deltaTime);
    x = m_cachedPosX = offset.x;
    y = m_cachedPosY = offset.y;
    z = m_cachedPosZ = offset.z;
    m_cachedPosValid = true;
    return true;
}

} // namespace ACUHT
