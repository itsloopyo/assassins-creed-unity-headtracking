#pragma once

#include <string>
#include <Windows.h>

namespace ACUHT {

// Directory containing our DLL (trailing backslash)
std::string GetModuleDirectory();

// Full path to a file alongside our DLL
std::string GetModulePath(const char* filename);

// Directory containing the host executable (trailing backslash)
std::string GetHostExeDirectory();

// Full path to a file alongside the host executable
std::string GetHostExePath(const char* filename);

} // namespace ACUHT
