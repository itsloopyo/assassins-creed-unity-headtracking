#pragma once

#include "config.h"
#include <cameraunlock/protocol/udp_receiver.h>
#include <cameraunlock/time/frame_clock.h>
#include <cameraunlock/tracking/head_tracking_session.h>

namespace ACUHT {

class Mod {
public:
    static Mod& Instance();

    bool Initialize();
    void Shutdown();

    bool IsEnabled() const { return m_enabled.load(); }
    void SetEnabled(bool enabled);
    void Toggle();

    void CycleTrackingMode();
    void ToggleYawMode();

    Config& GetConfig() { return m_config; }
    const Config& GetConfig() const { return m_config; }

    // Processed rotation (degrees) for the camera hook.
    bool GetProcessedRotation(float& yaw, float& pitch, float& roll);

    // Processed position offset in tracker space (meters).
    bool GetPositionOffset(float& x, float& y, float& z);

    bool IsRotationActive() const { return m_session.IsRotationActive(); }
    bool IsPositionActive() const { return m_session.IsPositionActive(); }
    bool IsWorldSpaceYaw() const { return m_worldSpaceYaw; }

    Mod(const Mod&) = delete;
    Mod& operator=(const Mod&) = delete;

private:
    Mod() = default;
    ~Mod() = default;

    bool LoadConfig();
    bool InitializeHooks();
    void ShutdownHooks();
    // Reports which of the two smoothing parameters the session is now using,
    // which follows the receiver's source-address classification.
    void LogConnectionLocality();

    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_initialized{false};

    Config m_config;
    cameraunlock::UdpReceiver m_udpReceiver;
    cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver> m_session{m_udpReceiver};
    cameraunlock::time::FrameClock m_frameClock{0.1f};

    bool m_worldSpaceYaw = true;

    uint64_t m_lastProcessTime = 0;

    // Last locality reported; the session owns the flag the processors use.
    bool m_isRemoteConnection = false;

    bool m_cameraHookInstalled = false;
    bool m_inputHookInstalled = false;
};

} // namespace ACUHT
