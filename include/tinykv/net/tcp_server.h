/**
 * TCP 服务器端
 */
#pragma once

#include "tinykv/core/kv_store.h"
#include "tinykv/net/scoped_fd.h"
#include "tinykv/net/output_buffer.h"
#include "tinykv/net/poll/io_event.h"
#include "tinykv/net/poll/poller.h"
#include "tinykv/net/poll/poller_factory.h"
#include "tinykv/net/wakeup/wakeup_channel.h"
#include "tinykv/observability/server_metrics.h"

#include <string>
#include <atomic>
#include <unordered_map>
#include <cstddef>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <algorithm>
#include <array>
#include <memory>

namespace tinykv {

struct TcpServerOptions {
    // 单连接允许保留的最大未完成请求数据。
    // 默认略大于 FrameCodec 的 1 MiB payload 上限。
    std::size_t maxReadBufferBytes { 1024U * 1024U + 4U };

    // 待发送数据达到高水位时暂停读取该客户端
    std::size_t writeHighWatermarkBytes { 1024U * 1024U };

    // 待发送数据下降到低水位时恢复读取。
    std::size_t writeLowWatermarkBytes { 512U * 1024U };

    // 单连接写缓冲区硬上限
    // 超过该值说明客户端消费响应过慢，服务端会关闭该连接，防止内存无限增长。
    std::size_t writeHardLimitBytes { 4U * 1024U * 1024U };

    // 单次 POLLOUT 事件最多发送的字节数。
    std::size_t maxWriteBytesPerEvent { 64U * 1024U };

    // 一次 listen fd 可读事件最多接受的连接数。
    std::size_t maxAcceptsPerEvent { 64 };

    // Auto：Linux 使用 epoll，macOS 使用 poll。
    PollerBackend pollerBackend { PollerBackend::Auto };

    // 收到退出请求后，允许现有的写缓冲区排空的最长时间
    std::chrono::milliseconds gracefulShutdownTimeout { std::chrono::milliseconds(3000) };
};

/**
 * TcpServer 运行状态
 * 
 * Created：构造完成，尚未调用 Run()
 * Running：运行中
 * Draining：实现优雅退出的关键状态，进入该状态后：
 *  - 不再接受新客户端连接
 *  - 不再读取新的客户端请求
 *  - 但继续发送已经生成响应、仍在客户端连接写缓冲区的响应数据
 * Stopped：服务端已经停止
 * 
 * 状态转换如下：
 * Created
 *    |
 *    | Run()
 *    v
 * Running
 *    |
 *    | Stop / SIGINT / SIGTERM
 *    v
 * Draining
 *    |
 *    | 所有连接排空 writeBuffer 或 shutdown timeout
 *    v
 * Stopped
 */
enum class ServerState {
    Created,
    Running,
    Draining,
    Stopped
};

/**
 * TcpServer 是一个单线程的 Reactor 模型 TCP 服务器（辅助后台清理线程）
 */
class TcpServer {
public:
    TcpServer(std::string host, int port,
        std::chrono::seconds sweepInterval = std::chrono::seconds(5),
        TcpServerOptions options = TcpServerOptions{});
    ~TcpServer();

    // 禁止移动
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer &) = delete;

    /**
     * 启动服务端
     * 
     * 当前实现仍是阻塞调用，Run() 会一直 accept 客户端并处理请求
     */
    void Run();

    /**
     * 停止服务端，可以从其他普通线程调用
     */
    void Stop();

    /**
     * 返回供信号处理器写入的 fd
     */
    int StopNotificationFd() const noexcept;

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
    void AcceptNewClients();
    void HandleClientRead(Connection &conn);
    void HandleClientWrite(Connection &conn);
    bool QueueResponse(Connection &conn, const std::string &response);
    void UpdateReadBackPressure(Connection &conn);
    void RejectOversizedReadBuffer(Connection& conn);
    IoEvent DesiredEvents(const Connection& conn) const noexcept;
    void RefreshConnectionInterest(Connection& conn);

    void BeginGracefulShutdown();
    void CheckShutdownProgress();
    void ForceCloseAllConnections();
    std::chrono::milliseconds ComputeWaitTimeout() const;
    void CleanupReactor() noexcept;

    std::string BuildStatsResponse();
    static std::uint64_t
        EstimatePercentileUpperBoundUs(const ServerMetricsSnapshot &snapshot, double percentile) noexcept;

    /**
     * 处理一条完整的 payload
     * 
     * 返回 true：继续处理当前客户端
     * 返回 false：关闭当前客户端连接
     */
    bool ProcessPayload(Connection &conn, const std::string &payload);
    
    void MarkClosed(Connection& conn);
    
    void CleanupClosedConnections();

    /**
     * 启动 TTL 后台清理线程
     */
    void StartSweeperThread();

    /**
     * 停止 TTL 后台清理线程
     */
    void StopSweeperThread();

    /**
     * 后台线程主循环
     */
    void SweeperLoop();

private:
    std::string m_host;
    int m_port {0};
    ScopedFd m_listenFd;
    KVStore  m_store;
    std::unordered_map<int, Connection> m_clients; // key 是 client fd，value 是该连接的状态。

     // TTL 后台清理线程相关
    std::chrono::seconds m_sweepInterval {5};
    bool m_sweeperRunning {false};        // 表示 sweeper 线程是否正在运行
    std::mutex m_sweeperMutex;            // 保护 m_sweepRunning，并参与 m_sweepCv 等待协议
    std::condition_variable m_sweepCv;  // 负责及时唤醒 sweeper 线程
    std::thread m_sweepThread;          // sweeper 后台线程对象

    // TCP Server 配置
    TcpServerOptions m_options;

    // Poller
    std::unique_ptr<Poller> m_poller;

    // wakeup handler
    std::atomic_bool m_stopRequested {false};
    ServerState m_state {ServerState::Created};
    WakeupChannel m_stopWakup;
    std::chrono::steady_clock::time_point m_shutdownDeadline {};

    // server metrics
    ServerMetrics m_metrics;
};

} // namespace tinyky
