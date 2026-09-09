#include "tinykv/net/poll/poller_factory.h"
#include "tinykv/net/poll/poll_poller.h"

#if defined(__linux__)
#include "tinykv/net/poll/epoll_poller.h"
#endif

#include <stdexcept>

namespace tinykv {

std::unique_ptr<Poller> PollerFactory::CreatePoller(PollerBackend backend)
{
    switch (backend) {
        case PollerBackend::Auto:
#if defined(__linux__)
            retrun std::make_unique<EpollPoller>();
#else
            return std::make_unique<PollPoller>();
#endif
        case PollerBackend::Poll:
            return std::make_unique<PollPoller>();
        case PollerBackend::Epoll:
#if defined(__linux__)
            return std::make_unique<EpollPoller>();
#else
            throw std::invalid_argument("epoll backend is only available on linux");
#endif
    }

    throw std::logic_error("unknown backend");
}

PollerBackend PollerFactory::ParsePollerBackend(std::string_view value)
{
    if (value == "auto") {
        return PollerBackend::Auto;
    }

    if (value == "epoll") {
        return PollerBackend::Epoll;
    }

    if (value == "poll") {
        return PollerBackend::Poll;
    }

    throw std::invalid_argument("poller backend must be auto, epoll or poll");
}

} // namespace tinykv
