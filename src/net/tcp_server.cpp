#include "tinykv/net/tcp_server.h"
#include "tinykv/core/command_parser.h"
#include "tinykv/core/command_executor.h"
#include "tinykv/core/frame_codec.h"
#include "tinykv/net/socket_util.h"
#include "tinykv/net/poll/poller_factory.h"
#include "tinykv/observability/logger.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <chrono>
#include <thread>
#include <array>
#include <iomanip>
#include <sstream>

namespace tinykv {

namespace {

std::string ErrorMessage(const char* prefix)
{
    return std::string(prefix) + ": " + std::strerror(errno);
}

bool IsWouldBlockError()
{   
    // 如果 recv() 没有数据，就返回：EAGAIN / EWOULDBLOCK
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

}

TcpServer::TcpServer(std::string host, int port, std::chrono::seconds sweepInterval, TcpServerOptions options)
    : m_host(std::move(host)), m_port(port), m_sweepInterval(sweepInterval), m_options(options)
{
    if (m_options.maxReadBufferBytes < 4U) {
        throw std::invalid_argument("maxReadBufferBytes must be at least 4");
    }

    if (m_options.writeLowWatermarkBytes > m_options.writeHighWatermarkBytes) {
        throw std::invalid_argument("write low watermark exceeds high watermark");
    }

    if (m_options.writeHighWatermarkBytes > m_options.writeHardLimitBytes) {
        throw std::invalid_argument("write high watermark exceeds hard limit");
    }

    if (m_options.maxWriteBytesPerEvent == 0U) {
        throw std::invalid_argument("maxWriteBytesPerEvent must be greater than zero");
    }

    if (m_options.maxAcceptsPerEvent == 0U) {
        throw std::invalid_argument("maxAcceptsPerEvent must be greater than zero");
    }
}

TcpServer::~TcpServer()
{
    Stop();
    StopSweeperThread();
}

void TcpServer::Run()
{
    if (m_state != ServerState::Created) {
        throw std::logic_error("TcpServer can only run once!");
    }

    m_poller = CreatePoller(m_options.pollerBackend);
    m_listenFd = CreateListenSocket(m_host, m_port);
    SetNonBlocking(m_listenFd.Get());
    m_poller->Add(m_listenFd.Get(), IoEvent::Read);
    m_poller->Add(m_stopWakup.ReadFd(), IoEvent::Read);

    m_state = ServerState::Running;
    StartSweeperThread();
    Logger::Instance().Info(
        "server started, host=", m_host, ", port=", m_port, ", poller=", m_poller->Name());

    try {
        while (m_state != ServerState::Stopped) {
            const std::vector<ReadyEvent> readyEvents = m_poller->Wait(ComputeWaitTimeout());
            bool stopEventReceived = m_stopRequested.load(std::memory_order_acquire);
            
            // 先处理停止通知，避免同一批事件中继续 accept 新连接
            for (const ReadyEvent& ready : readyEvents) {
                if (ready.fd == m_stopWakup.ReadFd()) {
                    m_stopWakup.Drain();
                    stopEventReceived = true;
                }
            }
            if (stopEventReceived) {
                BeginGracefulShutdown();
            }

            for (const ReadyEvent& ready : readyEvents) {
                if (ready.fd == m_stopWakup.ReadFd()) {
                    continue;
                }
                // 处理 m_listenFd 相关事件
                if (m_listenFd.Valid() && ready.fd == m_listenFd.Get()) {
                    if (m_state == ServerState::Running && HasIoEvent(ready.events, IoEvent::Read)) {
                        AcceptNewClients();
                    }
                    continue;
                }

                // 处理 client 相关事件
                auto iter = m_clients.find(ready.fd);
                if (iter == m_clients.end()) {
                    continue;
                }

                Connection& conn = iter->second;
                if (HasIoEvent(ready.events, IoEvent::Error)) {
                    MarkClosed(conn);
                    continue;
                }
                // Draining 状态下不再读取新请求
                if (m_state == ServerState::Running && HasIoEvent(ready.events, IoEvent::Read)) {
                    HandleClientRead(conn);
                }
                if (!conn.closed && HasIoEvent(ready.events, IoEvent::Write)) {
                    HandleClientWrite(conn);
                }
                if (!conn.closed && HasIoEvent(ready.events, IoEvent::Hangup)) {
                    MarkClosed(conn);
                }
                if (!conn.closed) {
                    RefreshConnectionInterest(conn);
                }
            }
            CleanupClosedConnections();
            CheckShutdownProgress();
        }
    } catch (const std::exception&) {
        StopSweeperThread();
        CleanupReactor();
        m_state = ServerState::Stopped;
        throw;
    }

    StopSweeperThread();
    CleanupReactor();
    m_state = ServerState::Stopped;

    Logger::Instance().Info("server stopped gracefully");
}

/**
 * 只做通知动作，真正的状态修改全部由事件循环线程完成
 */
void TcpServer::Stop()
{
    m_stopRequested.store(true, std::memory_order_release);
    m_stopWakup.Notify();
}

int TcpServer::StopNotificationFd() const noexcept
{
    return m_stopWakup.WriteFd();
}

void TcpServer::BeginGracefulShutdown()
{
    if (m_state != ServerState::Running) {
        return;
    }

    m_state = ServerState::Draining;
    m_shutdownDeadline = std::chrono::steady_clock::now() + m_options.gracefulShutdownTimeout;

    Logger::Instance().Info("graceful shutdown started, clients size=", m_clients.size());
    
    // 停止接受新连接
    if (m_listenFd.Valid()) {
        m_poller->Remove(m_listenFd.Get());
        m_listenFd.Reset();
    }

    // 不再读取已有连接的新请求，但允许已经排队的响应继续发送
    for (auto &item : m_clients) {
        Connection &conn = item.second;
        conn.readPaused = true;
        conn.closeAfterWrite = true;
        if (conn.writeBuffer.Empty()) {
            MarkClosed(conn);
            continue;
        }

        RefreshConnectionInterest(conn);
    }

    CleanupClosedConnections();
    if (m_clients.empty()) {
        m_state = ServerState::Stopped;
    }
}

void TcpServer::CheckShutdownProgress()
{
    if (m_state != ServerState::Draining) {
        return;
    }

    if (m_clients.empty()) {
        m_state = ServerState::Stopped;
        return;
    }

    if (std::chrono::steady_clock::now() >= m_shutdownDeadline) {
        ForceCloseAllConnections();
        m_state = ServerState::Stopped;
    }
}

void TcpServer::ForceCloseAllConnections()
{
    const std::size_t connectionCount = m_clients.size();
    m_metrics.AddForcedShutdownConnections(connectionCount);

    Logger::Instance().Warn("graceful shutdown timed out, force closing ", connectionCount, " client(s)");
    for (auto& item : m_clients) {
        MarkClosed(item.second);
    }

    CleanupClosedConnections();
}

std::chrono::milliseconds TcpServer::ComputeWaitTimeout() const
{
    if (m_state != ServerState::Running) {
        // WakeupChannel 可以主动唤醒，因此正常运行时可以无限等待
        return std::chrono::milliseconds(-1);
    }

    const auto now = std::chrono::steady_clock::now();
    if (now >= m_shutdownDeadline) {
        return std::chrono::milliseconds(0);
    }

    auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(m_shutdownDeadline - now);
    if (remaining.count() == 0) {
        remaining = std::chrono::milliseconds(1);
    }
    return remaining;
}

void TcpServer::AcceptNewClients()
{
    // 这里采用循环的原因是：
    // 一次 poll() 通知 listen fd 可读时，可能已经有多个客户端在连接队列中，所以要一直 accept()

    std::size_t acceptedCount = 0;
    while (acceptedCount < m_options.maxAcceptsPerEvent) {
        const int clientFd = ::accept(m_listenFd.Get(), nullptr, nullptr);
        if (clientFd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (IsWouldBlockError()) {
                return;
            }

            throw std::runtime_error(ErrorMessage("accept failed"));
        }

        ScopedFd client(clientFd);
        try {
            SetNonBlocking(client.Get());
        } catch (...) {
            throw; // client 是 ScopedFd，异常时会自动 close。
        }

        const int rawFd = client.Get();
        auto [iter, inserted] = m_clients.emplace(rawFd, Connection(std::move(client)));
        if (!inserted) {
            throw std::runtime_error("client fd already exists");
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

        ++acceptedCount;
        m_metrics.OnConnectionAccepted();
        const auto snapshot = m_metrics.Snapshot();
        Logger::Instance().Info(
            "client connected, fd=", rawFd, ", active_connections=", snapshot.activeConnections
        );
    }
}

void TcpServer::HandleClientRead(Connection &conn)
{
    if (conn.closed || conn.readPaused || conn.closeAfterWrite) {
        return;
    }

    if (conn.readBuffer.size() >= m_options.maxReadBufferBytes) {
        RejectOversizedReadBuffer(conn);
        return;
    }

    std::array<char, 16U * 1024U> temp{};
    const std::size_t availableBytes = m_options.maxReadBufferBytes - conn.readBuffer.size();
    const std::size_t readSize = std::min(temp.size(), availableBytes);

    // 这里只执行一次 recv，是因为当前使用的是level-triggered poll：
    // socket 内核缓冲区仍然有数据 -> 下一个 poll 仍会报告 POLLIN -> 其他连接也获得一次处理机会
    ssize_t received = -1;
    do {
        received = ::recv(conn.fd.Get(), temp.data(), readSize, 0);
    } while (received < 0 && errno == EINTR);

    if (received == 0) {
        MarkClosed(conn);
        return;
    }
    if (received < 0) {
        if (IsWouldBlockError()) {
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
        Logger::Instance().Warn(
            "protocol error, fd=", conn.fd.Get(),
            ", error=", error.what()
        );
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

void TcpServer::HandleClientWrite(Connection &conn)
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

    const std::size_t bytesToWrite = std::min(conn.writeBuffer.Size(), m_options.maxWriteBytesPerEvent);
    
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

    if (IsWouldBlockError()) {
        return;
    }

    MarkClosed(conn);
}

bool TcpServer::ProcessPayload(Connection &conn, const std::string &payload)
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

    Logger::Instance().Debug(
        "command processed, fd=", conn.fd.Get(),
        ", command=", payload,
        ", response_bytes=", response.size()
    );

    return true;
}

bool TcpServer::QueueResponse(Connection& conn, const std::string& response)
{
    std::string frame;
    try {
        frame = FrameCodec::Encode(response);
    } catch (const std::exception&) {
        MarkClosed(conn);
        return false;
    }

    const std::size_t pendingBytes = conn.writeBuffer.Size();
    const std::size_t hardLimit = m_options.writeHardLimitBytes;

    if (pendingBytes > hardLimit || frame.size() > hardLimit - pendingBytes) {
        m_metrics.OnSlowClientDisconnected();
        Logger::Instance().Warn(
            "close slow client, fd=", conn.fd.Get(),
            ", pending_bytes=", pendingBytes, 
            ", new_frame_bytes=", frame.size()
        );

        MarkClosed(conn);
        return false;
    }

    conn.writeBuffer.Append(frame);
    m_metrics.ObservePendingWriteBytes(conn.writeBuffer.Size());

    UpdateReadBackPressure(conn);
    return true;
}

void TcpServer::MarkClosed(Connection& conn)
{
    conn.closed = true;
    conn.readPaused = false;
    conn.closeAfterWrite = false;

    // 连接已经关闭或异常时，未发送数据不再保留。
    conn.readBuffer.clear();
    conn.writeBuffer.Clear();
}

void TcpServer::CleanupClosedConnections()
{
    for (auto it = m_clients.begin(); it != m_clients.end();) {
        if (!it->second.closed) {
            ++it;
            continue;
        }

        const int fd = it->first;
        m_poller->Remove(fd);
        
        m_metrics.OnConnectionClosed();
        const auto snapshot = m_metrics.Snapshot();
        Logger::Instance().Info(
            "client disconnected, fd=",
            fd,
            ", active_connections=",
            snapshot.activeConnections
        );
        it = m_clients.erase(it);
    }
}

IoEvent TcpServer::DesiredEvents(const Connection& conn) const noexcept
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

void TcpServer::RefreshConnectionInterest(Connection& conn)
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

void TcpServer::CleanupReactor() noexcept
{
    if (m_poller != nullptr) {
        for (const auto& item : m_clients) {
            try {
                m_poller->Remove(item.first);
            } catch (const std::exception& error) {
                Logger::Instance().Error(
                    "failed to remove client, fd=", item.first,
                    " from poller: ", error.what()
                );
            }
        }

        if (m_listenFd.Valid()) {
            try {
                m_poller->Remove(m_listenFd.Get());
            } catch (const std::exception& error) {
                Logger::Instance().Error("failed to remove listen fd:", error.what());
            }
        }

        try {
            m_poller->Remove(m_stopWakup.ReadFd());
        } catch (const std::exception& error) {
            Logger::Instance().Error("failed to remove wakeup fd: ", error.what());
        }
    }

    m_clients.clear();
    m_listenFd.Reset();
    m_poller.reset();
}

void TcpServer::StartSweeperThread()
{
    if (m_sweepInterval.count() < 0) {
        return;
    }

    bool expected = false;
    if (!m_sweepRunning.compare_exchange_strong(expected, true)) {
        // 已经启动过了
        return;
    }

    m_sweepThread = std::thread(&TcpServer::SweeperLoop, this);
}

void TcpServer::StopSweeperThread()
{
    const bool wasRunning = m_sweepRunning.exchange(false);
    if (wasRunning) {
        m_sweepCv.notify_all();
    }

    if (m_sweepThread.joinable()) {
        m_sweepThread.join();
    }
}

void TcpServer::SweeperLoop()
{
    std::unique_lock<std::mutex> lock(m_sweepMutex);

    while (m_sweepRunning) {
        const bool shouldStop = m_sweepCv.wait_for(lock, m_sweepInterval, [this] {
            return !m_sweepRunning.load();
        });

        if (shouldStop) {
            break;
        }

        // 不要持有 sweeper_mutex_ 调用 KVStore。
        // KVStore 内部有自己的 mutex。
        lock.unlock();

        const size_t removed = m_store.SweepExpired();
        m_metrics.OnSweeperRun(removed);
        if (removed > 0) {
            Logger::Instance().Debug(
                "[sweeper] removed expired keys, count=", removed
            );
        }
        
        lock.lock();
    }
}

void TcpServer::UpdateReadBackPressure(Connection& conn)
{
    if (conn.closeAfterWrite || conn.closed) {
        return;
    }

    const std::size_t pendingBytes = conn.writeBuffer.Size();

    if (conn.readPaused) {
        if (pendingBytes <= m_options.writeLowWatermarkBytes) {
            conn.readPaused = false;

            m_metrics.OnReadResumed();
            Logger::Instance().Debug(
                "client read resumed, fd=", conn.fd.Get(),
                ", pending_bytes=", pendingBytes
            );
        }
        return;
    }

    if (pendingBytes >= m_options.writeHighWatermarkBytes) {
        conn.readPaused = true;

        m_metrics.OnReadPaused();
        Logger::Instance().Debug(
            "client read paused, fd=", conn.fd.Get(),
            ", pending_bytes=", pendingBytes
        );
    }
}

void TcpServer::RejectOversizedReadBuffer(Connection& conn)
{
    if (QueueResponse(conn, "-ERR request buffer too large")) {
        conn.closeAfterWrite = true;
    } else {
        MarkClosed(conn);
    }
}

// 延迟桶估算百分位
std::uint64_t TcpServer::EstimatePercentileUpperBoundUs(
    const ServerMetricsSnapshot &snapshot, double percentile) noexcept
{
    if (snapshot.commandLatencyCount == 0U) {
        return 0U;
    }

    const double requested = static_cast<double>(snapshot.commandLatencyCount) * percentile;
    const auto targetRank = static_cast<std::uint64_t>(requested < 1.0 ? 1.0 : requested);

    constexpr std::array<std::uint64_t, 7> UPPER_BOUNDS_US {
        10U,
        50U,
        100U,
        500U,
        1000U,
        5000U,
        5001U
    };

    std::uint64_t accumulated = 0;
    for (std::size_t index = 0; index < snapshot.commandLatencyBuckets.size(); ++index) {
        accumulated += snapshot.commandLatencyBuckets[index];
        if (accumulated >= targetRank) {
            return UPPER_BOUNDS_US[index];
        }
    }

    return UPPER_BOUNDS_US.back();
}

std::string TcpServer::BuildStatsResponse() {
    const auto storeStats = m_store.Stats();
    const auto metrics = m_metrics.Snapshot();
    const double averageLatencyUs = metrics.AverageCommandLatencyMicroseconds();
    const double maximumLatencyUs = static_cast<double>(metrics.commandLatencyMaximumNanoseconds) / 1000.0;
    const std::uint64_t p95UpperUs = EstimatePercentileUpperBoundUs(metrics, 0.95);
    const std::uint64_t p99UpperUs = EstimatePercentileUpperBoundUs(metrics, 0.99);

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

} // namespace tinykv
