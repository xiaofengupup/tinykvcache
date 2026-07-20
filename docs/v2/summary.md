# TinyKVCache 五天准生产化优化报告

## 1. 优化背景

基础版本已经具备：

- TCP 协议
- KVStore
- poll Reactor
- TTL
- 部署与压测

本轮目标是提升：

- 正确性
- 资源边界
- 可扩展性
- 生命周期管理
- 可观测性

## 2. Day 1：质量门禁

- Werror
- ASan
- UBSan
- TSan
- E2E
- CI

## 3. Day 2：背压与公平性

- OutputBuffer
- 高低水位
- 硬上限
- 读写预算
- 慢客户端保护

## 4. Day 3：Poller 抽象

- PollPoller
- EpollPoller
- 自动后端选择
- 公共契约测试

## 5. Day 4：优雅退出

- WakeupChannel
- SIGINT/SIGTERM
- Draining
- 超时关闭
- sweeper join

## 6. Day 5：可观测性

- Logger
- Metrics
- STATS
- 延迟直方图
- benchmark 分位数

## 7. 性能对比

填写基线版本和优化后版本结果。

## 8. 已解决的问题

列出实际修复：

- Release assert 被 NDEBUG 移除
- pthread 链接
- 非法 CMake 宏
- POLLOUT 兴趣更新
- writeBuffer 内存移动
- 慢客户端内存增长
- 停止事件循环唤醒
- 信号安全

## 9. 当前限制

- 单线程 Reactor
- 无持久化
- 无认证
- 无 TLS
- TTL 全量扫描
- 指标没有外部导出
- 没有连接空闲超时

## 10. 下一步方向

- kqueue
- timer wheel
- 线程池
- Prometheus exporter
- 配置文件
- CI benchmark