#include "tinykv/net/sub_reactor.h"
#include "tinykv/net/poll/io_event.h"
#include "tinykv/observability/logger.h"
#include "tinykv/core/frame_codec.h"
#include "tinykv/core/command_parser.h"
#include "tinykv/core/command_executor.h"
#include "tinykv/net/poll/poller_factory.h"

#include <vector>
#include <chrono>
#include <stdexcept>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <iomanip>
#include <cmath>

namespace tinykv {

SubReactor::SubReactor(std::uint16_t id, ServerMetrics& metrics, KVStore& store,
    const NetworkOptions& networkOptions, const ReactorOptions& reactorOptions)
    : m_id(id), m_store(store), m_metrics(metrics),
      m_networkOptions(networkOptions), m_reactorOptions(reactorOptions)
{}

void SubReactor::Start()
{
    if (m_publishedState.load(std::memory_order_acquire) != ServerState::Created) {
        throw std::logic_error("Sub-reactor can only run once!");
    }

    m_poller = PollerFactory::CreatePoller(m_reactorOptions.pollerBackend);
    m_poller->Add(m_controlWakeup.ReadFd(), IoEvent::Read);
    PublishState(ServerState::Running);

    try {
        m_thread = std::thread(&SubReactor::EventLoop, this);
    } catch(...) {
        TINYKV_LOG_ERROR_MSG("Failed to start sub-reactor thread!");
        PublishState(ServerState::Created);
        try {
            m_poller->Remove(m_controlWakeup.ReadFd());
        } catch (...) {
        }

        m_poller.reset();
        throw;
    }
}

bool SubReactor::EnqueClient(ScopedFd clientFd)
{
    if (!clientFd.Valid()) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_controlMutex);
        // 收到停止通知后，不再接受新的连接
        if (m_stopRequested || m_forceStopRequested) {
            return false;
        }

        m_pendingClients.emplace_back(std::move(clientFd));
    }

    m_controlWakeup.Notify();
    return true;
}

void SubReactor::Stop(std::chrono::steady_clock::time_point deadline)
{
    {
        std::lock_guard<std::mutex> lock(m_controlMutex);
        if (!m_stopRequested || deadline < m_requestedShutdownDeadline) {
            m_requestedShutdownDeadline = deadline;
        }
        m_stopRequested = true;
    }

    m_controlWakeup.Notify();
}

void SubReactor::ForceStop()
{
    {
        std::lock_guard<std::mutex> lock(m_controlMutex);
        m_forceStopRequested = true;
    }

    m_controlWakeup.Notify();
}

bool SubReactor::IsStopped() const noexcept
{
    return m_publishedState.load(std::memory_order_acquire) == ServerState::Stopped;
}

bool SubReactor::HasFailed() const noexcept
{
    return m_failed.load(std::memory_order_acquire);
}

