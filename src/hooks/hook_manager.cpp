#include "pch.h"
#include "hook_manager.h"
#include "core/logger.h"

namespace ACUHT {

HookManager& HookManager::Instance() {
    static HookManager instance;
    return instance;
}

bool HookManager::Initialize() {
    if (m_initialized) return true;
    MH_STATUS status = MH_Initialize();
    if (status != MH_OK) {
        Logger::Instance().Error("MH_Initialize failed: %d", static_cast<int>(status));
        return false;
    }
    m_initialized = true;
    Logger::Instance().Info("MinHook initialized");
    return true;
}

void HookManager::Shutdown() {
    if (!m_initialized) return;
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    m_initialized = false;
    Logger::Instance().Info("MinHook shutdown complete");
}

} // namespace ACUHT
