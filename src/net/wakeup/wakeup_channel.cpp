#include "tinykv/net/wakeup/wakeup_channel.h"
#include "tinykv/net/socket_util.h"

#include <stdexcept>
#include <string>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/types.h>
#include <utility>
#include <cstdint>
#include <array>
#include <unistd.h>
#include <fcntl.h>

namespace tinykv {

namespace {

std::string ErrnoMessage(const char* operation)
{
    return std::string(operation) + ": " + std::strerror(errno);
}

/**
 * 给文件描述符 fd 设置 close-on-exec 标志，使当前进程执行 exec 系列函数启动新程序时，内核自动关闭这个 fd
 * 
 * 注意，它不是在 fork() 时关闭，而是在 exec() 成功时关闭。
 *   fork 后：子进程仍然继承 fd
 *   exec 后：设置了 FD_CLOEXEC 的 fd 被关闭
 * 
 * 什么是 exec?
 *   Unix 系统中，进程可以通过 execve/execl/execvp 等函数，把当前进程替换成另一个程序。
 *   默认情况下，当前进程已经打开的文件描述符可能会被新程序继承，包括：监听socket、客户端socket、pipe、socketpair、普通文件、日志文件
 *   但很多 fd 并不应该被新程序继承。
 */
void SetCloseOnExec(int fd)
{
    const int flags = ::fcntl(fd, F_GETFD, 0);
    if (flags < 0) {
        throw std::runtime_error(ErrnoMessage("fcntl F_GETFD failed"));
    }

    if (::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
        throw std::runtime_error(ErrnoMessage("fcntl F_SETFD failed"));
    }
}

}

WakeupChannel::WakeupChannel()
{   
    // socketpair() 会返回一对已经互相连接的 socket，适合用作进程内部通知通道；
    // 这一机制可以同时被 poll 和 epoll 监听
    int rawFds[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, rawFds) < 0) {
        throw std::runtime_error(ErrnoMessage("socketpair failed"));
    }

    ScopedFd readFd(rawFds[0]);
    ScopedFd writeFd(rawFds[1]);

    // 两端都设置非阻塞，信号处理写入时不能因为缓冲区已满而阻塞
    SetNonBlocking(readFd.Get());
    SetNonBlocking(writeFd.Get());
    SetCloseOnExec(readFd.Get());
    SetCloseOnExec(writeFd.Get());

    m_readFd = std::move(readFd);
    m_writeFd = std::move(writeFd);
}

int WakeupChannel::ReadFd() const noexcept
{
    return m_readFd.Get();
}

int WakeupChannel::WriteFd() const noexcept
{
    return m_writeFd.Get();
}

void WakeupChannel::Notify() noexcept
{
    const std::uint8_t byte = 1U;

    while (true) {
        /**
         * ::socketpair() 创建的 socket 是全双工的，读端和写端都可以同时进行。
         * 这里写入一个字节后，读端就会变为可读，poll/epoll 就会通知事件循环线程，具体流程如下：
         * 
         *  writeFd 写入数据
         *      ↓
         *  数据进入对端 socket 的接收缓冲区
         *      ↓
         *  readFd 对应的接收缓冲区非空
         *      ↓
         *  内核认为 readFd 可读
         */
        const ssize_t written = ::write(m_writeFd.Get(), &byte, sizeof(byte));
        if (written == static_cast<ssize_t>(sizeof(byte))) {
            return;
        }

        if (written < 0 && errno == EINTR) {
            continue;
        }

        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return; // 通道已满时，说明已经有唤醒事件等待处理，不需要继续写入
        }

        return;
    }
    
}

void WakeupChannel::Drain()
{
    std::array<char, 256> buffer{};

    while (true) {
        const ssize_t received = ::read(m_readFd.Get(), buffer.data(), buffer.size());
        if (received > 0) {
            continue;
        }

        if (received == 0) {
            return;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }

        throw std::runtime_error(ErrnoMessage("wakeup channel read failed"));
    }
}

} // namespace tinykv