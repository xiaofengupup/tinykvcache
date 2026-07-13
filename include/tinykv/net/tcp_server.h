/**
 * TCP 服务器端
 */
#pragma once

#include "tinykv/core/kv_store.h"
#include "tinykv/net/scoped_fd.h"

#include <string>
#include <atomic>

namespace tinykv {

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
    /**
     * 处理一个客户端连接
     * 
     * 当前阶段一个客户端会阻塞占用服务端，后续 poll reactor 会把这里拆成事件驱动
     */
    void HandleClient(ScopedFd client);

    /**
     * 处理一条完整的 payload
     * 
     * 返回 true：继续处理当前客户端
     * 返回 false：关闭当前客户端连接
     */
    bool ProcessPayload(int clientFd, const std::string &payload);

    /**
     * 发送响应
     */
    void SendResponse(int clientFd, const std::string &response);

    /**
     * 确保发送完整 data
     */
    void SendAll(int clientFd, const std::string &data);

private:
    std::string m_host;
    int m_port {0};
    std::atomic_bool m_running {false};
    ScopedFd m_listenFd;
    KVStore  m_store;
};

} // namespace tinyky
