#include "tinykv/observability/server_metrics.h"

#include <algorithm>

namespace tinykv {

double ServerMetricsSnapshot::AverageCommandLatencyMicroseconds() const noexcept
{
    if (commandLatencyCount == 0U) {
        return 0.0;
    }

    const double totalMicroseconds = static_cast<double>(commandLatencyTotalNanoseconds) / 1000.0;
    return totalMicroseconds / static_cast<double>(commandLatencyCount);
}

void ServerMetrics::OnConnectionAccepted() noexcept
{
    m_acceptedConnections.fetch_add(1U, std::memory_order_relaxed);
    m_activeConnections.fetch_add(1U, std::memory_order_relaxed);
}

void ServerMetrics::OnConnectionClosed() noexcept
{
    m_closedConnections.fetch_add(1U, std::memory_order_relaxed);
    m_activeConnections.fetch_sub(1U, std::memory_order_relaxed);
}

void ServerMetrics::AddBytesReceived(std::size_t bytes) noexcept
{
    m_bytesReceived.fetch_add(static_cast<std::uint64_t>(bytes), std::memory_order_relaxed);
}

void ServerMetrics::AddBytesSent(std::size_t bytes) noexcept
{
    m_bytesSent.fetch_add(static_cast<std::uint64_t>(bytes), std::memory_order_relaxed);
}

void ServerMetrics::AddFramesReceived(std::size_t frames) noexcept
{
    m_framesReceived.fetch_add(static_cast<std::uint64_t>(frames),std::memory_order_relaxed);
}

void ServerMetrics::OnCommandProcessed() noexcept
{
    m_commandsProcessed.fetch_add(1U, std::memory_order_relaxed);
}

void ServerMetrics::OnCommandError() noexcept {
    m_commandErrors.fetch_add(1U, std::memory_order_relaxed);
}

void ServerMetrics::OnProtocolError() noexcept
{
    m_protocolErrors.fetch_add(1U, std::memory_order_relaxed );
}

void ServerMetrics::OnSlowClientDisconnected() noexcept
{
    m_slowClientDisconnects.fetch_add( 1U, std::memory_order_relaxed);
}

void ServerMetrics::OnReadPaused() noexcept
{
    m_readPauseTransitions.fetch_add(1U, std::memory_order_relaxed);
}

void ServerMetrics::OnReadResumed() noexcept
{
    m_readResumeTransitions.fetch_add( 1U, std::memory_order_relaxed);
}

void ServerMetrics::OnSweeperRun(std::size_t removedKeys) noexcept
{
    m_sweeperRuns.fetch_add(1U,std::memory_order_relaxed);
    m_expiredKeysRemoved.fetch_add(static_cast<std::uint64_t>(removedKeys), std::memory_order_relaxed);
}

void ServerMetrics::AddForcedShutdownConnections(std::size_t connections) noexcept
{
    m_forcedShutdownConnections.fetch_add(static_cast<std::uint64_t>(connections),std::memory_order_relaxed);
}

void ServerMetrics::ObservePendingWriteBytes(std::size_t bytes) noexcept
{
    UpdateMaximum(m_maximumPendingWriteBytes, static_cast<std::uint64_t>(bytes));
}

void ServerMetrics::RecordCommandLatency(std::chrono::nanoseconds latency) noexcept
{
    const auto rawCount = latency.count();
    const std::uint64_t latencyNs = rawCount > 0 ? static_cast<std::uint64_t>(rawCount) : 0U;

    m_commandLatencyCount.fetch_add(1U, std::memory_order_relaxed);
    m_commandLatencyTotalNanoseconds.fetch_add(latencyNs, std::memory_order_relaxed);

    UpdateMaximum(m_commandLatencyMaximumNanoseconds, latencyNs);

    std::size_t bucketIndex = 0;
    while (bucketIndex < LATENCY_UPPER_BOUNDS_NS.size() && latencyNs > LATENCY_UPPER_BOUNDS_NS[bucketIndex]) {
        ++bucketIndex;
    }
    m_commandLatencyBuckets[bucketIndex].fetch_add(1U, std::memory_order_relaxed);
}

ServerMetricsSnapshot ServerMetrics::Snapshot() const noexcept
{
    ServerMetricsSnapshot snapshot;

    snapshot.acceptedConnections = m_acceptedConnections.load(std::memory_order_relaxed);
    snapshot.closedConnections = m_closedConnections.load(std::memory_order_relaxed);
    snapshot.activeConnections = m_activeConnections.load(std::memory_order_relaxed);
    snapshot.bytesReceived = m_bytesReceived.load(std::memory_order_relaxed);
    snapshot.bytesSent = m_bytesSent.load(std::memory_order_relaxed);
    snapshot.framesReceived = m_framesReceived.load(std::memory_order_relaxed);
    snapshot.commandsProcessed = m_commandsProcessed.load(std::memory_order_relaxed);
    snapshot.commandErrors = m_commandErrors.load(std::memory_order_relaxed);
    snapshot.protocolErrors = m_protocolErrors.load(std::memory_order_relaxed);
    snapshot.slowClientDisconnects = m_slowClientDisconnects.load(std::memory_order_relaxed);
    snapshot.readPauseTransitions = m_readPauseTransitions.load(std::memory_order_relaxed);
    snapshot.readResumeTransitions = m_readResumeTransitions.load(std::memory_order_relaxed);
    snapshot.sweeperRuns = m_sweeperRuns.load(std::memory_order_relaxed);
    snapshot.expiredKeysRemoved = m_expiredKeysRemoved.load(std::memory_order_relaxed);
    snapshot.forcedShutdownConnections = m_forcedShutdownConnections.load(std::memory_order_relaxed);
    snapshot.maximumPendingWriteBytes = m_maximumPendingWriteBytes.load(std::memory_order_relaxed);
    snapshot.commandLatencyCount = m_commandLatencyCount.load(std::memory_order_relaxed);
    snapshot.commandLatencyTotalNanoseconds = m_commandLatencyTotalNanoseconds.load(std::memory_order_relaxed);
    snapshot.commandLatencyMaximumNanoseconds = m_commandLatencyMaximumNanoseconds.load(std::memory_order_relaxed);

    for (std::size_t index = 0; index < snapshot.commandLatencyBuckets.size(); ++index) {
        snapshot.commandLatencyBuckets[index] = m_commandLatencyBuckets[index].load(std::memory_order_relaxed);
    }

    return snapshot;
}

void ServerMetrics::UpdateMaximum(std::atomic<std::uint64_t>& target,std::uint64_t candidate) noexcept
{
    std::uint64_t current = target.load(std::memory_order_relaxed);
    while (candidate > current &&
        !target.compare_exchange_weak(current, candidate, std::memory_order_relaxed, std::memory_order_relaxed)) {
        // compare_exchange 失败时会把最新值写回 current。
    }
}
    
} // namespace tinykv