void SubReactor::Join()
{
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

std::uint16_t SubReactor::Id() const noexcept
{
    return m_id;
}

void SubReactor::EventLoop()
{
    try {
        while (m_state != ServerState::Stopped) {
            std::vector<ReadyEvent> readyEvents = m_poller->Wait(ComputeWaitTimeout());
            // 先处理 contorlWakeup 事件
            bool controlEventReceived = false;
            for (const ReadyEvent& rEvent : readyEvents) {
                if (rEvent.fd == m_controlWakeup.ReadFd()) {
                    m_controlWakeup.Drain();
                    controlEventReceived = true;
                    break;
                }
            }

            if (controlEventReceived) {
                ProcessControlRequests();
            }

            if (m_state == ServerState::Stopped) {
                break;
            }

            // 再处理所关联的 clients 事件
            for (const ReadyEvent& rEvent : readyEvents) {
                if (rEvent.fd == m_controlWakeup.ReadFd()) {
                    continue;
                }

                auto iter = m_clients.find(rEvent.fd);
                if (iter == m_clients.end()) {
                    continue;
                }

                Connection& conn = iter->second;
                if (HasIoEvent(rEvent.events, IoEvent::Error)) {
                    MarkClosed(conn);
                    continue;
                }

                if (m_state == ServerState::Running && HasIoEvent(rEvent.events, IoEvent::Read)) {
                    HandleClientRead(conn);
                }
                if (!conn.closed && HasIoEvent(rEvent.events, IoEvent::Write)) {
                    HandleClientWrite(conn);
                }
                if (!conn.closed && HasIoEvent(rEvent.events, IoEvent::Hangup)) {
                    conn.readPaused = true;
                    conn.closeAfterWrite = true;
                    if (conn.writeBuffer.Empty()) {
                        MarkClosed(conn);
                    }
                }
                if (!conn.closed) {
                    RefreshConnectionInterest(conn);
                }
            }
            CleanupClosedConnections();
            CheckShutdownProgress();
        }
    } catch (const std::exception& error) {
        m_failed.store(true, std::memory_order_release);
        try {
            TINYKV_LOG_ERROR("sub reactor {} terminated unexpectedly: {}", m_id, error.what());
        } catch (...) {
        }

        m_clients.clear();
        PublishState(ServerState::Stopped);
    } catch (...) {
        m_failed.store(true, std::memory_order_release);
        try {
            TINYKV_LOG_ERROR("sub reactor {} terminated by unknown exceptio", m_id);
        } catch (...) {
        }

        m_clients.clear();
        PublishState(ServerState::Stopped);
    }
}

std::chrono::milliseconds SubReactor::ComputeWaitTimeout() const noexcept
{
    if (m_state == ServerState::Running) {
        return std::chrono::milliseconds(500);
    }

    if (m_state != ServerState::Draining) {
        return std::chrono::milliseconds(0);
    }

    const auto now = std::chrono::steady_clock::now();
    if (now >= m_shutdownDeadline) {
        return std::chrono::milliseconds(0);
    }

    auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            m_shutdownDeadline - now
        );

    if (remaining.count() <= 0) {
        remaining = std::chrono::milliseconds(1);
    }

    return std::min(std::chrono::milliseconds(100), remaining);
}

void SubReactor::ProcessControlRequests()
{
    std::deque<ScopedFd> pendingClients;
    bool stopRequested = false;
    bool forceStopRequested = false;
    std::chrono::steady_clock::time_point deadline {};

    {
        std::lock_guard<std::mutex> lock(m_controlMutex);
        pendingClients.swap(m_pendingClients);
        stopRequested = m_stopRequested;
        forceStopRequested = m_forceStopRequested;
        deadline = m_requestedShutdownDeadline;
    }

    if (forceStopRequested) {
        pendingClients.clear();

        ForceCloseAllConnections();
        PublishState(ServerState::Stopped);
        return;
    }

    if (stopRequested) {
        pendingClients.clear();
        BeginGracefulShutdown(deadline);
        return;
    }

    for (auto &client : pendingClients) {
        try {
            AddClientInLoop(std::move(client));
        } catch (const std::exception& error) {
            // 单个连接失败不能导致整个 server 退出，这里记录 ERROR 日志
            TINYKV_LOG_ERROR("failed to initialize client on reactor {}, error={}", m_id, error.what());
        }
    }
}

void SubReactor::AddClientInLoop(ScopedFd client)
{
    const int rawFd = client.Get();

    auto [iter, inserted] = m_clients.emplace(rawFd, Connection(std::move(client)));
    if (!inserted) {
        throw std::logic_error("client fd already exist");
    }
    
    Connection& conn = iter->second;
    const IoEvent interests = DesiredEvents(conn);
    try {
        m_poller->Add(rawFd, interests);
        conn.registeredEvents = interests;
    } catch (...) {
        m_clients.erase(iter);
        throw;
    }

    m_metrics.OnConnectionAccepted();
    const auto snapshot = m_metrics.Snapshot();
    TINYKV_LOG_INFO(
        "client connected, sub_reactor_id={}, fd={}, active_connections={}",
        m_id, rawFd, snapshot.activeConnections
    );
}


void SubReactor::PublishState(ServerState state) noexcept
{
    m_state = state;
    m_publishedState.store(state, std::memory_order_release);
}

