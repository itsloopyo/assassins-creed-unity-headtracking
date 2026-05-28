#pragma once

#include "config.h"
#include <cameraunlock/protocol/udp_receiver.h>
#include <cameraunlock/processing/tracking_processor.h>
#include <cameraunlock/processing/pose_interpolator.h>
#include <cameraunlock/processing/position_processor.h>
#include <cameraunlock/processing/position_interpolator.h>

namespace ACUHT {

enum class TrackingMode {
    SixDof,        // rotation + position
    RotationOnly,  // 3DOF rotational
    PositionOnly,  // 3DOF positional
};

class Mod {
public:
    static Mod& Instance();

    bool Initialize();
    void Shutdown();

    bool IsEnabled() const { return m_enabled.load(); }
    void SetEnabled(bool enabled);
    void Toggle();

    void Recenter();
    void CycleTrackingMode();
    void ToggleYawMode();

    Config& GetConfig() { return m_config; }
    const Config& GetConfig() const { return m_config; }

    // Processed rotation (degrees) for the camera hook.
    bool GetProcessedRotation(float& yaw, float& pitch, float& roll);

    // Processed position offset in tracker space (meters).
    bool GetPositionOffset(float& x, float& y, float& z);

    TrackingMode GetTrackingMode() const { return m_trackingMode; }
    bool IsRotationActive() const {
        return m_trackingMode == TrackingMode::SixDof ||
               m_trackingMode == TrackingMode::RotationOnly;
    }
    bool IsPositionActive() const {
        return m_trackingMode == TrackingMode::SixDof ||
               m_trackingMode == TrackingMode::PositionOnly;
    }
    bool IsWorldSpaceYaw() const { return m_worldSpaceYaw; }

    Mod(const Mod&) = delete;
    Mod& operator=(const Mod&) = delete;

private:
    Mod() = default;
    ~Mod() = default;

    bool LoadConfig();
    bool InitializeHooks();
    void ShutdownHooks();

    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_initialized{false};

    Config m_config;
    cameraunlock::UdpReceiver m_udpReceiver;
    cameraunlock::PoseInterpolator m_poseInterpolator;
    cameraunlock::TrackingProcessor m_processor;
    int64_t m_lastReceiveTimestamp = 0;

    cameraunlock::PositionProcessor m_positionProcessor;
    cameraunlock::PositionInterpolator m_positionInterpolator;
    TrackingMode m_trackingMode = TrackingMode::SixDof;
    bool m_worldSpaceYaw = true;

    uint64_t m_lastProcessTime = 0;
    float m_lastDeltaTime = 0.016f;

    float m_cachedYaw = 0.0f;
    float m_cachedPitch = 0.0f;
    float m_cachedRoll = 0.0f;
    bool m_cachedValid = false;

    uint64_t m_lastPositionProcessTime = 0;
    float m_cachedPosX = 0.0f;
    float m_cachedPosY = 0.0f;
    float m_cachedPosZ = 0.0f;
    bool m_cachedPosValid = false;

    bool m_hasCentered = false;
    int m_stabilizationFrames = 0;

    float m_lastRawYaw = 0.0f;
    float m_lastRawPitch = 0.0f;
    float m_lastRawRoll = 0.0f;

    bool m_cameraHookInstalled = false;
    bool m_inputHookInstalled = false;
};

} // namespace ACUHT
