#include "pch.h"
#include "path_utils.h"

namespace ACUHT {

static void DummyAddress() {}

std::string GetModuleDirectory() {
    char modulePath[MAX_PATH] = {};
    HMODULE hModule = nullptr;

    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&DummyAddress),
        &hModule
    );

    if (!hModule) return "";
    if (GetModuleFileNameA(hModule, modulePath, MAX_PATH) == 0) return "";

    std::string path(modulePath);
    size_t lastSlash = path.find_last_of("\\/");
    if (lastSlash != std::string::npos) return path.substr(0, lastSlash + 1);
    return "";
}

std::string GetModulePath(const char* filename) {
    std::string dir = GetModuleDirectory();
    if (dir.empty()) return filename;
    return dir + filename;
}

std::string GetHostExeDirectory() {
    char exePath[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) == 0) return "";

    std::string path(exePath);
    size_t lastSlash = path.find_last_of("\\/");
    if (lastSlash != std::string::npos) return path.substr(0, lastSlash + 1);
    return "";
}

std::string GetHostExePath(const char* filename) {
    std::string dir = GetHostExeDirectory();
    if (dir.empty()) return filename;
    return dir + filename;
}

} // namespace ACUHT
