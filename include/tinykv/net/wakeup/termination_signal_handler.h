/**
 * 终止信号处理器
 */
#pragma once

#include <signal.h>

namespace tinykv {

/**
 * 信号终止处理器：安装 SIGINT / SIGTERM 处理器，并忽略 SIGPIPE。
 * 
 * SIGINT / SIGTERM 处理器只向 notifyFd 写入一个字节，不直接操作 TcpServer 对象
 */
class TerminationSignalHandler {
public:
    explicit TerminationSignalHandler(int notifyFd);
    ~TerminationSignalHandler();

    TerminationSignalHandler(const TerminationSignalHandler&) = delete;
    TerminationSignalHandler& operator=(const TerminationSignalHandler&) = delete;

private:
    bool m_installed { false };

    sigaction m_previousSigint {};
    sigaction m_previousSigterm {};
    sigaction m_previousSigpipe {};
};

} // namespace tinykv
