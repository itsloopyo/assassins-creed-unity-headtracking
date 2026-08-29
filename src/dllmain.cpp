#include "pch.h"
#include "core/mod.h"
#include "core/logger.h"
#include <process.h>

static HANDLE g_initThreadHandle = nullptr;

static unsigned __stdcall InitThread(void*) {
    ACUHT::Logger::Instance().Info("init thread started, sleeping 6s for AnvilNext bootstrap");

    // Wait for the game process to finish initial startup. The ASI loader
    // injects us very early; AnvilNext takes several seconds to bootstrap
    // its own subsystems, and installing hooks before that occasionally
    // crashes. ACU's Uplay/anti-tamper layer in particular is slow to
    // finish unpacking its own code.
    Sleep(6000);

    ACUHT::Logger::Instance().Info("init thread wake, calling Mod::Initialize()");

    __try {
        if (!ACUHT::Mod::Instance().Initialize()) {
            ACUHT::Logger::Instance().Error("Mod initialization failed");
            return 1;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ACUHT::Logger::Instance().Error("CRASH during mod initialization (exception 0x%08X)",
                                        GetExceptionCode());
        return 1;
    }

    ACUHT::Logger::Instance().Info("%s v%s loaded successfully",
                                   ACUHT::ACUHT_MOD_NAME, ACUHT::ACUHT_VERSION);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(hModule);

            // Open the log file *synchronously* on attach, before we spawn
            // the init thread - if the DLL gets unloaded or the init thread
            // faults early, we still have a record that the .asi was
            // injected at all. This also makes "is the ASI Loader even
            // finding us?" diagnosable without running a full init.
            ACUHT::Logger::Instance().Initialize();

            char hostExe[MAX_PATH] = {};
            GetModuleFileNameA(nullptr, hostExe, MAX_PATH);
            char selfPath[MAX_PATH] = {};
            GetModuleFileNameA(hModule, selfPath, MAX_PATH);

            ACUHT::Logger::Instance().Info(
                "==== %s v%s: DLL_PROCESS_ATTACH ====",
                ACUHT::ACUHT_MOD_NAME, ACUHT::ACUHT_VERSION);
            ACUHT::Logger::Instance().Info("  host exe : %s", hostExe);
            ACUHT::Logger::Instance().Info("  self dll : %s", selfPath);

            g_initThreadHandle = (HANDLE)_beginthreadex(nullptr, 0, InitThread, nullptr, 0, nullptr);
            if (!g_initThreadHandle) {
                ACUHT::Logger::Instance().Error("Failed to spawn init thread (errno=%d)", errno);
            }
            break;
        }

        case DLL_PROCESS_DETACH:
            ACUHT::Logger::Instance().Info("DLL_PROCESS_DETACH");
            if (g_initThreadHandle) {
                WaitForSingleObject(g_initThreadHandle, 2000);
                CloseHandle(g_initThreadHandle);
                g_initThreadHandle = nullptr;
            }
            ACUHT::Mod::Instance().Shutdown();
            ACUHT::Logger::Instance().Shutdown();
            break;
    }
    return TRUE;
}