void SubReactor::HandleClientRead(Connection &conn)
{
    if (conn.closed || conn.readPaused || conn.closeAfterWrite) {
        return;
    }

    if (conn.readBuffer.size() >= m_networkOptions.maxReadBufferBytes) {
        RejectOversizedReadBuffer(conn);
        return;
    }

    std::array<char, 16U * 1024U> temp{};
    const std::size_t availableBytes = m_networkOptions.maxReadBufferBytes - conn.readBuffer.size();
    const std::size_t readSize = std::min(temp.size(), availableBytes);

    // 这里只执行一次 recv，是因为当前使用的是level-triggered poll：
    // socket 内核缓冲区仍然有数据 -> 下一个 poll 仍会报告 POLLIN -> 其他连接也获得一次处理机会
    ssize_t received = -1;
    do {
        received = ::recv(conn.fd.Get(), temp.data(), readSize, 0);
    } while (received < 0 && errno == EINTR);

    if (received == 0) {
        MarkClosed(conn);
        conn.readPaused = true;
        conn.closeAfterWrite = true;
        if (conn.writeBuffer.Empty()) {
            MarkClosed(conn);
        }
        return;
    }
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }

        MarkClosed(conn);
        return;
    }

    const std::size_t receivedBytes = static_cast<std::size_t>(received);
    m_metrics.AddBytesReceived(receivedBytes);

    std::vector<std::string> payloads;
    try {
        payloads = FrameCodec::Decode(conn.readBuffer, temp.data(), static_cast<std::size_t>(received));
    } catch (const std::exception& error) {
        m_metrics.OnProtocolError();
        TINYKV_LOG_WARN("protocol error, fd={}, error={}", conn.fd.Get(), error.what());
        if (QueueResponse(conn, "-ERR protocol error")) {
            conn.closeAfterWrite = true;
        }
        return;
    }
    m_metrics.AddFramesReceived(payloads.size());

    for (const auto &payload : payloads) {
        if (!ProcessPayload(conn, payload)) {
            return;
        }
    }
}

void SubReactor::RejectOversizedReadBuffer(Connection& conn)
{
    if (QueueResponse(conn, "-ERR request buffer too large")) {
        conn.closeAfterWrite = true;
    } else {
        MarkClosed(conn);
    }
}

void SubReactor::HandleClientWrite(Connection &conn)
{
    if (conn.closed) {
        return;
    }

    if (conn.writeBuffer.Empty()) {
        UpdateReadBackPressure(conn);
        if (conn.closeAfterWrite) {
            MarkClosed(conn);
        }

        return;
    }

    const std::size_t bytesToWrite = std::min(conn.writeBuffer.Size(), m_networkOptions.maxWriteBytesPerEvent);
    ssize_t written = -1;
    do {
        written = ::send(conn.fd.Get(), conn.writeBuffer.Data(), bytesToWrite, 0);
    } while (written < 0 && errno == EINTR);
    
    if (written > 0) {
        const std::size_t writtenBytes = static_cast<std::size_t>(written);
        m_metrics.AddBytesSent(writtenBytes);
        conn.writeBuffer.Consume(writtenBytes);
        UpdateReadBackPressure(conn);

        if (conn.writeBuffer.Empty() && conn.closeAfterWrite) {
            MarkClosed(conn);
        }
        return;
    }

    if (written == 0) {
        MarkClosed(conn);
        return;
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return;
    }

    MarkClosed(conn);
}

void SubReactor::UpdateReadBackPressure(Connection& conn)
{
    if (conn.closeAfterWrite || conn.closed) {
        return;
    }

    const std::size_t pendingBytes = conn.writeBuffer.Size();

    if (conn.readPaused) {
        if (pendingBytes <= m_networkOptions.writeLowWatermarkBytes) {
            conn.readPaused = false;
            m_metrics.OnReadResumed();
            TINYKV_LOG_DEBUG(
                "client read resumed, fd={}, pending_bytes={}", conn.fd.Get(), pendingBytes
            );
        }
        return;
    }

    if (pendingBytes >= m_networkOptions.writeHighWatermarkBytes) {
        conn.readPaused = true;

        m_metrics.OnReadPaused();
        TINYKV_LOG_DEBUG(
            "client read paused, fd={}, pending_bytes={}", conn.fd.Get(), pendingBytes
        );
    }
}

