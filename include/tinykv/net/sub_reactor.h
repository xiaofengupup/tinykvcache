/**
 * 从 Reactor
 */
#pragma once

#include "tinykv/net/poll/poller.h"
#include "tinykv/net/wakeup/wakeup_channel.h"
#include "tinykv/net/scoped_fd.h"
#include "tinykv/net/output_buffer.h"
#include "tinykv/observability/server_metrics.h"
#include "tinykv/common/reactor.h"
#include "tinykv/core/kv_store.h"

#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <chrono>
#include <mutex>
#include <deque>
#include <atomic>

namespace tinykv {

class SubReactor {

public:
    // SubReactor 构造
    explicit SubReactor(std::uint16_t id, const ServerOptions& m_options, ServerMetrics& m_metrics, KVStore& m_store);

    // 启动 SubReactor
    void Start();

    /**
     * 向 SubReactor 添加一个客户端，由 MainReactor 调用
     * 核心思路：该方法不能直接操作 m_clients 和 poller，避免跨线程竞态
     * 实现逻辑：lock -> pendingClients -> notify
     */
    bool EnqueClient(ScopedFd clientFd);

    /**
     * Stop 仍然由 MainReactor 触发
     * 
     * 为避免多线程竞态，采用 mutex + m_requestedShutdownDeadline + notify 机制
     */
    void Stop(std::chrono::steady_clock::time_point deadline);
    
    // 强制停止
    void ForceStop();

    // 等待 SubReactor 执行完毕
    void Join();

    // 是否已经结束
    bool IsStopped() const noexcept;
    bool HasFailed() const noexcept;

    // Id
    std::uint16_t Id() const noexcept;
    
private:
    struct Connection {
        explicit Connection(ScopedFd clientFd) : fd(std::move(clientFd)) {}

        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;
        Connection(Connection&&) noexcept = default;
        Connection& operator=(Connection&&) noexcept = default;

        ScopedFd fd;
        std::string readBuffer;         // 连接级读缓冲区，用于处理 TCP 半包和粘包
        OutputBuffer writeBuffer;       // 连接级写缓冲区，使用偏移式 OutputBuffer，避免频繁 erase。
        bool readPaused{false};         // 因为待发送数据过多而暂停读取。
        bool closeAfterWrite {false};   // 表示当前响应完后关闭客户端连接，对应客户端 quit 命令
        bool closed {false};            // 表示连接已失效，需要从 m_clients 中移除
        IoEvent registeredEvents {IoEvent::None}; // 当前已经注册到 poller 的关注事件，用于避免不必要的 Modify 调用
    };

private:
    // SubReactor 事件循环
    void EventLoop();
    std::chrono::milliseconds ComputeWaitTimeout() const noexcept;
    void AddClientInLoop(ScopedFd client);
    void ProcessControlRequests();

    // 关闭相关
    void BeginGracefulShutdown(std::chrono::steady_clock::time_point deadline);
    void MarkClosed(Connection& conn);
    void CleanupClosedConnections();
    void CheckShutdownProgress();
    void ForceCloseAllConnections();

    // client fd 可读
    void HandleClientRead(Connection &conn);
    void RejectOversizedReadBuffer(Connection& conn);
    bool ProcessPayload(Connection &conn, const std::string &payload);
    std::string BuildStatsResponse();
    std::string EstimatePercentileBucketUs(
        const ServerMetricsSnapshot& snapshot, double percentile) noexcept;

    // client fd 可写
    void HandleClientWrite(Connection &conn);
    void UpdateReadBackPressure(Connection &conn);
    bool QueueResponse(Connection &conn, const std::string &response);
    
    // 刷新 client fd 关注事件
    IoEvent DesiredEvents(const Connection& conn) const noexcept;
    void RefreshConnectionInterest(Connection& conn);

    void PublishState(ServerState state) noexcept;

private:
    std::uint16_t m_id;
    ServerState m_state {ServerState::Created};
    std::atomic<ServerState> m_publishedState {ServerState::Created};
    std::thread m_thread;
    std::unique_ptr<Poller> m_poller;
    std::atomic<bool> m_failed {false};

    std::mutex m_controlMutex;
    WakeupChannel m_controlWakeup;
    std::deque<ScopedFd> m_pendingClients;
    std::unordered_map<int, Connection> m_clients;
    
    bool m_stopRequested {false};
    bool m_forceStopRequested {false};
    std::chrono::steady_clock::time_point m_requestedShutdownDeadline {};
    std::chrono::steady_clock::time_point m_shutdownDeadline {};

    ServerOptions m_options;
    KVStore& m_store;
    ServerMetrics& m_metrics;
};

} // namespace tinykv

