#include "tinykv/observability/server_metrics.h"

#include <gtest/gtest.h>
#include <chrono>

namespace {
    
TEST(ServerMetrics, ConnectionMetrics)
{
    tinykv::ServerMetrics metrics;
    metrics.OnConnectionAccepted();
    metrics.OnConnectionAccepted();
    metrics.OnConnectionClosed();

    const auto snapshot = metrics.Snapshot();

    EXPECT_EQ(snapshot.acceptedConnections, 2);
    EXPECT_EQ(snapshot.closedConnections, 1);
    EXPECT_EQ(snapshot.activeConnections, 1);
}

TEST(ServerMetrics, TrafficAndErrorMetrics)
{
    tinykv::ServerMetrics metrics;

    metrics.AddBytesReceived(100);
    metrics.AddBytesSent(80);
    metrics.AddFramesReceived(3);

    metrics.OnCommandProcessed();
    metrics.OnCommandError();
    metrics.OnProtocolError();

    const auto snapshot = metrics.Snapshot();

    EXPECT_EQ(snapshot.bytesReceived, 100);
    EXPECT_EQ(snapshot.bytesSent, 80);
    EXPECT_EQ(snapshot.framesReceived, 3);
    EXPECT_EQ(snapshot.commandsProcessed, 1);
    EXPECT_EQ(snapshot.commandErrors, 1);
    EXPECT_EQ(snapshot.protocolErrors, 1);
}

TEST(ServerMetrics, LatencyMetrics)
{
    tinykv::ServerMetrics metrics;

    metrics.RecordCommandLatency(std::chrono::microseconds(5));
    metrics.RecordCommandLatency(std::chrono::microseconds(80));
    metrics.RecordCommandLatency(std::chrono::milliseconds(2));

    const auto snapshot = metrics.Snapshot();

    EXPECT_EQ(snapshot.commandLatencyCount, 3);
    EXPECT_EQ(snapshot.commandLatencyMaximumNanoseconds, 2'000'000U);
    EXPECT_EQ(snapshot.commandLatencyBuckets[0], 1);
    EXPECT_EQ(snapshot.commandLatencyBuckets[2], 1);
    EXPECT_EQ(snapshot.commandLatencyBuckets[5], 1);
    EXPECT_TRUE(snapshot.AverageCommandLatencyMicroseconds() > 0.0);
}

TEST(ServerMetrics, MaximumPendingBytes)
{
    tinykv::ServerMetrics metrics;

    metrics.ObservePendingWriteBytes(100);
    metrics.ObservePendingWriteBytes(50);
    metrics.ObservePendingWriteBytes(200);

    const auto snapshot = metrics.Snapshot();

    EXPECT_EQ(snapshot.maximumPendingWriteBytes, 200);
}

} // namespace
