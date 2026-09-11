/**
 * Poll 工厂
 */
#pragma once

#include "tinykv/net/poll/poller.h"
#include "tinykv/common/reactor.h"
#include <memory>

namespace tinykv {

/**
 * Poller 工厂类，简单工厂模式，提供创建 poller 的接口
 */
class PollerFactory {
public:
    /**
     * 创建指定后端 poller
     * 
     * Auto：Linux -> epoll，其它 POSIX 平台 -> poll
     */
    static std::unique_ptr<Poller> CreatePoller(PollerBackend backend);

    /**
     * 将命令行字符串解析为后端枚举
     */
    static PollerBackend ParsePollerBackend(std::string_view value);
};

} // namespace tinykv