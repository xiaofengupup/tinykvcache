# Day 3：Poller 抽象与 epoll 后端

## 1. 优化目标

解除 TcpServer 对 poll 系统调用的直接依赖。

## 2. 抽象接口

Poller 支持：

- Add
- Modify
- Remove
- Wait

统一事件：

- Read
- Write
- Error
- Hangup

## 3. PollPoller

每次 Wait 时根据注册表构建 pollfd 数组。

适用于 macOS 和 Linux。

## 4. EpollPoller

Linux 专用。

使用：

- epoll_create1
- epoll_ctl
- epoll_wait

当前使用 level-triggered 模式。

## 5. 后端选择

- macOS Auto -> poll
- Linux Auto -> epoll
- Linux 可以强制 poll 做对照

## 6. TcpServer 改造

TcpServer 不再包含 pollfd，也不再直接调用 poll。

Connection 保存 registeredEvents，仅在兴趣事件变化时调用 Modify。

## 7. 测试

记录：

- Poller 公共契约测试
- macOS poll 测试
- Linux poll 测试
- Linux epoll 测试
- e2e
- 慢客户端
- ASan/UBSan
- TSan

## 8. 性能对比

填写 poll 和 epoll 的压测结果。

## 9. 发现的问题

记录今天实际发现的问题和修复方法。