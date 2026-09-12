#include "tinykv/net/tcp_server.h"
#include "tinykv/core/command_parser.h"
#include "tinykv/core/command_executor.h"
#include "tinykv/core/frame_codec.h"
#include "tinykv/net/socket_util.h"
#include "tinykv/net/poll/poller_factory.h"
#include "tinykv/observability/logger.h"
#include "tinykv/common/posix_error.h"

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

TcpServer::TcpServer(std::string host, int port, std::chrono::milliseconds sweepInterval,
    NetworkOptions networkOptions, ReactorOptions reactorOptions, ShutdownOptions shutdownOptions)
    : m_host(std::move(host)), m_port(port), m_sweepInterval(sweepInterval),
      m_networkOptions(networkOptions), m_reactorOptions(reactorOptions), m_shutdownOptions(shutdownOptions)
{
    if (m_networkOptions.maxReadBufferBytes < 4U) {
        throw std::invalid_argument("maxReadBufferBytes must be at least 4");
    }

    if (m_networkOptions.writeLowWatermarkBytes > m_networkOptions.writeHighWatermarkBytes) {
        throw std::invalid_argument("write low watermark exceeds high watermark");
    }

    if (m_networkOptions.writeHighWatermarkBytes > m_networkOptions.writeHardLimitBytes) {
        throw std::invalid_argument("write high watermark exceeds hard limit");
    }

    if (m_networkOptions.maxWriteBytesPerEvent == 0U) {
        throw std::invalid_argument("maxWriteBytesPerEvent must be greater than zero");
    }

    if (m_networkOptions.maxAcceptsPerEvent == 0U) {
        throw std::invalid_argument("maxAcceptsPerEvent must be greater than zero");
    }

    if (m_reactorOptions.subReactorCount < 1U || m_reactorOptions.subReactorCount > 16U) {
        throw std::invalid_argument("sub_reactors must be between 1 and 16");
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

    try {
        m_poller = PollerFactory::CreatePoller(m_reactorOptions.pollerBackend);
        m_listenFd = CreateListenSocket(m_host, m_port);
        SetNonBlocking(m_listenFd.Get());
        SetCloseOnExec(m_listenFd.Get());
        m_poller->Add(m_listenFd.Get(), IoEvent::Read);
        m_poller->Add(m_stopWakeup.ReadFd(), IoEvent::Read);

        // 创建并启动 Sub Reactor
        m_subReactors.reserve(m_reactorOptions.subReactorCount);
        for (std::size_t i = 0; i < m_reactorOptions.subReactorCount; ++i) {
            m_subReactors.push_back(
                std::make_unique<SubReactor>(static_cast<std::uint16_t>(i), m_metrics, m_store,m_networkOptions, m_reactorOptions)
            );
        }
        for (auto& reactor : m_subReactors) {
            reactor->Start();
        }

        m_state = ServerState::Running;
        StartSweeperThread();
        TINYKV_LOG_INFO_MSG("TinyKVCache server started...");

        // 主 Reactor 事件循环，专职负责监听 listen fd，处理新连接的建立（Accept）
        // 同时监听处理 stop wakeup 事件
        while (m_state != ServerState::Stopped) {
            const std::vector<ReadyEvent> readyEvents = m_poller->Wait(ComputeWaitTimeout());
            bool stopEventReceived = m_stopRequested.load(std::memory_order_acquire);
            
            // 先处理停止通知，避免同一批事件中继续 accept 新连接
            for (const ReadyEvent& rEvent : readyEvents) {
                if (rEvent.fd == m_stopWakeup.ReadFd()) {
                    m_stopWakeup.Drain();
                    stopEventReceived = true;
                    break; // 同一批 readyEvents 里面，同一个 fd 通常只会对应一个就绪事件记录，所以找到后无需继续遍历
                }
            }
            if (stopEventReceived) {
                BeginGracefulShutdown();
            }

            // Main Reactor 需要监控 Sub Reactor 是否异常退出
            // 如果 Sub Reactor 异常退出，那么停止整个服务
            CheckSubReactorHealth();

            for (const ReadyEvent& rEvent : readyEvents) {
                if (rEvent.fd == m_stopWakeup.ReadFd()) {
                    continue;
                }
                if (m_listenFd.Valid() && rEvent.fd == m_listenFd.Get()) {
                    if (m_state == ServerState::Running && HasIoEvent(rEvent.events, IoEvent::Read)) {
                        AcceptNewClients();
                    }
                }
            }
            CheckShutdownProgress();
        }
    } catch (const std::exception&) {
        StopSweeperThread();
        ForceStopAndJoinSubReactors();
        CleanupMainReactor();
        m_state = ServerState::Stopped;
        throw;
    }

    StopSweeperThread();
    JoinSubReactors();  // 等待所有 Sub Reactor 退出
    CleanupMainReactor();
    m_state = ServerState::Stopped;

    TINYKV_LOG_INFO_MSG("server stopped gracefully");
}

/**
 * 只做通知动作，真正的状态修改全部由事件循环线程完成
 */
void TcpServer::Stop()
{
    m_stopRequested.store(true, std::memory_order_release);
    m_stopWakeup.Notify();
}

int TcpServer::StopNotificationFd() const noexcept
{
    return m_stopWakeup.WriteFd();
}

void TcpServer::BeginGracefulShutdown()
{
    if (m_state != ServerState::Running) {
        return;
    }

    m_state = ServerState::Draining;
    m_shutdownDeadline = std::chrono::steady_clock::now() + m_shutdownOptions.gracefulTimeout;

    TINYKV_LOG_INFO("graceful shutdown started, sub reactor size={}", m_subReactors.size());
    
    // 停止接受新连接
    if (m_listenFd.Valid()) {
        m_poller->Remove(m_listenFd.Get());
        m_listenFd.Reset();
    }

    // 通知 SubReactor
    for (auto &subReactor : m_subReactors) {
        subReactor->Stop(m_shutdownDeadline);
    }
}

void TcpServer::CheckShutdownProgress()
{
    if (m_state != ServerState::Draining) {
        return;
    }

    bool allSubReactorStopped = true;
    for (const auto& subReactor : m_subReactors) {
        if (!subReactor->IsStopped()) {
            allSubReactorStopped = false;
        }
    }
    if (allSubReactorStopped) {
        m_state = ServerState::Stopped;
        return;
    }

    if (!m_forceStopSent && std::chrono::steady_clock::now() >= m_shutdownDeadline) {
        TINYKV_LOG_WARN_MSG("graceful shutdown timed out, force closing server");
        for (auto& reactor : m_subReactors) {
            if (!reactor->IsStopped()) {
                reactor->ForceStop();
            }
        }
        m_forceStopSent = true;
    }
}

std::chrono::milliseconds TcpServer::ComputeWaitTimeout() const
{
    constexpr auto normalTimeout = std::chrono::milliseconds(500);
    constexpr auto drainingPollInterval = std::chrono::milliseconds(100);

    if (m_state == ServerState::Running) {
        return normalTimeout;
    }

    if (m_state != ServerState::Draining) {
        return std::chrono::milliseconds(0);
    }

    if (m_forceStopSent) {
        return std::chrono::milliseconds(10);
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

    return std::min(drainingPollInterval, remaining);
}

void TcpServer::JoinSubReactors() noexcept
{
    for (auto& reactor : m_subReactors) {
        if (reactor == nullptr) {
            continue;
        }

        try {
            reactor->Join();
        } catch (const std::exception& error) {
            try {
                TINYKV_LOG_ERROR("failed to join sub reactor {}: {}", reactor->Id(), error.what());
            } catch (...) {
            }
        }
    }
}

void TcpServer::ForceStopAndJoinSubReactors() noexcept
{
    for (auto& reactor : m_subReactors) {
        if (!reactor) {
            continue;
        }

        try {
            reactor->ForceStop();
        } catch (...) {
        }
    }

    JoinSubReactors();
}

void TcpServer::CheckSubReactorHealth()
{
    if (m_state != ServerState::Running) {
        return;
    }

    for (const auto& reactor : m_subReactors) {
        if (!reactor->HasFailed()) {
            continue;
        }

        TINYKV_LOG_ERROR("sub reactor {} failed, shuttdown server", reactor->Id());
        BeginGracefulShutdown();
        return;
    }
}

void TcpServer::AcceptNewClients()
{
    // 这里采用循环的原因是：
    // 一次 poll() 通知 listen fd 可读时，可能已经有多个客户端在连接队列中，所以要一直 accept()
    std::size_t acceptedCount = 0;
    while (acceptedCount < m_networkOptions.maxAcceptsPerEvent) {
        const int rawFd = ::accept(m_listenFd.Get(), nullptr, nullptr);
        if (rawFd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }

            throw std::runtime_error(ErrorMessage("accept failed"));
        }

        ++acceptedCount;

        ScopedFd client(rawFd);
        try {
            SetNonBlocking(client.Get());
            SetCloseOnExec(client.Get());
        } catch (const std::exception& error) {
            TINYKV_LOG_WARN("failed to initialize client fd={}, error={}", rawFd, error.what());
            continue;
        }

        // 此处暂时使用轮询策略，将 client 分配给 subReactor 进行处理
        const std::size_t reactorIndex = m_nextSubReactorIndex;
        m_nextSubReactorIndex = (m_nextSubReactorIndex + 1) % m_subReactors.size();
        SubReactor& reactor = *m_subReactors[reactorIndex];
        if (!reactor.EnqueClient(std::move(client))) {
            TINYKV_LOG_WARN("sub reactor rejected client, reactor_id={}, fd={}", reactor.Id(), rawFd);
        }
    }
}

void TcpServer::CleanupMainReactor() noexcept
{
    if (m_poller != nullptr) {
        if (m_listenFd.Valid()) {
            try {
                m_poller->Remove(m_listenFd.Get());
            } catch (...) {
            }
        }

        try {
            m_poller->Remove(m_stopWakeup.ReadFd());
        } catch (...) {
        }
    }

    m_listenFd.Reset();
    m_poller.reset();
    m_subReactors.clear();
}

void TcpServer::StartSweeperThread()
{
    if (m_sweepInterval.count() <= 0) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_sweeperMutex);
        // 除了由 TcpServer::Run() 保证只启动一次，内部也要保证不会重复启动 sweeper 线程
        if (m_sweeperRunning) {
            throw std::logic_error("sweeper thread already running");
        }
        m_sweeperRunning = true;
    }

    try {
        m_sweepThread = std::thread(&TcpServer::SweeperLoop, this);
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(m_sweeperMutex);
            m_sweeperRunning = false;
        }
        throw;
    }
}