bool SubReactor::ProcessPayload(Connection &conn, const std::string &payload)
{
    const auto begin = std::chrono::steady_clock::now();

    const Command cmd = ParseCommand(payload);
    m_metrics.OnCommandProcessed();
    if (cmd.type == CommandType::Unknown) {
        m_metrics.OnCommandError();
    }

    std::string response;
    if (cmd.type == CommandType::Stats) {
        response = BuildStatsResponse();
    } else {
        response = CommandExecutor::Execute(m_store, cmd);
    }

    const auto elapsed = std::chrono::steady_clock::now() - begin;
    m_metrics.RecordCommandLatency(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed));

    if (!QueueResponse(conn, response)) {
        return false;
    }

    if (cmd.type == CommandType::Quit) {
        conn.closeAfterWrite = true; // 先把 +BYE 写完，再关闭连接。
        return false;
    }

    TINYKV_LOG_DEBUG(
        "command processed, reactor_id={}, fd={}, command_type={}, request_bytes={}, response_bytes={}",
        m_id, conn.fd.Get(), static_cast<int>(cmd.type), payload.size(), response.size()
    );

    return true;
}

std::string SubReactor::BuildStatsResponse()
{
    const auto storeStats = m_store.Stats();
    const auto metrics = m_metrics.Snapshot();
    const double averageLatencyUs = metrics.AverageCommandLatencyMicroseconds();
    const double maximumLatencyUs = static_cast<double>(metrics.commandLatencyMaximumNanoseconds) / 1000.0;
    const std::string p95UpperUs = EstimatePercentileBucketUs(metrics, 0.95);
    const std::string p99UpperUs = EstimatePercentileBucketUs(metrics, 0.99);

    std::ostringstream stream;

    stream
        << std::fixed
        << std::setprecision(2)
        << "+keys="
        << storeStats.keys
        << ",persistent="
        << storeStats.persistentKeys
        << ",expiring="
        << storeStats.expiringKeys
        << ",connections_active="
        << metrics.activeConnections
        << ",connections_accepted="
        << metrics.acceptedConnections
        << ",connections_closed="
        << metrics.closedConnections
        << ",bytes_received="
        << metrics.bytesReceived
        << ",bytes_sent="
        << metrics.bytesSent
        << ",frames_received="
        << metrics.framesReceived
        << ",commands="
        << metrics.commandsProcessed
        << ",command_errors="
        << metrics.commandErrors
        << ",protocol_errors="
        << metrics.protocolErrors
        << ",slow_clients="
        << metrics.slowClientDisconnects
        << ",read_pauses="
        << metrics.readPauseTransitions
        << ",read_resumes="
        << metrics.readResumeTransitions
        << ",expired_removed="
        << metrics.expiredKeysRemoved
        << ",max_pending_write_bytes="
        << metrics.maximumPendingWriteBytes
        << ",latency_avg_us="
        << averageLatencyUs
        << ",latency_max_us="
        << maximumLatencyUs
        << ",latency_p95_upper_us="
        << p95UpperUs
        << ",latency_p99_upper_us="
        << p99UpperUs;

    return stream.str();
}

// 延迟桶估算百分位
std::string SubReactor::EstimatePercentileBucketUs(
    const ServerMetricsSnapshot& snapshot, double percentile) noexcept
{
    if (snapshot.commandLatencyCount == 0U) {
        return "0";
    }

    const auto targetRank = static_cast<std::uint64_t>(
        std::ceil(static_cast<double>(snapshot.commandLatencyCount) * percentile)
    );

    constexpr std::array<std::uint64_t, 6> upperBoundsUs {
        10U, 50U, 100U, 500U, 1000U, 5000U
    };

    std::uint64_t accumulated = 0;
    for (std::size_t i = 0; i < upperBoundsUs.size(); ++i) {
        accumulated += snapshot.commandLatencyBuckets[i];
        if (accumulated >= targetRank) {
            return std::to_string(upperBoundsUs[i]);
        }
    }

    return ">5000";
}

bool SubReactor::QueueResponse(Connection& conn, const std::string& response)
{
    std::string frame;
    try {
        frame = FrameCodec::Encode(response);
    } catch (const std::exception&) {
        MarkClosed(conn);
        return false;
    }

    const std::size_t pendingBytes = conn.writeBuffer.Size();
    const std::size_t hardLimit = m_networkOptions.writeHardLimitBytes;

    if (pendingBytes > hardLimit || frame.size() > hardLimit - pendingBytes) {
        m_metrics.OnSlowClientDisconnected();
        TINYKV_LOG_WARN(
            "close slow client, fd={}, pending_bytes={}, new_frame_bytes={}", conn.fd.Get(), pendingBytes, frame.size()
        );

        MarkClosed(conn);
        return false;
    }

    conn.writeBuffer.Append(frame);
    m_metrics.ObservePendingWriteBytes(conn.writeBuffer.Size());

    UpdateReadBackPressure(conn);
    return true;
}

