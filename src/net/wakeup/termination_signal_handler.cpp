#include "tinykv/net/wakeup/termination_signal_handler.h"

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace tinykv {

namespace {

// 信号处理器与普通程序共享的数据应尽量简单，sig_atomic_t 适合这种最小状态交换
volatile sig_atomic_t G_NOTIFY_FD = -1;

std::string ErrnoMessage(const char* operation)
{
    return std::string(operation) + ": " + std::strerror(errno);
}

void HandleTerminationSignal(int signalNumber) noexcept
{
    (void)signalNumber;

    const int savedErrno = errno;
    const int notifyFd = static_cast<int>(G_NOTIFY_FD);
    if (notifyFd >= 0) {
        const std::uint8_t byte = 1U;
        const ssize_t ignored = ::write(notifyFd, &byte, sizeof(byte));
        (void)ignored;
    }

    errno = savedErrno;
}

sigaction MakeTerminationAction()
{
    sigaction action {};
    action.sa_handler = HandleTerminationSignal;
    action.sa_flags = 0;

    // 将 sa_mask（信号掩码）初始化为空集
    // 信号掩码的作用：在执行信号处理函数期间，sa_mask 中指定的信号会被阻塞。
    if (::sigemptyset(&action.sa_mask) < 0) {
        throw std::runtime_error(ErrnoMessage("sigemptyset failed"));
    }

    return action;
}

sigaction MakeIgnoreAction()
{
    sigaction action {};
    action.sa_handler = SIG_IGN;
    action.sa_flags = 0;

    if (::sigemptyset(&action.sa_mask) < 0) {
        throw std::runtime_error(ErrnoMessage("sigemptyset failed"));
    }

    return action;
}

TerminationSignalHandler::TerminationSignalHandler(int notifyFd)
{
    if (notifyFd < 0) {
        throw std::invalid_argument("termination notify fd must be valid");
    }

    if (G_NOTIFY_FD > 0) {
        throw std::logic_error("termination signal handler already installed");
    }

    G_NOTIFY_FD = static_cast<sig_atomic_t>(notifyFd);
    const sigaction terminationAction = MakeTerminationAction();
    const sigaction ignoreAction = MakeIgnoreAction();

    // int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
    if (::sigaction(SIGINT, &terminationAction, &m_previousSigint) < 0) {
        G_NOTIFY_FD = -1;
        throw std::runtime_error(ErrnoMessage("sigaction SIGINT failed"));
    }

    if (::sigaction(SIGTERM, &terminationAction, &m_previousSigterm) < 0) {
        (void)::sigaction(SIGINT, &m_previousSigint, nullptr);
        G_NOTIFY_FD = -1;
        throw std::runtime_error(ErrnoMessage("sigaction SIGTERM failed"));
    }

    // 避免向已经关闭的 socket 发送数据时，SIGPIPE 直接终止整个服务进程。
    if (::sigaction(SIGPIPE, &ignoreAction, &m_previousSigpipe) < 0) {
        (void)::sigaction(SIGINT, &m_previousSigint, nullptr);
        (void)::sigaction(SIGTERM, &m_previousSigterm, nullptr);
        G_NOTIFY_FD = -1;
        throw std::runtime_error(ErrnoMessage("sigaction SIGTERM failed"));
    }

    m_installed = true;
}

TerminationSignalHandler::~TerminationSignalHandler()
{
    if (!m_installed) {
        return;
    }

    // 先恢复原来的信号处理器，再使全局通知 fd 失效
    (void)::sigaction(SIGINT, &m_previousSigint, nullptr);
    (void)::sigaction(SIGTERM, &m_previousSigterm, nullptr);
    (void)::sigaction(SIGPIPE, &m_previousSigpipe, nullptr);

    G_NOTIFY_FD = -1;
}

}

} // namespace tinykv