#pragma once

#include <string>
#include <Windows.h>

namespace ACUHT {

// Directory containing our DLL (trailing backslash)
std::string GetModuleDirectory();

// Full path to a file alongside our DLL
std::string GetModulePath(const char* filename);

} // namespace ACUHT
