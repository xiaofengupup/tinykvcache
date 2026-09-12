#include "tinykv/net/poll/io_event.h"
#include "tinykv/net/poll/poller_factory.h"
#include "tinykv/net/scoped_fd.h"

#include <gtest/gtest.h>
#include <chrono>
#include <stdexcept>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace {

struct PipePair {
    tinykv::ScopedFd readFd;
    tinykv::ScopedFd writeFd;
};

PipePair CreatePipePair()
{
    int rawFds[2] = {-1, -1};
    (void)::pipe(rawFds);

    return PipePair {
        tinykv::ScopedFd(rawFds[0]),
        tinykv::ScopedFd(rawFds[1])
    };
}

bool ContainsEvent(const std::vector<tinykv::ReadyEvent>& events, int fd, tinykv::IoEvent expected)
{
    for (const tinykv::ReadyEvent& event : events) {
        if (event.fd == fd && tinykv::HasIoEvent(event.events, expected)) {
            return true;
        }
    }

    return false;
}

void RunReadableContract(tinykv::PollerBackend backend)
{
    auto poller = tinykv::PollerFactory::CreatePoller(backend);
    auto pipe = CreatePipePair();
    poller->Add(pipe.readFd.Get(), tinykv::IoEvent::Read);

    const char input = 'A';
    EXPECT_EQ(::write(pipe.writeFd.Get(), &input, 1), 1);

    const auto events = poller->Wait(std::chrono::milliseconds(500));
    EXPECT_TRUE(ContainsEvent(events, pipe.readFd.Get(), tinykv::IoEvent::Read));

    char output = '\0';
    EXPECT_EQ(::read(pipe.readFd.Get(), &output, 1), 1);
    EXPECT_EQ(output, input);

    poller->Remove(pipe.readFd.Get());
    const auto afterRemove = poller->Wait(std::chrono::milliseconds(0));
    EXPECT_FALSE(ContainsEvent(afterRemove, pipe.readFd.Get(), tinykv::IoEvent::Read));
}

void RunWritableContract(tinykv::PollerBackend backend)
{
    auto poller = tinykv::PollerFactory::CreatePoller(backend);
    auto pipe = CreatePipePair();
    poller->Add(pipe.writeFd.Get(), tinykv::IoEvent::Write);

    const auto events = poller->Wait(std::chrono::milliseconds(500));
    EXPECT_TRUE(ContainsEvent(events, pipe.writeFd.Get(), tinykv::IoEvent::Write));
}

void RunRegistrationErrorContract(tinykv::PollerBackend backend)
{
    auto poller = tinykv::PollerFactory::CreatePoller(backend);
    auto pipe = CreatePipePair();
    poller->Add(pipe.readFd.Get(), tinykv::IoEvent::Read);

    bool duplicateAddThrown = false;
    try {
        poller->Add(pipe.readFd.Get(), tinykv::IoEvent::Read);
    } catch (const std::logic_error&) {
        duplicateAddThrown = true;
    }
    EXPECT_TRUE(duplicateAddThrown);

    poller->Remove(pipe.readFd.Get());
    bool unknownModifyThrown = false;
    try {
        poller->Modity(pipe.readFd.Get(), tinykv::IoEvent::Read);
    } catch (const std::logic_error&) {
        unknownModifyThrown = true;
    }
    EXPECT_TRUE(unknownModifyThrown);
}

TEST(PollerTest, PollBackend)
{
    RunReadableContract(tinykv::PollerBackend::Poll);
    RunWritableContract(tinykv::PollerBackend::Poll);
    RunRegistrationErrorContract(tinykv::PollerBackend::Poll);
}

TEST(PollerTest, EpollBackend)
{
#if TINYKV_HAS_EPOLL
    RunReadableContract(tinykv::PollerBackend::Epoll);
    RunWritableContract(tinykv::PollerBackend::Epoll);
    RunRegistrationErrorContract(tinykv::PollerBackend::Epoll);
#else
    bool thrown = false;
    try {
        auto poller = tinykv::PollerFactory::CreatePoller(tinykv::PollerBackend::Epoll);
        (void)poller;
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    EXPECT_TRUE(thrown);
#endif
}

TEST(PollerTest, AutoBackend)
{
    auto poller = tinykv::PollerFactory::CreatePoller(tinykv::PollerBackend::Auto);

#if TINYKV_HAS_EPOLL
    EXPECT_TRUE(std::string_view(poller->Name()) == "epoll");
#else
    EXPECT_TRUE(std::string_view(poller->Name()) == "poll");
#endif
}

}