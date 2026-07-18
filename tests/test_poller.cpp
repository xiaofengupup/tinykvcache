#include "tinykv/net/poll/io_event.h"
#include "tinykv/net/poll/poller_factory.h"
#include "tinykv/net/scoped_fd.h"
#include "test_utils.h"

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
    TINYKV_CHECK(::pipe(rawFds) == 0);

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
    auto poller = tinykv::CreatePoller(backend);
    auto pipe = CreatePipePair();
    poller->Add(pipe.readFd.Get(), tinykv::IoEvent::Read);

    const char input = 'A';
    TINYKV_CHECK(::write(pipe.writeFd.Get(), &input, 1) == 1);

    const auto events = poller->Wait(std::chrono::milliseconds(500));
    TINYKV_CHECK(ContainsEvent(events, pipe.readFd.Get(), tinykv::IoEvent::Read));

    char output = '\0';
    TINYKV_CHECK(::read(pipe.readFd.Get(), &output, 1) == 1);
    TINYKV_CHECK(output == input);

    poller->Remove(pipe.readFd.Get());
    const auto afterRemove = poller->Wait(std::chrono::milliseconds(0));
    TINYKV_CHECK(!ContainsEvent(afterRemove, pipe.readFd.Get(), tinykv::IoEvent::Read));
}

void RunWritableContract(tinykv::PollerBackend backend)
{
    auto poller = tinykv::CreatePoller(backend);
    auto pipe = CreatePipePair();
    poller->Add(pipe.writeFd.Get(), tinykv::IoEvent::Write);

    const auto events = poller->Wait(std::chrono::milliseconds(500));
    TINYKV_CHECK(ContainsEvent(events, pipe.writeFd.Get(), tinykv::IoEvent::Write));
}

void RunRegistrationErrorContract(tinykv::PollerBackend backend)
{
    auto poller = tinykv::CreatePoller(backend);
    auto pipe = CreatePipePair();
    poller->Add(pipe.readFd.Get(), tinykv::IoEvent::Read);

    bool duplicateAddThrown = false;
    try {
        poller->Add(pipe.readFd.Get(), tinykv::IoEvent::Read);
    } catch (const std::logic_error&) {
        duplicateAddThrown = true;
    }
    TINYKV_CHECK(duplicateAddThrown);

    poller->Remove(pipe.readFd.Get());
    bool unknownModifyThrown = false;
    try {
        poller->Modity(pipe.readFd.Get(), tinykv::IoEvent::Read);
    } catch (const std::logic_error&) {
        unknownModifyThrown = true;
    }
    TINYKV_CHECK(unknownModifyThrown);
}

void TestPollBackend()
{
    RunReadableContract(tinykv::PollerBackend::Poll);
    RunWritableContract(tinykv::PollerBackend::Poll);
    RunRegistrationErrorContract(tinykv::PollerBackend::Poll);
}

void TestEpollBackend()
{
#if TINYKV_HAS_EPOLL
    RunReadableContract(tinykv::PollerBackend::Epoll);
    RunWritableContract(tinykv::PollerBackend::Epoll);
    RunRegistrationErrorContract(tinykv::PollerBackend::Epoll);
#else
    bool thrown = false;
    try {
        auto poller = tinykv::CreatePoller(tinykv::PollerBackend::Epoll);
        (void)poller;
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    TINYKV_CHECK(thrown);
#endif
}

void TestAutoBackend()
{
    auto poller = tinykv::CreatePoller(tinykv::PollerBackend::Auto);

#if TINYKV_HAS_EPOLL
    TINYKV_CHECK(std::string_view(poller->Name()) == "epoll");
#else
    TINYKV_CHECK(std::string_view(poller->Name()) == "poll");
#endif
}

}

int main()
{
    TestPollBackend();
    TestEpollBackend();
    TestAutoBackend();

    std::cout << "poLler tests passed\n";
    return 0;
}