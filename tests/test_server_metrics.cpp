#include "tinykv/observability/server_metrics.h"
#include "test_utils.h"

#include <chrono>

namespace {
    
void TestConnectionMetrics()
{
    tinykv::ServerMetrics metrics;
    metrics.OnConnectionAccepted();
    metrics.OnConnectionAccepted();
    metrics.OnConnectionClosed();

    const auto snapshot = metrics.Snapshot();

    TINYKV_CHECK(snapshot.acceptedConnections == 2);
    TINYKV_CHECK(snapshot.closedConnections == 1);
    TINYKV_CHECK(snapshot.activeConnections == 1);
}

void TestTrafficAndErrorMetrics()
{
    tinykv::ServerMetrics metrics;

    metrics.AddBytesReceived(100);
    metrics.AddBytesSent(80);
    metrics.AddFramesReceived(3);

    metrics.OnCommandProcessed();
    metrics.OnCommandError();
    metrics.OnProtocolError();

    const auto snapshot = metrics.Snapshot();

    TINYKV_CHECK(snapshot.bytesReceived == 100);
    TINYKV_CHECK(snapshot.bytesSent == 80);
    TINYKV_CHECK(snapshot.framesReceived == 3);
    TINYKV_CHECK(snapshot.commandsProcessed == 1);
    TINYKV_CHECK(snapshot.commandErrors == 1);
    TINYKV_CHECK(snapshot.protocolErrors == 1);
}

void TestLatencyMetrics()
{
    tinykv::ServerMetrics metrics;

    metrics.RecordCommandLatency(std::chrono::microseconds(5));
    metrics.RecordCommandLatency(std::chrono::microseconds(80));
    metrics.RecordCommandLatency(std::chrono::milliseconds(2));

    const auto snapshot = metrics.Snapshot();

    TINYKV_CHECK(snapshot.commandLatencyCount == 3);
    TINYKV_CHECK(snapshot.commandLatencyMaximumNanoseconds == 2'000'000U);
    TINYKV_CHECK(snapshot.commandLatencyBuckets[0] == 1);
    TINYKV_CHECK(snapshot.commandLatencyBuckets[2] == 1);
    TINYKV_CHECK(snapshot.commandLatencyBuckets[5] == 1);
    TINYKV_CHECK(snapshot.AverageCommandLatencyMicroseconds() > 0.0);
}

void TestMaximumPendingBytes()
{
    tinykv::ServerMetrics metrics;

    metrics.ObservePendingWriteBytes(100);
    metrics.ObservePendingWriteBytes(50);
    metrics.ObservePendingWriteBytes(200);

    const auto snapshot = metrics.Snapshot();

    TINYKV_CHECK(snapshot.maximumPendingWriteBytes == 200);
}

} // namespace

int main()
{
    TestConnectionMetrics();
    TestTrafficAndErrorMetrics();
    TestLatencyMetrics();
    TestMaximumPendingBytes();

    std::cout << "server metrics test passed" << std::endl;
    return 0;
}