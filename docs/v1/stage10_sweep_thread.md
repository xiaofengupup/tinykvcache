# 阶段 11：TTL 后台清理线程

## 1. 阶段目标

本阶段为 TcpServer 增加后台 TTL 清理线程。

后台线程会周期性调用：

```cpp
KVStore::SweepExpired();
```

主动清理已经过期但长期没有被访问的 key。

## 2. 为什么需要后台清理

阶段 3 中 KVStore 已经实现 lazy delete。

lazy delete 的含义是：

访问 key 时检查是否过期，如果已过期，顺手删除

但是如果一个 key 过期后一直没人访问，它就会暂时留在 unordered_map 中。

后台 sweeper 线程可以定期扫描并删除这些 key。

## 3. 当前过期清理策略

本项目采用：lazy delete + background sweep 两者结合：

- GET / TTL / DEL / EXPIRE 时会检查并清理过期 key；
- 后台线程周期性清理长期未访问的过期 key。

## 4. 线程安全

在这一阶段完成后，KVStore 会被两个线程访问：

- 主线程：处理客户端命令；
- 后台线程：清理过期 key。

KVStore 内部使用 mutex 保护 unordered_map。

因此主线程和后台线程可以安全访问 KVStore。

## 5. 为什么使用 condition_variable

如果后台线程直接 sleep_for，每次停止服务端时可能需要等待一个完整 sleep 周期。

本项目使用 condition_variable::wait_for。

这样 stop_sweeper_thread 可以通过 notify_all 立即唤醒后台线程，让服务端更快退出。

## 6. 当前实现限制

当前 sweep_expired 的复杂度是 O(N)。

如果 key 数量很大，每次扫描整个 unordered_map 会有成本。

后续可以优化为：

- 小根堆；
- 时间轮；
- 分片扫描；
- 分桶过期。

当前版本为了项目可控性，使用简单可靠的全量扫描。

## 7. 面试表达

KVStore 的过期清理采用 lazy delete 和后台 sweep 相结合的策略。

访问 key 时会检查是否过期并删除，后台 sweeper 线程会周期性调用 sweep_expired 清理长期未访问的过期 key。

因为 KVStore 会同时被主事件循环和后台线程访问，所以内部使用 mutex 保护 unordered_map。

后台线程使用 condition_variable::wait_for 实现周期等待，这样 stop 时可以及时唤醒并退出。