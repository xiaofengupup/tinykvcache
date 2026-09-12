/**
 * 项目整体配置
 */
#pragma once

#include "tinykv/observability/logger.h"
#include "tinykv/common/server_info.h"

#include <string>
#include <chrono>

namespace tinykv {

struct AppConfig {
    std::string host { "0.0.0.0" }; // 服务器地址
    int port { 7777 };              // 服务器端口

    NetworkOptions network;         // 网络配置
    ReactorOptions reactor;         // Reactor配置
    ShutdownOptions shutdown;       // 停止配置               

    std::chrono::milliseconds sweepInterval { 5000 };   // 扫描过期键的间隔
    LogLevel logLevel { LogLevel::Info };               // 日志级别
};

} // namespace tinykv