/**
 * SocketUtil 创建和配置 socket 的工具类
 */

#pragma once

#include <string>
#include "tinykv/net/scoped_fd.h"

namespace tinykv {

/**
 * 设置 socket 为非阻塞模式
 * 
 * 非阻塞 fd 调用 read/write/send/recv 时，如果当前无法完成操作，不会一直等待，而是返回错误
 */
void SetNonBlocking(int fd);

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
void SetCloseOnExec(int fd);

/**
 * 创建 TCP 监听 socket，并绑定到指定的 host:port 上
 * 
 * 内部流程：socket() -> setsockopt() -> bind() -> listen()
 * 
 * 返回值：返回一个 ScopedFd 对象，表示创建的监听 socket。
 */
ScopedFd CreateListenSocket(const std::string &host, int port, int backlog = 128);

/**
 * 创建 TCP 客户端连接
 * 
 * 内部流程：socket() -> connect()
 * 
 * 返回值：返回一个 ScopedFd 对象，表示创建的客户端 socket。
 */
ScopedFd ConnectToServer(const std::string &host, int port);

} // namespace tinykv