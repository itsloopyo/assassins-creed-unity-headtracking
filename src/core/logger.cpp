#include "pch.h"
#include "logger.h"
#include "path_utils.h"
#include <cstdio>

namespace ACUHT {

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

bool Logger::Initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) return true;

    std::string logPath = GetModulePath("HeadTracking.log");
    // Keep one previous generation: the session worth reading is often the one
    // that just crashed, and the user relaunches before sending the file.
    MoveFileExA(logPath.c_str(), GetModulePath("HeadTracking.prev.log").c_str(),
                MOVEFILE_REPLACE_EXISTING);
    m_logFile.open(logPath, std::ios::out | std::ios::trunc);
    if (!m_logFile.is_open()) return false;

#ifdef _DEBUG
    m_minLevel = LogLevel::Debug;
#endif

    m_initialized = true;
    return true;
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_logFile.is_open()) m_logFile.close();
    m_initialized = false;
}

void Logger::LogVa(LogLevel level, const char* fmt, va_list args) {
    char buffer[2048];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    WriteLog(level, buffer);
}

#define ACUHT_LOG_IMPL(METHOD, LEVEL)               \
void Logger::METHOD(const char* fmt, ...) {         \
    if (LogLevel::LEVEL < m_minLevel) return;       \
    va_list args;                                   \
    va_start(args, fmt);                            \
    LogVa(LogLevel::LEVEL, fmt, args);              \
    va_end(args);                                   \
}

ACUHT_LOG_IMPL(Debug,   Debug)
ACUHT_LOG_IMPL(Info,    Info)
ACUHT_LOG_IMPL(Warning, Warning)

void Logger::Error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LogVa(LogLevel::Error, fmt, args);
    va_end(args);
}

void Logger::WriteLog(LogLevel level, const char* message) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm;
    localtime_s(&tm, &time);

    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "%02d:%02d:%02d.%03d",
             tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()));

    if (m_logFile.is_open()) {
        m_logFile << "[" << timestamp << "] [" << LevelToString(level) << "] "
                  << message << std::endl;
    }
}

const char* Logger::LevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error:   return "ERROR";
        default:                return "UNKNOWN";
    }
}

} // namespace ACUHT
