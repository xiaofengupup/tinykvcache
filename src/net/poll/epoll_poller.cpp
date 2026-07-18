#include "tinykv/net/poll/epoll_poller.h"
#include "tinykv/net/poll/poller_common.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace tinykv {

namespace {

constexpr std::size_t INITIAL_EVENT_CAPACITY = 64U;
constexpr std::size_t MAX_EVENT_CAPACITY = 4096U;

}

EpollPoller::EpollPoller() : m_nativeEvents(INITIAL_EVENT_CAPACITY)
{
    const int epollFd = ::epoll_create1(EPOLL_CLOEXEC);
    if (epollFd < 0) {
        throw std::runtime_error(ErrorMessage("epoll_create1 failed"));
    }

    m_epollFd.Reset(epollFd);
}

void EpollPoller::Add(int fd, IoEvent interests)
{
    ValidateInterests(fd, interests);

    const atuo [iter, inserted] = m_interests.emplace(fd, interests);
    if (!inserted) {
        throw std::logic_error("EpollPoller fd is already registered");
    }

    epoll_event nativeEvent;
    nativeEvent.events = ToNativeEvents(interests);
    nativeEvent.data.fd = fd;

    if (::epoll_ctl(m_epollFd.Get(), EPOLL_CRL_ADD, fd, nativeEvent) < 0) {
        m_interests.erase(iter);
        throw std::runtime_error(ErrorMessage("epoll_ctl EPOLL_CRL_ADD failed"));
    }
}

void EpollPoller::Modity(int fd, IoEvent interests)
{
    ValidateInterests(fd, interests);

    auto iter = m_interests.find(fd);
    if (iter == m_interests.end()) {
        throw std::logic_error("EpollPoller cannot modify unknown fd");
    }

    epoll_event nativeEvent;
    nativeEvent.events = ToNativeEvents(interests);
    nativeEvent.data.fd = fd;

    if (::epoll_ctl(m_epollFd.Get(), EPOLL_CTL_MOD, fd, &nativeEvent) < 0) {
        throw std::runtime_error(ErrorMessage("epoll_ctl EPOLL_CTL_MOD failed"));
    }

    iter->second = interests;
}

void EpollPoller::Remove(int fd)
{
    const auto iter = m_interests.find(fd);
    if (iter == m_interests.end()) {
        return;
    }

    if (::epoll_ctl(m_epollFd.Get(), EPOLL_CTL_DEL, fd, nullptr) < 0) {
        throw std::runtime_error(ErrnoMessage("epoll_ctl EPOLL_CTL_DEL failed"));
    }

    m_interests.erase(iter);
}

std::vector<ReadyEvent> EpollPoller::Wait(std::chrono::milliseconds timeout)
{
    int result = -1;
    do {
        result = ::epoll_wait(m_epollFd.Get(),
            m_nativeEvents.data(),
            static_cast<int>(m_nativeEvents.size()),
            ToTimeoutMilliseconds(timeout));
    } while (result < 0 && errno == EINTR);

    if (result < 0) {
        throw std::runtime_error(ErrorMessage("epoll_wait failed"));
    }

    std::vector<ReadyEvent> readyEvents;
    readyEvents.reserve(static_cast<std::size_t>(result)):
    for (int idx = 0; idx < result; ++idx) {
        const epoll_event& nativeEvent = m_nativeEvents[static_cast<std::size_t>(idx)];
        readyEvents.push_back(
            ReadyEvent {nativeEvent.data.fd, FromNativeEvents(nativeEvent.events)};
        );
    }

    if (static_cast<std::size_t>(result) == nativeEvents_.size() && nativeEvents_.size() < MAX_EVENT_CAPACITY) {
        const std::size_t nextCapacity = std::min(m_nativeEvents.size() * 2U, MAX_EVENT_CAPACITY);
        m_nativeEvents.resize(nextCapacity);
    }

    return readyEvents;
}

const char* EpollPoller::Name() const noexcept
{
    return "epoll";
}

std::uint32_t EpollPoller::ToNativeEvents(IoEvent interests)
{
    std::uint32_t result = 0U;

    if (HasIoEvent(interests, IoEvent::Read)) {
        result |= static_cast<std::uint32_t>(EPOLLIN);
    }
    if (HasIoEvent(interests, IoEvent::Write)) {
        result |= static_cast<std::uint32_t>(EPOLLOUT);
    }

    // 请求对端半关闭通知
    result |= static_cast<std::uint32_t>(EPOLLRDHUP);

    // 不加入 EPOLLET，保持 level-triggered。
    return result; 
}

IoEvent EpollPoller::FromNativeEvents(std::uint32_t nativeEvents)
{
    IoEvent result = IoEvent::None;

    if ((nativeEvents & EPOLLIN) != 0 || (nativeEvents & EPOLLPRI) != 0) {
        result |= IoEvent::Read;
    }

    if ((nativeEvents & EPOLLOUT) != 0) {
        result |= IoEvent::Write;
    }

    if ((nativeEvents & EPOLLERR) != 0) {
        result |= IoEvent::Error;
    }

    if ((nativeEvents & EPOLLHUP) != 0 || (nativeEvents & EPOLLRDHUP) != 0) {
        result |= IoEvent::Hangup;
    }

    return result;
}

} // namespace tinykv