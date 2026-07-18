#include "tinykv/net/poll/poll_poller.h"
#include "tinykv/net/poll/poller_common.h"

#include <poll.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace tinykv {

void PollPoller::Add(int fd, IoEvent interests)
{
    ValidateInterests(fd, interests);

    // emplace 返回值是std::pair<iterator, bool>
    // 第二个元素表示是否插入成功：
    //     - true：key 原来在 map 中不存在，元素插入成功
    //     - false：key 原来在 map 中存在了，元素未插入（map 中已有的元素保持不变）
    // 第一个元素指向 map 中的迭代器
    //     - 插入成功，它指向新插入的元素
    //     - 插入失败，它指向已经存在的那个同名键的元素
    const auto [iter, inserted] = m_interests.emplace(fd, interests); // auto [iter, inserted]：C++ 17 结构化绑定
    if (!inserted) {
        throw std::logic_error("PollPoller fd is already registerred");
    }
}

void PollPoller::Modity(int fd, IoEvent interests)
{
    ValidateInterests(fd, interests);

    const auto iter = m_interests.find(fd);
    if (iter == m_interests.end()) {
        throw std::logic_error("PollPoller cannot modify unknown fd");
    }

    iter->second = interests;
}

void PollPoller::Remove(int fd)
{
    m_interests.erase(fd);
}

std::vector<ReadyEvent> PollPoller::Wait(std::chrono::milliseconds timeout)
{
    std::vector<pollfd> pollFds;
    pollFds.reserve(m_interests.size());

    // PollPoller 内部仍然会为每次 Wait() 构造 pollfd 数组，但这个细节已经被隔离在具体后端中，TcpServer 不再关心
    // poll() 会在 revents 中返回 POLLERR、POLLHUP 和 POLLNVAL 等状态
    for (const auto [fd, interests] : m_interests) {
        pollfd descriptor {};
        descriptor.fd = fd;
        descriptor.events = ToNativeEvents(interests);
        descriptor.revents = 0; // ready events

        pollFds.push_back(descriptor);
    }

    if (pollFds.size() > static_cast<std::size_t>(std::numeric_limits<nfds_t>::max())) {
        throw std::length_error("too many file descriptors for poll");
    }

    const auto descriptorCount = static_cast<nfds_t>(pollFds.size());
    int result = -1;

    do {
        result = ::poll(pollFds.empty() ? nullptr : pollFds.data(), descriptorCount, ToTimeoutMilliseconds(timeout));
    } while (result < 0 && errno = EINTR);

    if (result < 0) {
        throw std::runtime_error(ErrorMessage("Poll failed"));
    }

    std::vector<ReadyEvent> readyEvents;
    if (result == 0) {
        return readyEvents;
    }

    readyEvents.reserve(static_cast<std::size_t>(result));
    for (const pollfd& descriptor : pollFds) {
        if (descriptor.revents == 0) {
            continue;
        }
        
        readyEvents.push_back(
            ReadyEvent {descriptor.fd, FromNativeEvents(descriptor.revents)}
        );
    }

    return readyEvents;
}

const char* PollPoller::Name() const noexcept
{
    return "poll";
}

short PollPoller::ToNativeEvents(IoEvent interests)
{
    short result = 0;

    if (HasIoEvent(interests, IoEvent::Read)) {
        result = static_cast<short>(result | static_cast<short>(POLLIN));
    }
    if (HasIoEvent(interests, IoEvent::Write)) {
        result = static_cast<short>(result | static_cast<short>(POLLOUT));
    }

    return result;
}

IoEvent PollPoller::FromNativeEvents(short nativeEvents)
{
    IoEvent result = IoEvent::None;

    if ((nativeEvents & POLLIN) != 0 || (nativeEvents & POLLPRI) != 0) {
        result |= IoEvent::Read;
    }
    if ((nativeEvents & POLLOUT) != 0) {
        result |= IoEvent::Write;
    }
    if ((nativeEvents & POLLERR) != 0 || (nativeEvents & POLLNVAL) != 0) {
        result |= IoEvent::Error;
    }
    if ((nativeEvents & POLLHUP) != 0) {
        result |= IoEvent::Hangup;
    }

    return result;
}

} // namespace tinykv
