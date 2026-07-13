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

namespace tinykv {

namespace {

std::string ErrorMessage(const char* prefix)
{
    return std::string(prefix) + ": " + std::strerror(errno);
}

}

TcpServer::TcpServer(std::string host, int port) : m_host(std::move(host)), m_port(port) {}

void TcpServer::Run()
{
    m_listenFd = CreateListenSocket(m_host, m_port);
    m_running = true;
    std::cout << "server listenint on " << m_host << ":" << m_port << "\n";

    while (m_running) {
        sockaddr_in clientAddr;
        socklen_t len = sizeof(clientAddr);

        const int clientFd = ::accept(m_listenFd.Get(), reinterpret_cast<sockaddr*>(&clientAddr), &len);
        if (clientFd < 0) {
            if (errno == EINTR) {
                continue;
            }

            throw std::runtime_error(ErrorMessage("accept failed"));
        }

        ScopedFd client(clientFd);
        std::cout << "client connected\n";
        HandleClient(std::move(client));
        std::cout << "client disconnected\n";
    }
}

void TcpServer::Stop()
{
    m_running = false;

    // 关闭监听 fd。
    // 当前阶段 stop 主要作为预留接口。
    m_listenFd.Reset();
}

void TcpServer::HandleClient(ScopedFd client)
{
    std::string readBuffer;
    char temp[4096];

    while (m_running) {
        const ssize_t n = ::recv(client.Get(), &temp, sizeof(temp), 0);
        if (n == 0) {
            return; // 对端正常关闭
        }

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error(ErrorMessage("recv failed"));
        }

        auto payloads = FrameCodec::Decode(readBuffer, temp, static_cast<std::size_t>(n));
        for (const auto &payload : payloads) {
            const bool keepAlive = ProcessPayload(client.Get(), payload);
            if (!keepAlive) {
                return;
            }
        }
    }
}

bool TcpServer::ProcessPayload(int clientFd, const std::string &payload)
{
    const Command cmd = ParseCommand(payload);
    const std::string response = CommandExecutor::Execute(m_store, cmd);
    SendResponse(clientFd, response);

    return cmd.type != CommandType::Quit; // QUIT 只关闭当前客户端连接，不关闭整个服务端。
}

void TcpServer::SendResponse(int clientFd, const std::string &response)
{
    const std::string frame = FrameCodec::Encode(response);
    SendAll(clientFd, frame);
}

void TcpServer::SendAll(int clientFd, const std::string &data)
{
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(clientFd, data.data() + sent, data.size() - sent, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error(ErrorMessage("send failed"));
        }

        if (n == 0) {
            throw std::runtime_error("send returned 0");
        }

        sent += static_cast<std::size_t>(n);
    }
}

} // namespace tinykv
