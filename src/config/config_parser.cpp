#include "tinykv/config/config_parser.h"

#include <stdexcept>

namespace tinykv {

PollerBackend ParsePollerBackend(std::string_view value)
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

std::string ParsePollerBackendName(PollerBackend backend)
{
    if (backend == PollerBackend::Auto) { return "auto"; }
    if (backend == PollerBackend::Poll) { return "poll"; }
    if (backend == PollerBackend::Epoll) { return "epoll"; }
    
    throw std::invalid_argument("invalid poller backend");
}

LogLevel ParseLogLevel(std::string_view value)
{
    static std::unordered_map<std::string_view, LogLevel> logValueMap = {
        { "debug", LogLevel::Debug },
        { "info", LogLevel::Info},
        { "warn", LogLevel::Warn },
        { "error", LogLevel::Error },
        { "off", LogLevel::Off }
    };

    auto iter = logValueMap.find(value);
    if (iter == logValueMap.end()) {
        throw std::invalid_argument("log level must be debug, info, warn, error, or off");
    }

    return iter->second;
}

}  // namespace tinykv