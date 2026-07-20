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

namespace tinykv {

enum class LogLevel : int {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
    Off = 4
};

const char* LogLevelName(LogLevel level) noexcept;

LogLevel ParseLogLevel(std::string_view value);

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
    void Debug(Args&&... args) { Log(LogLevel::Debug, std::forward<Args>(args)); }

    template<typename... Args>
    void Info(Args&&... args) { Log(LogLevel::Info, std::forward<Args>(args)); }

    template<typename... Args>
    void Warn(Args&&... args) { Log(LogLevel::Warn, std::forward<Args>(args)); }

    template<typename... Args>
    void Error(Args&&... args) { Log(LogLevel::Error, std::forward<Args>(args)); }

private:
    Logger() = default;

    bool ShouldLog(LogLevel level) const noexcept;
    void Write(LogLevel level, std::string_view message);

    template<typename... Args>
    void Log(LogLevel level, Args&&... args)
    {
        if (!ShouldLog(level)) {
            return;
        }

        std::ostringstream stream;
        (stream << ... << std::forward<Args>(args));

        Write(level, stream.str());
    }

private:
    std::atomic<int> m_logLevel { static_cast<int>(LogLevel::Info) };
    std::mutex m_outputMutex;
};

} // namespace tinykv
