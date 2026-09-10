# 阶段 3：KVStore 内存存储模块

## 1. 阶段目标

本阶段实现 TinyKVCache 的内存 key-value 存储模块。

KVStore 负责真正保存 key-value 数据，并支持：

1. SET
2. GET
3. DEL
4. EXPIRE
5. TTL
6. STATS
7. 过期 key 清理

本阶段不涉及 TCP 网络，也不涉及命令文本解析。

## 2. 模块职责

KVStore 只负责数据存储和过期逻辑。

它不关心：

1. TCP 粘包半包；
2. FrameCodec；
3. 命令字符串；
4. 客户端连接；
5. 响应格式。

这样可以让存储层保持独立，方便单元测试。

## 3. 数据结构

KVStore 内部使用：

```cpp
std::unordered_map<std::string, Entry>
```

其中，`Entry`保存：
```cpp
std::string value;
std::optional<TimePoint> expire_at;
```

expire_at 的含义：
```
std::nullopt：key 没有过期时间
有值：key 在指定时间点过期
```

## 4.SET 语义
SET 会写入或覆盖 key。本项目约定：
```
SET 会清除 key 原来的 TTL。
```
例如：
```
SET name xiaofeng
EXPIRE name 10
SET name han
TTL name
```
此时，TTL 命令返回 -1，表示 key 存在，但没有过期时间。

## 5. GET 语义

GET 时如果 key 存在且未过期，返回 value。

如果 key 不存在，返回 false。

如果 key 已过期，KVStore 会删除该 key，然后返回 false。

## 6. DEL 语义

DEL 时如果 key 存在且未过期，删除并返回 true。

如果 key 不存在，返回 false。

如果 key 已经过期，先删除，再返回 false。

## 7. EXPIRE 语义

EXPIRE 用来设置 key 的过期时间。

seconds 必须大于 0。

如果 key 不存在，返回 false。

如果 key 已过期，删除 key，返回 false。

## 8. TTL 语义

TTL 返回值约定：
```
-2：key 不存在，或者 key 已过期
-1：key 存在，但没有过期时间
>=0：key 剩余过期秒数
```

本项目对剩余时间做向上取整。

例如 key 还剩 500ms，也会返回 1 秒。

## 9. lazy delete
KVStore 使用 lazy delete 处理过期 key。

也就是说，key 到期时不会立刻自动删除。

当 GET / TTL / DEL / EXPIRE / size / stats 访问到它时，才会检查是否过期，并删除它。

优点是实现简单。

缺点是如果过期 key 一直不被访问，可能会暂时占用内存。

## 10. SweepExpired
为了清理那些一直没有被访问的过期 key，KVStore 提供：
```cpp
std::size_t SweepExpired();
```

它会扫描整个 unordered_map，删除所有过期 key，并返回删除数量。

后续阶段可以用后台线程周期性调用这个方法。

## 11. 为什么使用 steady_clock

TTL 是相对时间，不应该受系统时间调整影响。

如果使用 system_clock，当系统时间被手动修改或 NTP 校准时，可能影响过期判断。

所以本项目使用 steady_clock，它是单调递增时钟，更适合做超时和 TTL 判断。

## 12. 线程安全

KVStore 内部使用 mutex 保护 unordered_map。

所有公开方法都会加锁。

这样后续即使有主事件循环和后台清理线程同时访问 KVStore，也不会出现 unordered_map 并发读写问题。


13. 本阶段测试覆盖

本阶段测试覆盖：

1. SET / GET；
2. SET 覆盖旧值；
3. DEL；
4. TTL 查询不存在 key；
5. TTL 查询无过期时间 key；
6. EXPIRE 不存在 key；
7. EXPIRE 非法 seconds；
8. 正常 EXPIRE 和 TTL；
9. key 真实过期；
10. SET 清除旧 TTL；
11. sweep_expired 清理过期 key；
12. stats 统计；
13. 已过期 key 不算存在。

## 14. 面试表达

KVStore 是项目的存储层，内部使用 unordered_map 保存 key-value。

每个 Entry 除了 value，还保存一个可选的 expire_at 时间点。没有 expire_at 表示永久 key，有 expire_at 表示会在指定时间过期。

TTL 判断使用 steady_clock，避免系统时间变化影响过期逻辑。

过期清理采用 lazy delete 加 sweep_expired 的组合：访问 key 时会检查是否过期并删除，同时也提供 sweep_expired 给后台线程周期性清理。

KVStore 内部使用 mutex 保护 unordered_map，保证后续主线程和后台清理线程同时访问时不会发生数据竞争。