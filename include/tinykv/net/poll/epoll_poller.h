/**
 * 实现 Linux EpollPoller
 * 
 * 当前仅使用 level-triggered 模式，不设置 EPOLLET。
 */
#pragma once

#include "tinykv/net/poll/poller.h"
#include "tinykv/net/scoped_fd.h"

#include <sys/epoll.h> // linux 独有

#include <unordered_map>
#include <vector>

namespace tinykv {

class EpollPoller final : public Poller {
public:
    EpollPoller();
    ~EpollPoller() override = default;

    void Add(int fd, IoEvent interests) override;
    void Modity(int fd, IoEvent interests) override;
    void Remove(int fd) override;
    std::vector<ReadyEvent> Wait(std::chrono::milliseconds timeout) override;
    const char* Name() const noexcept override;

private:
    static std::uint32_t ToNativeEvents(IoEvent interests);
    static IoEvent FromNativeEvents(std::uint32_t nativeEvents);

private:
    ScopedFd m_epollFd;
    std::unordered_map<int, IoEvent> m_interests;
    std::vector<epoll_event> m_nativeEvents; // epoll_wait 的接收数组
};

} // namespace tinykv
