#include "tinykv/net/tcp_server.h"

#include "tinykv/core/command_parser.h"
#include "tinykv/core/command_executor.h"
#include "tinykv/core/frame_codec.h"
#include "tinykv/net/socket_util.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <utility>

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

TcpServer::TcpServer(std::string host, int port) : m_host(std::move(host)), m_port(port) {}

void TcpServer::Run()
{
    m_listenFd = CreateListenSocket(m_host, m_port);
    SetNonBlocking(m_listenFd.Get()); // 核心修改：将 listen fd 设置为非阻塞，这样 accept 不会卡死
    m_running = true;
    std::cout << "server listenint on " << m_host << ":" << m_port << "\n";

    while (m_running) {
        std::vector<pollfd> pollFds;
        pollFds.reserve(1 + m_clients.size());

        // 将 listen fd 加入 poll 关注列表
        pollfd listenPoll;
        listenPoll.fd = m_listenFd.Get();
        listenPoll.events = POLLIN;
        listenPoll.revents = 0;
        pollFds.push_back(listenPoll);

        // 将 client fd 加入 poll 关注列表
        for (auto& item : m_clients) {
            Connection &conn = item.second;
            if (conn.closed) {
                continue;
            }

            pollfd clientPoll;
            clientPoll.fd = conn.fd.Get();
            clientPoll.events = POLLIN;
            clientPoll.revents = 0;
            if (!conn.writeBuffer.empty()) {
                // 只有当 write_buffer 非空时，才关注 POLLOUT。
                // 否则大多数 socket 都会一直可写，导致 poll 频繁返回，浪费 CPU。
                clientPoll.events |= POLLOUT;
            }

            pollFds.push_back(clientPoll);
        }


        const int ret = ::poll(pollFds.data(), pollFds.size(), 1000);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error(ErrorMessage("poll failed"));
        }

        if (ret == 0) {
            CleanupClosedConnections();
            continue;
        }

        // 第 0 个永远是 listen fd
        if ((pollFds[0].revents & POLLIN) != 0) {
            AcceptNewClients();
        }

        // 后续都是 client fd
        for (std::size_t i = 1; i < pollFds.size(); ++i) {
            const int fd = pollFds[i].fd;
            const short revents = pollFds[i].revents;

            if (revents == 0) {
                continue;
            }
            
            auto it = m_clients.find(fd);
            if (it == m_clients.end()) {
                continue;
            }

            Connection &conn = it->second;
            if ((revents & (POLLERR | POLLNVAL)) != 0) {
                MarkClosed(conn);
                continue;
            }


            // 处理读事件
            if ((revents & POLLIN) != 0) {
                HandleClientRead(conn);
            }
            if (conn.closed) {
                continue;
            }

            // 处理写事件
            if ((revents & POLLOUT) != 0) {
                HandleClientWrite(conn);
            }
            if (conn.closed) {
                continue;
            }

            // 处理中断
            if ((revents & POLLHUP) != 0) {
                MarkClosed(conn);
            }
        }

        CleanupClosedConnections();
    }

    m_clients.clear();
    m_listenFd.Reset();
}

void TcpServer::Stop()
{
    m_running = false;
    m_listenFd.Reset();

    for (auto &item : m_clients) {
        MarkClosed(item.second);
    }
}

void TcpServer::AcceptNewClients()
{
    // 这里采用循环的原因是：
    // 一次 poll() 通知 listen fd 可读时，可能已经有多个客户端在连接队列中，所以要一直 accept()
    while (true) {
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
        auto result = m_clients.emplace(rawFd, Connection(std::move(client)));
        if (!result.second) {
            throw std::runtime_error("client fd already exists");
        }
        std::cout << "client connected, fd=" << rawFd << "\n";
    }
}

void TcpServer::HandleClientRead(Connection &conn)
{
    char temp[4096];
    while (m_running) {
        const ssize_t n = ::recv(conn.fd.Get(), &temp, sizeof(temp), 0);
        if (n > 0) {
            std::vector<std::string> payloads;
            try {
                payloads = FrameCodec::Decode(conn.readBuffer, temp, static_cast<std::size_t>(n));
            } catch (const std::exception&) {
                AppendResponse(conn, "-ERR protocol error");
                conn.closeAfterWrite = true;
                return;
            }

            for (const auto &payload : payloads) {
                const bool keepAlive = ProcessPayload(conn, payload);
                if (!keepAlive) {
                    conn.closeAfterWrite = true;
                    return;
                }
            }

            continue;
        }

        if (n == 0) {
            // 对端关闭连接。
            MarkClosed(conn);
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        if (IsWouldBlockError()) {
            return;
        }

        MarkClosed(conn);
        return;
    }
}

void TcpServer::HandleClientWrite(Connection &conn)
{
    while (!conn.writeBuffer.empty()) {
        const ssize_t n = ::send(conn.fd.Get(), conn.writeBuffer.data(), conn.writeBuffer.size(), 0);
        if (n > 0) {
            conn.writeBuffer.erase(0, static_cast<std::size_t>(n));
            continue;
        }

        if (n == 0) {
            return;
        }
        
        if (errno == EINTR) {
            continue;
        }
        if (IsWouldBlockError()) {
            return;
        }

        MarkClosed(conn);
        return;
    }

    if (conn.writeBuffer.empty() && conn.closeAfterWrite) {
        MarkClosed(conn);
    }
}

bool TcpServer::ProcessPayload(Connection &conn, const std::string &payload)
{
    const Command cmd = ParseCommand(payload);
    const std::string response = CommandExecutor::Execute(m_store, cmd);
    AppendResponse(conn, response);

    return cmd.type != CommandType::Quit; // QUIT 只关闭当前客户端连接，不关闭整个服务端。
}

void TcpServer::AppendResponse(Connection& conn, const std::string& response)
{
    const std::string frame = FrameCodec::Encode(response);
    conn.writeBuffer += frame;
}

void TcpServer::MarkClosed(Connection& conn)
{
    conn.closed = true;
    conn.closeAfterWrite = false;

    // 连接已经关闭或异常时，未发送数据不再保留。
    conn.readBuffer.clear();
    conn.writeBuffer.clear();
}

void TcpServer::CleanupClosedConnections()
{
    for (auto it = m_clients.begin(); it != m_clients.end();) {
        if (it->second.closed) {
            std::cout << "client disconnected, fd=" << it->first << "\n";
            it = m_clients.erase(it);
        } else {
            ++it;
        }
    } 
}

} // namespace tinykv
