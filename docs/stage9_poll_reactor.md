# 阶段 9：poll Reactor 多客户端服务端

## 1. 阶段目标

本阶段将阶段 8 的阻塞 TcpServer 升级为 poll Reactor 模型。

升级后，服务端可以用单线程同时管理多个客户端连接。

## 2. 阶段 9 的问题

阶段 9 是阻塞模型：

```text
accept 一个客户端
  ↓
阻塞 recv 处理该客户端
  ↓
客户端断开
  ↓
再处理下一个客户端
```
如果一个客户端连接后不发送数据，服务端会阻塞在 recv，其他客户端无法被处理。

## 3. poll reactor的思想
`poll` 可以同时监听多个 fd。在本项目中，监听：

1. listen fd：有新客户端连接时触发 POLIN；
2. client fd：有数据可读时触发 POLLIN；
3. client fd：有待发送数据时触发 POLLOUT；

服务端只处理当前有事件的 fd，不会被某个空闲客户端阻塞。

## 4. Connection 状态
每个客户端连接都有独立状态：

```cpp
ScopedFd fd;
std::string read_buffer;
std::string write_buffer;
bool close_after_write;
bool closed;
```

`read_buffer` 用于处理 TCP 半包和粘包。

`write_buffer` 用于处理非阻塞 send 没有一次写完的情况。


## 5. 非阻塞 fd
在这一阶段中，listen fd 和 client fd 都设置为非阻塞。

这样 accept、recv、send 在暂时不能完成时不会卡住，而是返回 EAGAIN 或 EWOULDBLOCK。

## 6. accept_new_clients
当 poll 告诉 listen fd 可读时，说明有新连接到来。

由于可能同时有多个连接在等待队列中，accept_new_clients 会循环 accept，直到返回 EAGAIN 或 EWOULDBLOCK。

## 7. read_buffer

TCP 是字节流，不保留消息边界。

一个客户端可能一次只发来半条消息，也可能一次发来多条消息。

因此每个连接都需要自己的 read_buffer。

收到数据后，服务端把数据追加到 read_buffer，再调用 FrameCodec 尽可能解析完整 payload。

## 8. write_buffer

非阻塞 send 不保证一次写完整个响应。

如果只写出一部分，剩余数据必须保存在 write_buffer 中。

当 poll 再次通知该 fd 可写时，继续发送剩余数据。

## 9. 为什么不能一直监听 POLLOUT

大多数 socket 大部分时间都是可写的。

如果一直监听 POLLOUT，poll 会频繁返回，导致 CPU 空转。

所以本项目只在 write_buffer 非空时监听 POLLOUT。

## 11. 当前版本限制

当前版本仍然是单线程 poll Reactor。

它可以同时处理多个客户端，但所有事件处理都在一个线程中完成。

如果某个业务处理耗时很长，仍然会影响其他连接。

后续可以引入线程池，将耗时任务交给 worker 线程处理。

## 12. 面试表达

这一阶段中，我把阻塞 TcpServer 改造成了单线程 poll Reactor。

服务端将 listen fd 和所有 client fd 放入 pollfd 数组中，poll 返回后只处理有事件的 fd。

listen fd 可读时 accept 新连接；client fd 可读时 recv 数据并通过 FrameCodec 解析请求；如果响应数据没有一次写完，就保存在连接级 write_buffer 中，等 fd 可写时继续发送。

每个连接都有独立的 read_buffer 和 write_buffer，因此可以正确处理 TCP 半包、粘包以及非阻塞 send 的部分写问题。