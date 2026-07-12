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