#include "tinykv/net/poll/poller_common.h"

#include <stdexcept>
#include <cstring>
#include <cerrno>
#include <limits>

namespace tinykv {

void ValidateInterests(int fd, IoEvent interests)
{
    if (fd < 0) {
        throw std::invalid_argument("Poller fd must be non-negative");
    }

    const bool hasRead = HasIoEvent(interests, IoEvent::Read);
    const bool hasWrite = HasIoEvent(interests, IoEvent::Write);

    if (!hasRead && !hasWrite) {
        throw std::invalid_argument("Poller interests must contain Read or Write");
    }
}

int ToTimeoutMilliseconds(std::chrono::milliseconds timeout)
{
    const auto count = timeout.count();

    if (count < 0) {
        return -1;
    }

    const auto maxTimeout = static_cast<decltype(count)>(std::numeric_limits<int>::max());
    if (count > maxTimeout) {
        return std::numeric_limits<int>::max();
    }

    return static_cast<int>(count);
}

std::string ErrorMessage(const char* prefix)
{
    return std::string(prefix) + ": " + std::strerror(errno);
}
    
} // namespace tinykv
