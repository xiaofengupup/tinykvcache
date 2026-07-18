/**
 * Poll 工厂
 */
#pragma once

#include "tinykv/net/poll/poller.h"

#include <memory>
#include <string_view>

namespace tinykv {

enum class PollerBackend {
    Auto,
    Poll,
    Epoll
};

/**
 * 创建指定后端
 * 
 * Auto：Linux -> epoll，其它 POSIX 平台 -> poll
 */
std::unique_ptr<Poller> CreatePoller(PollerBackend backend);

/**
 * 将命令行字符串解析为后端枚举
 */
PollerBackend ParsePollerBackend(std::string_view value);

const char* PollerBackendName(PollerBackend backend) noexcept;


} // namespace tinykv