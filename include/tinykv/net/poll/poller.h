/**
 * Poller 抽象接口
 */
#pragma once

#include "tinykv/net/poll/io_event.h"

#include <chrono>
#include <vector>

namespace tinykv {

/**
 * Poller 封装 IO 就绪通知机制
 * 
 * TcpServer 只依赖这个接口，不直接依赖 poll 或者 epoll
 */
class Poller {
public:
    virtual ~Poller() = default;

    // 不允许拷贝和移动
    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;
    Poller(Poller&&) = delete;
    Poller& operator=(Poller&&) = delete;

    // 注册一个 fd，interests 只能包含 Read/Write
    virtual void Add(int fd, IoEvent interests) = 0;

    // 修改已经注册的 fd 的关注事件
    virtual void Modity(int fd, IoEvent interests) = 0;

    // 删除 fd，fd 不存在时允许做 no-op，方便连接清理
    virtual void Remove(int fd) = 0;

    // 等待 IO 事件
    virtual std::vector<ReadyEvent> Wait(std::chrono::milliseconds timeout) = 0;

    // 返回后端名称，用于日志和测试
    virtual const char* Name() const noexcept = 0;

protected:
    Poller() = default;
};

} // namespace tinykv