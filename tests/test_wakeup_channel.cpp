#include "tinykv/net/poll/io_event.h"
#include "tinykv/net/poll/poller_factory.h"
#include "tinykv/net/wakeup/termination_signal_handler.h"
#include "tinykv/net/wakeup/wakeup_channel.h"

#include <gtest/gtest.h>
#include <chrono>
#include <csignal>
#include <thread>
#include <vector>

namespace {

bool ContainsReadableEvent(const std::vector<tinykv::ReadyEvent>& events, int fd)
{
    for (const tinykv::ReadyEvent& event : events) {
        if (event.fd == fd && tinykv::HasIoEvent(event.events, tinykv::IoEvent::Read)) {
            return true;
        }
    }

    return false;
}

void RunWakeupContract(tinykv::PollerBackend backend)
{
    auto poller = tinykv::PollerFactory::CreatePoller(backend);
    tinykv::WakeupChannel channel;
    poller->Add(channel.ReadFd(), tinykv::IoEvent::Read);

    std::thread notifier([&channel](){
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        channel.Notify();
    });
    const auto events = poller->Wait(std::chrono::milliseconds(500));
    notifier.join();
    EXPECT_TRUE(ContainsReadableEvent(events, channel.ReadFd()));

    channel.Drain();
    const auto afterDrain = poller->Wait(std::chrono::milliseconds(0));
    EXPECT_FALSE(ContainsReadableEvent(afterDrain, channel.ReadFd()));
}

TEST(WakeupChannelTest, PollWakeup)
{
    RunWakeupContract(tinykv::PollerBackend::Poll);
}

TEST(WakeupChannelTest, EpollWakeup)
{
#if TINYKV_HAS_EPOLL
    RunWakeupContract(tinykv::PollerBackend::Epoll);
#endif
}

TEST(WakeupChannelTest, SignalWakeup)
{
    auto poller = tinykv::PollerFactory::CreatePoller(tinykv::PollerBackend::Auto);
    tinykv::WakeupChannel channel;
    poller->Add(channel.ReadFd(), tinykv::IoEvent::Read);

    tinykv::TerminationSignalHandler signals(channel.WriteFd());
    EXPECT_EQ(::raise(SIGTERM), 0);

    const auto events = poller->Wait(std::chrono::milliseconds(500));
    EXPECT_TRUE(ContainsReadableEvent(events, channel.ReadFd()));
    channel.Drain();
}

}