void TcpServer::StopSweeperThread()
{
    {
        std::lock_guard<std::mutex> lock(m_sweeperMutex);
        m_sweeperRunning = false;   
    }

    m_sweepCv.notify_all();
    if (m_sweepThread.joinable()) {
        m_sweepThread.join();
    }
}

void TcpServer::SweeperLoop()
{
    std::unique_lock<std::mutex> lock(m_sweeperMutex);

    while (true) {
        // 这里 wait_for 的作用：最多等待 m_sweepInterval，等待期间如果被通知并且谓词返回 true，就提前结束
        //     lock: 要由条件变量暂时释放和重新获取的锁
        //     m_sweepInterval：最长等待时间
        //     [this] { return !m_sweeperRunning; }: 谓词，这里指停止条件
        /**
         * 执行过程大致如下：
         * 1. 检查谓词；
         * 2. 如果谓词为 false，释放 m_sweeperMutex
         * 3. 当前线程进入等待状态
         * 4. 收到 notify 或者等待超时
         * 5. 重新获取 m_sweeperMutex
         * 6. 再次检查谓词
         * 7. 返回
         * 
         * 最重要的是：条件变量等待时不会一直占用 mutex，否则其他线程就无法修改共享状态或执行停止逻辑
         */
        const bool shouldStop = m_sweepCv.wait_for(lock, m_sweepInterval, [this] {
            return !m_sweeperRunning;
        });

        if (shouldStop) {
            break;
        }

        // 不要持有 m_sweeperMutex 调用 KVStore，KVStore 内部有自己的 mutex
        lock.unlock();

        KVStore::SweepResult result;
        do {
            result = m_store.SweepExpired();
            m_metrics.OnSweeperRun(result.removed);
            if (result.removed > 0) {
                TINYKV_LOG_DEBUG("[sweeper] removed expired keys, processed={}, removed={}, more={}",
                    result.processed, result.removed, result.hasMoreExpired);
            }
        } while (result.hasMoreExpired && !m_stopRequested.load());

        lock.lock();
    }
}

} // namespace tinykv
