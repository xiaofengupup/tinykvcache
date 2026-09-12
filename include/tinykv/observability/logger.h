/**
 * 实现日志系统
 */
#pragma once

#include <string>
#include <string_view>
#include <atomic>
#include <mutex>
#include <sstream>
#include <utility>
#include <fmt/format.h>

namespace tinykv {

enum class LogLevel : int {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
    Off = 4
};

const char* LogLevelName(LogLevel level) noexcept;

/**
 * 简单线程安全日志器
 * 
 * 不依赖第三方库，适合当前项目；信号处理函数中禁止调用 Logger
 */
class Logger {
public:
    static Logger& Instance();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    LogLevel GetLevel() const noexcept;
    void SetLevel(LogLevel level) noexcept;

    template<typename... Args>
    void Debug(fmt::format_string<Args...> formatString, Args&&... args) 
    {
        Log(LogLevel::Debug, formatString, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Info(fmt::format_string<Args...> formatString, Args&&... args)
    {
        Log(LogLevel::Info, formatString, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Warn(fmt::format_string<Args...> formatString, Args&&... args)
    {
        Log(LogLevel::Warn, formatString, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Error(fmt::format_string<Args...> formatString, Args&&... args)
    {
        Log(LogLevel::Error, formatString, std::forward<Args>(args)...);
    }

private:
    Logger() = default;

    bool ShouldLog(LogLevel level) const noexcept;
    void Write(LogLevel level, std::string_view message);

    template<typename... Args>
    void Log(LogLevel level, fmt::format_string<Args...> formatString, Args&&... args)
    {
        if (!ShouldLog(level)) {
            return;
        }

        Write(level, fmt::format(formatString, std::forward<Args>(args)...));
    }

private:
    std::atomic<int> m_logLevel { static_cast<int>(LogLevel::Info) };
    std::mutex m_outputMutex;
};

} // namespace tinykv

// 带参数打印日志
#define TINYKV_LOG_DEBUG(formatString, ...)                                          \
    do {                                                                             \
        ::tinykv::Logger::Instance().Debug(FMT_STRING(formatString), __VA_ARGS__);   \
    } while (false)

#define TINYKV_LOG_INFO(formatString, ...)                                           \
    do {                                                                             \
        ::tinykv::Logger::Instance().Info(FMT_STRING(formatString), __VA_ARGS__);    \
    } while (false)

#define TINYKV_LOG_WARN(formatString, ...)                                           \
    do {                                                                             \
        ::tinykv::Logger::Instance().Warn(FMT_STRING(formatString), __VA_ARGS__);    \
    } while (false)

#define TINYKV_LOG_ERROR(formatString, ...)                                          \
    do {                                                                             \
        ::tinykv::Logger::Instance().Error(FMT_STRING(formatString), __VA_ARGS__);   \
    } while (false)


// 纯文本日志，无参数
#define TINYKV_LOG_DEBUG_MSG(messageLiteral)                              \
    do {                                                                  \
        ::tinykv::Logger::Instance().Debug(FMT_STRING(messageLiteral));   \
    } while (false)

#define TINYKV_LOG_INFO_MSG(messageLiteral)                                   \
    do {                                                                  \
        ::tinykv::Logger::Instance().Info(FMT_STRING(messageLiteral));    \
    } while (false)

#define TINYKV_LOG_WARN_MSG(messageLiteral)                                   \
    do {                                                                  \
        ::tinykv::Logger::Instance().Warn(FMT_STRING(messageLiteral));    \
    } while (false)

#define TINYKV_LOG_ERROR_MSG(messageLiteral)                                  \
    do {                                                                  \
        ::tinykv::Logger::Instance().Error(FMT_STRING(messageLiteral));   \
    } while (false)