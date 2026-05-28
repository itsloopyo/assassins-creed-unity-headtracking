#pragma once

namespace ACUHT {

class HookManager {
public:
    static HookManager& Instance();

    bool Initialize();
    void Shutdown();

    HookManager(const HookManager&) = delete;
    HookManager& operator=(const HookManager&) = delete;

private:
    HookManager() = default;
    ~HookManager() = default;
    bool m_initialized = false;
};

} // namespace ACUHT
