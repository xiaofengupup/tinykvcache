/**
 * 服务器指标
 */
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace tinykv {

struct ServerMetricsSnapshot {
    std::uint64_t acceptedConnections{0};
    std::uint64_t closedConnections{0};
    std::uint64_t activeConnections{0};

    std::uint64_t bytesReceived{0};
    std::uint64_t bytesSent{0};

    std::uint64_t framesReceived{0};
    std::uint64_t commandsProcessed{0};
    std::uint64_t commandErrors{0};
    std::uint64_t protocolErrors{0};

    std::uint64_t slowClientDisconnects{0};
    std::uint64_t readPauseTransitions{0};
    std::uint64_t readResumeTransitions{0};

    std::uint64_t sweeperRuns{0};
    std::uint64_t expiredKeysRemoved{0};

    std::uint64_t forcedShutdownConnections{0};

    std::uint64_t maximumPendingWriteBytes{0};

    std::uint64_t commandLatencyCount{0};
    std::uint64_t commandLatencyTotalNanoseconds{0};
    std::uint64_t commandLatencyMaximumNanoseconds{0};

    // 延迟桶：<=10us <=50us <=100us <=500us <=1ms <=5ms >5ms
    std::array<std::uint64_t, 7> commandLatencyBuckets {};

    double AverageCommandLatencyMicroseconds() const noexcept;
};


class ServerMetrics {
public:
    void OnConnectionAccepted() noexcept;
    void OnConnectionClosed() noexcept;
    void AddBytesReceived(std::size_t bytes) noexcept;
    void AddBytesSent(std::size_t bytes) noexcept;
    void AddFramesReceived(std::size_t frames) noexcept;
    void OnCommandProcessed() noexcept;
    void OnCommandError() noexcept;
    void OnProtocolError() noexcept;
    void OnSlowClientDisconnected() noexcept;
    void OnReadPaused() noexcept;
    void OnReadResumed() noexcept;
    void OnSweeperRun(std::size_t removedKeys) noexcept;
    void AddForcedShutdownConnections(std::size_t connections) noexcept;
    void ObservePendingWriteBytes(std::size_t bytes) noexcept;
    void RecordCommandLatency(std::chrono::nanoseconds latency) noexcept;
    ServerMetricsSnapshot Snapshot() const noexcept;

private:
    static constexpr std::array<std::uint64_t, 6> LATENCY_UPPER_BOUNDS_NS {
        10'000U,
        50'000U,
        100'000U,
        500'000U,
        1'000'000U,
        5'000'000U
    };

    static void UpdateMaximum(std::atomic<std::uint64_t>& target, std::uint64_t candidate) noexcept;

private:
    std::atomic<std::uint64_t> m_acceptedConnections {0};
    std::atomic<std::uint64_t> m_closedConnections {0};
    std::atomic<std::uint64_t> m_activeConnections {0};
    std::atomic<std::uint64_t> m_bytesReceived {0};
    std::atomic<std::uint64_t> m_bytesSent {0};
    std::atomic<std::uint64_t> m_framesReceived {0};
    std::atomic<std::uint64_t> m_commandsProcessed {0};
    std::atomic<std::uint64_t> m_commandErrors {0};
    std::atomic<std::uint64_t> m_protocolErrors {0};
    std::atomic<std::uint64_t> m_slowClientDisconnects {0};
    std::atomic<std::uint64_t> m_readPauseTransitions {0};
    std::atomic<std::uint64_t> m_readResumeTransitions {0};
    std::atomic<std::uint64_t> m_sweeperRuns {0};
    std::atomic<std::uint64_t> m_expiredKeysRemoved {0};
    std::atomic<std::uint64_t> m_forcedShutdownConnections {0};
    std::atomic<std::uint64_t> m_maximumPendingWriteBytes {0};
    std::atomic<std::uint64_t> m_commandLatencyCount {0};
    std::atomic<std::uint64_t> m_commandLatencyTotalNanoseconds {0};
    std::atomic<std::uint64_t> m_commandLatencyMaximumNanoseconds {0};
    std::array<std::atomic<std::uint64_t>, 7> m_commandLatencyBuckets {};
};


} // namespace tinykv