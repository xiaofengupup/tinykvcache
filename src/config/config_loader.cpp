#include "tinykv/config/config_loader.h"
#include "tinykv/config/config_parser.h"
#include "tinykv/net/poll/poller_factory.h"

#include <thread>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <limits>

#include <toml++/toml.h>
#include <fmt/format.h>

namespace tinykv {

namespace {

const toml::table* FindTable(const toml::table& root, const std::string_view &section)
{
    const toml::node* node = root.get(section);
    if (node == nullptr) {
        return nullptr;
    }

    const toml::table* table = node->as_table();
    if (table == nullptr) {
        throw std::invalid_argument(fmt::format("configuration section [{}] must be a table", section));
    }

    return table;
}

const toml::node* FindNode(
    const toml::table& root, const std::string_view section, const std::string_view key)
{
    const toml::table* table = FindTable(root, section);
    if (table == nullptr) {
        return nullptr;
    }

    return table->get(key);
}

std::optional<std::string> ReadString(
    const toml::table& root, const std::string_view section, const std::string_view key)
{
    const toml::node* node = FindNode(root, section, key);
    if (node == nullptr) {
        return std::nullopt;
    }

    const auto value = node->value<std::string>();
    if (!value.has_value()) {
        throw std::invalid_argument(fmt::format("configuration '{}.{}' must be a string", section, key));
    }
    return value;
}

std::optional<std::int64_t> ReadInteger(
    const toml::table& root, const std::string_view section, const std::string_view key)
{
    const toml::node* node = FindNode(root, section, key);
    if (node == nullptr) {
        return std::nullopt;
    }

    const auto value = node->value<std::int64_t>();
    if (!value.has_value()) {
        throw std::invalid_argument(fmt::format("configuration '{}.{}' must be an integer", section, key));
    }
    
    return value;
}

std::size_t ToSize(const std::int64_t value, const std::string_view name)
{
    if (value < 0) {
        throw std::invalid_argument(fmt::format("configuration '{}' must not be negative", name));
    }

    const auto unsignedValue = static_cast<std::uint64_t>(value);
    if (unsignedValue > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument(fmt::format("configuration '{}' is too large", name));
    }

    return static_cast<std::size_t>(unsignedValue);
}

} // namespace

AppConfig ConfigLoader::LoadFromFile(const std::string &path)
{
    // 解析 toml 配置文件
    const toml::table table = toml::parse_file(path);

    AppConfig config;

    // server 相关配置
    ReadServerConfig(config, table);

    // reactor 配置
    ReadReactorConfig(config, table);

    // network 配置
    ReadNetworkConfig(config, table);

    // shutdown 配置
    ReadShutdownConfig(config, table);

    // TTL 清理线程配置
    ReadTTLConfig(config, table);

    // 日志相关配置
    ReadLogConfig(config, table);

    Validate(config);
    return config;
}

void ConfigLoader::Validate(const AppConfig& config)
{
    if (config.host.empty()) {
        throw std::invalid_argument("server.host must not be empty");
    }

    if (config.port < 1 || config.port > 65535) {
        throw std::invalid_argument("port must be between 1 and 65535");
    }

    const auto& networkConfig = config.network;
    if (networkConfig.maxReadBufferBytes < 4U) {
        throw std::invalid_argument("maxReadBufferBytes must be at least 4");
    }

    if (networkConfig.writeLowWatermarkBytes > networkConfig.writeHighWatermarkBytes) {
        throw std::invalid_argument("write low watermark exceeds high watermark");
    }

    if (networkConfig.writeHighWatermarkBytes > networkConfig.writeHardLimitBytes) {
        throw std::invalid_argument("write high watermark exceeds hard limit");
    }

    if (networkConfig.maxWriteBytesPerEvent == 0U) {
        throw std::invalid_argument("maxWriteBytesPerEvent must be greater than zero");
    }

    if (networkConfig.maxAcceptsPerEvent == 0U) {
        throw std::invalid_argument("maxAcceptsPerEvent must be greater than zero");
    }

    if (config.reactor.subReactorCount > 16U) {
        throw std::invalid_argument("sub_reactors must be between 0 and 16");
    }

    if (config.shutdown.gracefulTimeout.count() <= 0) {
        throw std::invalid_argument(
            "shutdown.graceful_timeout_ms must be greater than zero"
        );
    }

    if (config.sweepInterval.count() < 0) {
        throw std::invalid_argument(
            "ttl.sweep_interval_ms must not be negative"
        );
    }
}

std::string ConfigLoader::BuildConfigStr(const AppConfig &config) noexcept
{
    return fmt::format(
        "host={}, port={}, poller={}, sub_reactors={}, "
        "max_read_buffer_byte={}, write_high_watermark_bytes={}, write_low_watermark_bytes={}, "
        "write_hard_limit_bytes={}, max_write_bytes_per_event={}, max_accepts_per_event={}, "
        "graceful_shutdown_timeout={}, log_level={}",
        config.host, config.port, 
        ParsePollerBackendName(config.reactor.pollerBackend), config.reactor.subReactorCount, 
        config.network.maxReadBufferBytes, config.network.writeHighWatermarkBytes,
        config.network.writeLowWatermarkBytes, config.network.writeHardLimitBytes,
        config.network.maxWriteBytesPerEvent, config.network.maxAcceptsPerEvent,
        config.shutdown.gracefulTimeout.count(),
        LogLevelName(config.logLevel)
    );
}

void ConfigLoader::ReadServerConfig(AppConfig& config, const toml::table& table)
{
    // 读取 host
    if (const auto value = ReadString(table, "server", "host")) {
        config.host = *value;
    }

    // 读取 port
    if (const auto value = ReadInteger(table, "server", "port")) {
        if (*value < 1 || *value > 65535) {
            throw std::invalid_argument("configuration 'server.port'must be between 1 and 65535");
        }
        config.port = static_cast<int>(*value);
    }
}
void ConfigLoader::ReadNetworkConfig(AppConfig& config, const toml::table& table)
{
    // 读取 poller backend
    if (const auto value = ReadString(table, "reactor", "poller")) {
        config.reactor.pollerBackend = ParsePollerBackend(*value);
    }

    // 读取 sub reactors
    if (const auto value = ReadInteger(table, "reactor", "sub_reactors")) {
        if (*value < 0 || *value > 16) {
            throw std::invalid_argument("configuration 'reactor.sub_reactors' must be between 0 and 16");
        }

        config.reactor.subReactorCount = static_cast<std::size_t>(*value);
        if (config.reactor.subReactorCount == 0) {
            // 为 0 表示 Auto，此时设置为计算系统 CPU 核心数的一半，并将最终结果限制在 1 到 8 之间。
            config.reactor.subReactorCount = std::clamp(std::thread::hardware_concurrency() / 2, 1U, 8U);
        }
    }
}
void ConfigLoader::ReadReactorConfig(AppConfig& config, const toml::table& table)
{
    if (const auto value = ReadInteger(table, "network", "max_read_buffer_bytes")) {
        config.network.maxReadBufferBytes = ToSize(*value, "network.max_read_buffer_bytes");
    }

    if (const auto value = ReadInteger(table, "network", "write_low_watermark_bytes")) {
        config.network.writeLowWatermarkBytes = ToSize(*value, "network.write_low_watermark_bytes");
    }

    if (const auto value = ReadInteger(table, "network", "write_high_watermark_bytes")) {
        config.network.writeHighWatermarkBytes = ToSize(*value, "network.write_high_watermark_bytes");
    }

    if (const auto value = ReadInteger(table, "network", "write_hard_limit_bytes")) {
        config.network.writeHardLimitBytes = ToSize(*value, "network.write_hard_limit_bytes");
    }

    if (const auto value = ReadInteger(table, "network", "max_write_bytes_per_event")) {
        config.network.maxWriteBytesPerEvent = ToSize(*value, "network.max_write_bytes_per_event");
    }

    if (const auto value = ReadInteger(table, "network", "max_accepts_per_event")) {
        config.network.maxAcceptsPerEvent = ToSize(*value, "network.max_accepts_per_event");
    }
}

void ConfigLoader::ReadShutdownConfig(AppConfig& config, const toml::table& table)
{
    if (const auto value = ReadInteger(table, "shutdown", "graceful_timeout_ms")) {
        if (*value <= 0) {
            throw std::invalid_argument(
                "configuration 'shutdown.graceful_timeout_ms' must be greater than zero"
            );
        }

        config.shutdown.gracefulTimeout = std::chrono::milliseconds(*value);
    }
}
void ConfigLoader::ReadTTLConfig(AppConfig& config, const toml::table& table)
{
    if (const auto value = ReadInteger(table, "ttl", "sweep_interval_ms")) {
        if (*value <= 0) {
            throw std::invalid_argument(
                "configuration 'ttl.sweep_interval_ms' must not be negative"
            );
        }

        config.sweepInterval = std::chrono::milliseconds(*value);
    }
}
void ConfigLoader::ReadLogConfig(AppConfig& config, const toml::table& table)
{
    if (const auto value = ReadString(table, "logging", "level")) {
        config.logLevel = ParseLogLevel(*value);
    }
}

} // namespace tinykv