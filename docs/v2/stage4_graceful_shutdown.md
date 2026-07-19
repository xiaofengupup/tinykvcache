# Day 4：可唤醒事件循环与优雅退出

## 1. 优化目标

- Stop 可以跨线程调用
- SIGINT/SIGTERM 触发优雅退出
- Poller::Wait 可以立即被唤醒
- 已排队响应尽量发送完成
- 超时后强制关闭连接
- sweeper 正常 join

## 2. WakeupChannel

使用 socketpair 创建内部通知通道。

read fd 注册到 Poller，write fd 用于跨线程和信号通知。

## 3. 信号安全

信号处理器只调用 write，不执行日志、锁和复杂 C++ 逻辑。

## 4. 退出状态机

Created -> Running -> Draining -> Stopped

## 5. Draining 阶段

- 停止 accept
- 停止读取新请求
- 继续发送 writeBuffer
- 超时后强制关闭

## 6. 测试

记录：

- WakeupChannel 单测
- SIGTERM 单测
- Ctrl+C 手动测试
- poll 测试
- epoll 测试
- ASan/UBSan
- TSan

## 7. 实际发现的问题

记录当天出现的生命周期、信号或线程问题。