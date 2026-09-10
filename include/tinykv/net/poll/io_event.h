/**
 * 定义统一事件类型
 * 
 * 这里不直接暴露 POLLIN、POLLOUT、EPOLLIN、EPOLLOUT，因为它们属于不同的后端
 * TCPServer 只认识 IoEvent::Read, IoEvent::Write, IoEvent::Error, IoEvent::Hangup
 */
#pragma once

#include <cstdint>

namespace tinykv {

// 与具体系统调用无关的 IO 事件类型
enum class IoEvent : std::uint32_t {
    None = 0U,
    Read = 1U << 0U,        // fd 当前可读
    Write = 1U << 1U,       // fd 当前可写
    Error = 1U << 2U,       // fd 出现错误
    Hangup = 1U << 3U       // 对端关闭或者连接挂起
};

constexpr std::uint32_t IoEventBits(IoEvent event) noexcept
{
    return static_cast<std::uint32_t>(event);
}

constexpr IoEvent operator|(IoEvent left, IoEvent right) noexcept
{
    return static_cast<IoEvent>(IoEventBits(left) | IoEventBits(right));
}

constexpr IoEvent operator&(IoEvent left, IoEvent right) noexcept
{
    return static_cast<IoEvent>(IoEventBits(left) & IoEventBits(right));
}

constexpr IoEvent& operator|=(IoEvent& left, IoEvent right)
{
    left = left | right;
    return left;
}

constexpr bool HasIoEvent(IoEvent events, IoEvent expected)
{
    return (IoEventBits(events) & IoEventBits(expected)) != 0U;
}

// Poller 返回给 TCPServer 的统一就绪事件
struct ReadyEvent {
    int fd {-1};
    IoEvent events {IoEvent::None};
};


} // namespace tinykv