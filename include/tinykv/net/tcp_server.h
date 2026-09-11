/**
 * TCP 服务器端
 */
#pragma once

#include "tinykv/core/kv_store.h"
#include "tinykv/net/scoped_fd.h"
#include "tinykv/net/output_buffer.h"
#include "tinykv/net/poll/io_event.h"
#include "tinykv/net/poll/poller.h"
#include "tinykv/net/wakeup/wakeup_channel.h"
#include "tinykv/observability/server_metrics.h"
#include "tinykv/common/server_info.h"
#include "tinykv/net/sub_reactor.h"

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

/**
 * TcpServer 是一个单线程的 Reactor 模型 TCP 服务器（辅助后台清理线程）
 */
class TcpServer {
public:
    TcpServer(std::string host, int port,
        std::chrono::milliseconds sweepInterval = std::chrono::milliseconds(5000),
        NetworkOptions options = NetworkOptions{}, ReactorOptions reactorOptions = ReactorOptions{},
        ShutdownOptions shutdownOptions = ShutdownOptions{});
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
    // Main Reactor 事件循环相关
    std::chrono::milliseconds ComputeWaitTimeout() const;
    void CheckSubReactorHealth();
    void AcceptNewClients();

    // 优雅停止相关
    void BeginGracefulShutdown();
    void CheckShutdownProgress();
    void CleanupMainReactor() noexcept;
    void JoinSubReactors() noexcept;
    void ForceStopAndJoinSubReactors() noexcept;

    // TTL 后台清理线程相关
    void StartSweeperThread();
    void StopSweeperThread();
    void SweeperLoop();

private:
    // 基本信息
    std::string m_host;
    int m_port {0};
    ScopedFd m_listenFd;
    
    // Poller
    std::unique_ptr<Poller> m_poller;

    // wakeup handler
    std::atomic_bool m_stopRequested {false};
    ServerState m_state {ServerState::Created};
    WakeupChannel m_stopWakeup;
    std::chrono::steady_clock::time_point m_shutdownDeadline {};
    bool m_forceStopSent {false};

    // 从 Reactor 集合
    std::vector<std::unique_ptr<SubReactor>> m_subReactors;
    std::size_t m_nextSubReactorIndex {0};

     // TTL 后台清理线程相关
    std::chrono::milliseconds m_sweepInterval {5000};
    bool m_sweeperRunning {false};        // 表示 sweeper 线程是否正在运行
    std::mutex m_sweeperMutex;            // 保护 m_sweepRunning，并参与 m_sweepCv 等待协议
    std::condition_variable m_sweepCv;    // 负责及时唤醒 sweeper 线程
    std::thread m_sweepThread;            // sweeper 后台线程对象

    // TCP Server 配置
    NetworkOptions m_networkOptions;
    ReactorOptions m_reactorOptions;
    ShutdownOptions m_shutdownOptions;

    // TCP server metrics
    ServerMetrics m_metrics;

    // KV 存储
    KVStore m_store;
};

} // namespace tinyky