void SubReactor::MarkClosed(Connection& conn)
{
    conn.closed = true;
    conn.readPaused = false;
    conn.closeAfterWrite = false;

    // 连接已经关闭或异常时，未发送数据不再保留。
    conn.readBuffer.clear();
    conn.writeBuffer.Clear();
}

void SubReactor::CleanupClosedConnections()
{
    for (auto it = m_clients.begin(); it != m_clients.end();) {
        if (!it->second.closed) {
            ++it;
            continue;
        }

        const int fd = it->first;
        try {
            m_poller->Remove(fd);
        } catch (const std::exception& error) {
            TINYKV_LOG_WARN(
                "failed to remove closed client from poller, reactor_id={}, fd={}, error={}",
                m_id, fd, error.what()
            );
        }
        
        m_metrics.OnConnectionClosed();
        const auto snapshot = m_metrics.Snapshot();
        it = m_clients.erase(it);
        TINYKV_LOG_INFO(
            "client disconnected, reactor_id={}, client_fd={}, active_connections={}", 
            m_id, fd, snapshot.activeConnections
        );
    }
}

void SubReactor::CheckShutdownProgress()
{
    if (m_state != ServerState::Draining) {
        return;
    }

    if (m_clients.empty()) {
        PublishState(ServerState::Stopped);
        return;
    }

    if (std::chrono::steady_clock::now() >= m_shutdownDeadline) {
        ForceCloseAllConnections();
        PublishState(ServerState::Stopped);
    }
}

void SubReactor::ForceCloseAllConnections()
{
    const std::size_t connectionCount = m_clients.size();
    m_metrics.AddForcedShutdownConnections(connectionCount);

    TINYKV_LOG_WARN("graceful shutdown timed out, sub reactor {} force closing {} client(s)",
        m_id, connectionCount);
    for (auto& item : m_clients) {
        MarkClosed(item.second);
    }

    CleanupClosedConnections();
}

void SubReactor::BeginGracefulShutdown(std::chrono::steady_clock::time_point deadline)
{
    if (m_state != ServerState::Running) {
        return;
    }

    m_shutdownDeadline = deadline;
    PublishState(ServerState::Draining);
    TINYKV_LOG_INFO("sub reactor graceful shutdown started, id={}, clients size={}", m_id, m_clients.size());

    // 不再读取已有连接的新请求，但允许已经排队的响应继续发送
    for (auto &item : m_clients) {
        Connection &conn = item.second;
        conn.readPaused = true;
        conn.closeAfterWrite = true;
        if (conn.writeBuffer.Empty()) {
            MarkClosed(conn);
            continue;
        }
        TINYKV_LOG_INFO("Closing connection {}", conn.fd.Get());
        RefreshConnectionInterest(conn);
    }

    CleanupClosedConnections();
    if (m_clients.empty()) {
        PublishState(ServerState::Stopped);
    }
}

void SubReactor::RefreshConnectionInterest(Connection& conn)
{
    if (conn.closed) {
        return;
    }

    UpdateReadBackPressure(conn);
    
    if (conn.closeAfterWrite && conn.writeBuffer.Empty()) {
        MarkClosed(conn);
        return;
    }

    const IoEvent desired = DesiredEvents(conn);
    if (desired == IoEvent::None) {
        MarkClosed(conn);
        return;
    }

    if (desired == conn.registeredEvents) {
        return;
    }

    m_poller->Modity(conn.fd.Get(), desired);
    conn.registeredEvents = desired;
}

IoEvent SubReactor::DesiredEvents(const Connection& conn) const noexcept
{
    if (conn.closed) {
        return IoEvent::None;
    }

    IoEvent interests = IoEvent::None;
    if (!conn.readPaused && !conn.closeAfterWrite) {
        interests |= IoEvent::Read;
    }
    if (!conn.writeBuffer.Empty()) {
        interests |= IoEvent::Write;
    }

    return interests;
}

} // namespace tinykv