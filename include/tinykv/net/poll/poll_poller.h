/**
 * 基于 POSIX poll() 的 Poller 实现
 * 
 * Linux 和 macOS 都可以使用
 */
#pragma once

#include "tinykv/net/poll/poller.h"
#include <unordered_map>

namespace tinykv {

class PollPoller final : public Poller {
public:
    PollPoller() = default;
    ~PollPoller() override = default;

    void Add(int fd, IoEvent interests) override;
    void Modity(int fd, IoEvent interests) override;
    void Remove(int fd) override;
    std::vector<ReadyEvent> Wait(std::chrono::milliseconds timeout) override;
    const char* Name() const noexcept override;

private:
    static short ToNativeEvents(IoEvent interests);
    static IoEvent FromNativeEvents(short nativeEvents);

private:
    std::unordered_map<int, IoEvent> m_interests;
};

} // namespace tinykv
