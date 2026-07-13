/**
 * TCP 服务器端
 */
#pragma once

#include "tinykv/core/kv_store.h"
#include "tinykv/net/scoped_fd.h"

#include <string>
#include <atomic>
#include <unordered_map>

namespace tinykv {

/**
 * 在第 9 阶段升级为 poll reactor 模型
 */
class TcpServer {
public:
    TcpServer(std::string host, int port);

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
     * 停止服务端
     * 
     * 当前阶段主要作为接口预留，后续配合信号处理或多线程使用。
     */
    void Stop();

private:
    struct Connection {
        explicit Connection(ScopedFd clientFd) : fd(std::move(clientFd)) {}

        ScopedFd fd;
        std::string readBuffer;         // 连接级读缓冲区，用于处理 TCP 半包和粘包
        std::string writeBuffer;        // 连接级写缓冲区，用于处理非阻塞 send 未写完成的情况
        bool closeAfterWrite {false};   // 表示当前响应完后关闭客户端连接，对应客户端 quit 命令
        bool closed {false};            // 表示连接已失效，需要从 m_clients 中移除
    };
private:
    void AcceptNewClients();

    void HandleClientRead(Connection &conn);
    
    void HandleClientWrite(Connection &conn);
    
    /**
     * 处理一条完整的 payload
     * 
     * 返回 true：继续处理当前客户端
     * 返回 false：关闭当前客户端连接
     */
    bool ProcessPayload(Connection &conn, const std::string &payload);
    
    void AppendResponse(Connection& conn, const std::string& response);
    
    void MarkClosed(Connection& conn);
    
    void CleanupClosedConnections();

private:
    std::string m_host;
    int m_port {0};
    std::atomic_bool m_running {false};
    ScopedFd m_listenFd;
    KVStore  m_store;
    std::unordered_map<int, Connection> m_clients; // key 是 client fd，value 是该连接的状态。
};

} // namespace tinyky
