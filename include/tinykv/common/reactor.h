/**
 * Reactor 相关状态
 */
#pragma once

#include <cstddef>
#include <chrono>

namespace tinykv {

enum class PollerBackend {
    Auto,
    Poll,
    Epoll
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

struct ServerOptions {
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

    // 创建的子 reactor 的线程数
    std::size_t subReactorCount { 4U };
};
 
} // namespace tinykv