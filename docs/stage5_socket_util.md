# 阶段 5：SocketUtil 网络工具模块

## 1. 阶段目标

本阶段实现 socket 创建和配置工具。

SocketUtil 负责：

1. 创建 TCP server socket；
2. 创建 TCP client socket；
3. 设置 fd 非阻塞模式。

本阶段不负责连接管理，不实现 accept、recv、send 和 Reactor。

## 2. 服务端 socket 创建流程

TCP server 创建流程：

socket()

↓

setsockopt()

↓

bind()

↓

listen()


## 3. socket()

socket 用于创建通信端点。

参数：

AF_INET：

表示 IPv4。

SOCK_STREAM：

表示 TCP。

## 4. setsockopt()

SO_REUSEADDR 可以允许端口快速复用。

避免服务器重启时由于 TIME_WAIT 状态导致：

Address already in use。

## 5. bind()

bind 将 socket 绑定到指定 IP 和端口。

例如：

127.0.0.1:8888

## 6. listen()

listen 将 socket 转换成监听 socket。

监听 socket 只负责等待新的客户端连接。

accept 后会产生新的 client socket。

## 7. connect()

客户端通过 connect 连接服务器。

连接成功后，客户端和服务端之间建立 TCP 连接。

## 8. 非阻塞 socket

默认 socket 是阻塞模式。

例如：

recv 没有数据时，会一直等待。

通过：

fcntl(fd,F_SETFL,O_NONBLOCK)

可以设置非阻塞模式。

非阻塞模式下，如果暂时无法完成操作，会返回 EAGAIN。

## 9. 为什么 SocketUtil 返回 ScopedFd

socket fd 是系统资源。

通过 ScopedFd 管理，可以：

1. 自动 close；
2. 避免资源泄漏；
3. 明确 fd 所有权。

## 10. 模块职责划分

SocketUtil：

负责创建 socket。


ScopedFd：

负责管理 fd 生命周期。


TcpServer：

负责连接管理和事件循环。


## 11. 面试表达

SocketUtil 是网络基础设施层。

它封装了 socket 创建过程，包括 socket、setsockopt、bind、listen。

创建出的 socket 使用 ScopedFd 管理生命周期，避免 fd 泄漏。

后续 TcpServer 不需要关心底层 socket 创建细节，只负责 accept、IO 和事件处理。