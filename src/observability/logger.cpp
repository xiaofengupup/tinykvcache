#include "tinykv/observability/logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace tinykv {

const char* LogLevelName(LogLevel level) noexcept
{
    static std::unordered_map<LogLevel, const char*> logLevelMap = {
        { LogLevel::Debug, "DEBUG" },
        { LogLevel::Info, "INFO" },
        { LogLevel::Warn, "WARN" },
        { LogLevel::Error, "ERROR" },
        { LogLevel::Off, "OFF"}
    };

    auto iter = logLevelMap.find(level);
    if (iter == logLevelMap.end()) {
        return "UNKNOWN";
    }

    return iter->second;
}

LogLevel ParseLogLevel(std::string_view value)
{
    static std::unordered_map<std::string_view, LogLevel> logValueMap = {
        { "debug", LogLevel::Debug },
        { "info", LogLevel::Info},
        { "warn", LogLevel::Warn },
        { "error", LogLevel::Error },
        { "off", LogLevel::Off }
    };

    auto iter = logValueMap.find(value);
    if (iter == logValueMap.end()) {
        throw std::invalid_argument("log level must be debug, info, warn, error, or off");
    }

    return iter->second;
}

Logger& Logger::Instance()
{
    static Logger logger;
    return logger;
}

LogLevel Logger::GetLevel() const noexcept
{
    return static_cast<LogLevel>(m_logLevel.load(std::memory_order_relaxed));
}

void Logger::SetLevel(LogLevel level) noexcept
{
    m_logLevel.store(static_cast<int>(level), std::memory_order_relaxed);
}

bool Logger::ShouldLog(LogLevel level) const noexcept
{
    return static_cast<int>(level) >= m_logLevel.load(std::memory_order_relaxed);
}

void Logger::Write(LogLevel level, std::string_view message)
{
    const auto now = std::chrono::system_clock::now();
    const auto timestamp = std::chrono::system_clock::to_time_t(now);
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm localTime {};
    (void)localtime_r(&timestamp, &localTime);

    std::lock_guard<std::mutex> lock(m_outputMutex);

    std::cerr << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S")
              << '.'
              << std::setfill('0')
              << std::setw(3)
              << milliseconds.count()
              << " ["
              << LogLevelName(level)
              << "] [thread="
              << std::this_thread::get_id()
              << "] "
              << message
              << "\n";
}

} // namespace tinykv