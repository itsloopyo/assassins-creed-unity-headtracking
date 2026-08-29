#pragma once

#include <atomic>
#include <cstdarg>
#include <fstream>
#include <mutex>

namespace ACUHT {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

class Logger {
public:
    static Logger& Instance();

    bool Initialize();
    void Shutdown();

    // Verbose diagnostics are off unless the user turns them on, so anything
    // logged at Debug must be safe to skip entirely - including the work that
    // computes its arguments. Call sites that would pay for that work guard it
    // with IsEnabled().
    void SetMinLevel(LogLevel level);
    bool IsEnabled(LogLevel level) const;

    void Debug(const char* fmt, ...);
    void Info(const char* fmt, ...);
    void Warning(const char* fmt, ...);
    void Error(const char* fmt, ...);

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger() = default;
    ~Logger() = default;

    void LogVa(LogLevel level, const char* fmt, va_list args);
    void WriteLog(LogLevel level, const char* message);
    const char* LevelToString(LogLevel level);

    std::ofstream m_logFile;
    std::mutex m_mutex;
    std::atomic<LogLevel> m_minLevel{LogLevel::Info};
    bool m_initialized = false;
};

} // namespace ACUHT
