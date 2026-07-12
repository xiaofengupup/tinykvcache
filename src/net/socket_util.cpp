#include "tinykv/net/socket_util.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>

namespace tinykv {

void SetNonBlocking(int fd)
{
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        throw std::runtime_error("fcntl(F_GETFL) failed: " + std::string(std::strerror(errno)));
    }

    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        throw std::runtime_error("fcntl(F_SETFL) failed: " + std::string(std::strerror(errno)));
    }
}

ScopedFd CreateListenSocket(const std::string &host, int port, int backlog)
{
    // 创建 socket
    // protocol: 0 表示使用默认协议（对于 SOCK_STREAM，通常是 TCP）
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throw std::runtime_error("socket() failed: " + std::string(std::strerror(errno)));
    }

    ScopedFd socketFd(fd); // 使用 ScopedFd 管理 fd，确保异常时自动关闭

    // 设置 SO_REUSEADDR，允许端口快速复用
    int opt = 1;
    if (::setsockopt(socketFd.Get(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        throw std::runtime_error("setsockopt(SO_REUSEADDR) failed: " + std::string(std::strerror(errno)));
    }

    // 绑定地址
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        throw std::runtime_error("inet_pton() failed: " + std::string(std::strerror(errno)));
    }

    if (::bind(socketFd.Get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        throw std::runtime_error("bind() failed: " + std::string(std::strerror(errno)));
    }

    // 开始监听
    if (::listen(socketFd.Get(), backlog) < 0) {
        throw std::runtime_error("listen() failed: " + std::string(std::strerror(errno)));
    }

    return socketFd;
}

ScopedFd ConnectToServer(const std::string &host, int port)
{
    // 创建 socket
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throw std::runtime_error("socket() failed: " + std::string(std::strerror(errno)));
    }

    ScopedFd socketFd(fd); // 使用 ScopedFd 管理 fd，确保异常时自动关闭

    // 设置服务器地址
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        throw std::runtime_error("inet_pton() failed: " + std::string(std::strerror(errno)));
    }

    // 连接服务器
    if (::connect(socketFd.Get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        throw std::runtime_error("connect() failed: " + std::string(std::strerror(errno)));
    }

    return socketFd;
}

} // namespace tinykv