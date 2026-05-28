#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <Psapi.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <string>
#include <vector>
#include <cstdint>
#include <cmath>
#include <chrono>
#include <fstream>
#include <sstream>
#include <iomanip>

#include "MinHook.h"

#include "core/constants.h"

namespace ACUHT {
    class Mod;
    struct Config;
    class Logger;
    class HookManager;
}
