#include "tinykv/config/command_line.h"
#include "tinykv/config/config_parser.h"
#include "tinykv/net/poll/poller_factory.h"
#include "tinykv/observability/logger.h"

#include <string_view>
#include <stdexcept>
#include <charconv>
#include <thread>
#include <algorithm>

namespace tinykv {

namespace {

std::string_view RequireNextValue(int &idx, const int argc, char *argv[], const std::string_view option)
{
    if (idx + 1 >= argc) {
        throw std::invalid_argument("Missing value for option:" + std::string(option));
    }

    ++idx;
    const std::string_view value = argv[idx];
    if (value.empty()) {
        throw std::invalid_argument("Empty value for option:" + std::string(option));
    }

    return value;
}

bool StartsWith(const std::string_view value, const std::string_view prefix)
{
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool MatchesOption(const std::string_view argument, const std::string_view option)
{
    if (argument == option) {
        return true;
    }

    const std::string prefix = std::string(option) + "=";
    return StartsWith(argument, prefix);
}

std::optional<std::string_view> ParseInlineValue(
    const std::string_view argument, const std::string_view option)
{
    if (argument == option) {
        return std::nullopt;
    }

    const std::string prefix = std::string(option) + "=";
    if (!StartsWith(argument, prefix)) {
        return std::nullopt;
    }

    const std::string_view value = argument.substr(prefix.size());
    if (value.empty()) {
        throw std::invalid_argument("empty value for option " + std::string(option));
    }

    return value;
}

std::string_view ReadOptionValue(const std::string_view argument, const std::string_view option,
    int& index, const int argc, char* argv[])
{
    if (argument == option) {
        return RequireNextValue(index, argc, argv, option);
    }

    const auto inlineValue = ParseInlineValue(argument, option);
    if (!inlineValue.has_value()) {
        throw std::logic_error("argument does not match option");
    }

    return *inlineValue;
}

template<typename T>
T ParseInteger(const std::string_view value, const std::string_view option)
{
    T result{};

    const char* begin = value.data();
    const char* end = value.data() + value.size();

    const auto [ptr, error] = std::from_chars(begin, end, result);
    if (error != std::errc{} || ptr != end) {
        throw std::invalid_argument("invalid integer for option " +
            std::string(option) +": " + std::string(value)
        );
    }

    return result;
}

} // namespace

std::optional<std::string> FindConfigPath(int argc, char *argv[])
{
    std::optional<std::string> configPath;

    for(int i = 1; i < argc; i++) {
        const std::string_view argument = argv[i];

        if (argument == "--config" || argument == "-c") {
            const std::string_view value = RequireNextValue(i, argc, argv, argument);
            if (configPath.has_value()) {
                throw std::invalid_argument("configuration file specified multiple times");
            }

            configPath = std::string(value);
            continue;
        }

        if (MatchesOption(argument, "--config")) {
            const std::string_view value = ReadOptionValue(argument, "--config", i, argc, argv);
            if (configPath.has_value()) {
                throw std::invalid_argument("configuration file specified multiple times");
            }
            configPath = std::string(value);
        }
    }

    return configPath;
}

void ApplyCommandLineOverrides(const int argc, char* argv[], AppConfig& config)
{
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];

        /*
         * --config 已经由 FindConfigPath() 处理。
         *
         * 第二遍解析时必须跳过其 value，否则 config/tinykv.toml 会被当成未知参数。
         */
        if (argument == "--config" ||argument == "-c") {
            RequireNextValue(i, argc, argv, argument);
            continue;
        }

        if (MatchesOption(argument, "--config")) {
            ReadOptionValue(argument, "--config", i, argc, argv);
            continue;
        }

        if (MatchesOption(argument, "--host")) {
            const std::string_view value = ReadOptionValue(argument, "--host", i, argc, argv);
            config.host = std::string(value);
            continue;
        }

        if (MatchesOption(argument, "--port")) {
            const std::string_view value = ReadOptionValue(argument, "--port", i, argc, argv);
            config.port = ParseInteger<int>(value, "--port");
            continue;
        }

        if (MatchesOption(argument, "--poller")) {
            const std::string_view value = ReadOptionValue(argument, "--poller", i, argc, argv);
            config.reactor.pollerBackend = ParsePollerBackend(value);
            continue;
        }

        if (MatchesOption(argument, "--sub-reactors")) {
            const std::string_view value = ReadOptionValue(argument, "--sub-reactors", i, argc, argv);
            config.reactor.subReactorCount = ParseInteger<std::size_t>(value, "--sub-reactors");
            if (config.reactor.subReactorCount == 0) {
                // 为 0 表示 Auto，此时设置为计算系统 CPU 核心数的一半，并将最终结果限制在 1 到 8 之间。
                config.reactor.subReactorCount = std::clamp(std::thread::hardware_concurrency() / 2, 1U, 8U);
            }
            continue;
        }

        if (MatchesOption(argument, "--log-level")) {
            const std::string_view value = ReadOptionValue(argument, "--log-level", i, argc, argv);
            config.logLevel = ParseLogLevel(value);
            continue;
        }

        if (argument == "--help" || argument == "-h") {
            continue;
        }

        throw std::invalid_argument("unknown command line option: " + std::string(argument));
    }
}

} // namespace tinykv