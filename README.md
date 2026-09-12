# TinyKVCache

TinyKVCache 是一个基于 **C++17** 实现的轻量级 TCP 内存 KV 缓存服务。它不是 Redis 的替代品，而是一个从底层 socket、应用层协议、Reactor 事件循环、背压、TTL、优雅退出一路搭起来的网络服务学习项目。

当前代码已经演进到 **Main Reactor + 多 Sub Reactor** 架构：

- Main Reactor 负责监听 socket、accept 新连接、分发连接和服务生命周期管理；
- Sub Reactor 运行在独立线程中，负责客户端连接读写、协议解析、命令执行、背压和连接关闭；
- 多个 Sub Reactor 共享一个线程安全的 `KVStore`；
- TTL 清理由后台 sweeper 线程周期执行；
- SIGINT/SIGTERM 通过 `socketpair` 唤醒事件循环，进入 graceful shutdown。

项目配套提供客户端、TOML 配置、单元测试、集成测试、冒烟测试、慢客户端隔离测试、benchmark 脚本和 GitHub Actions CI，适合用来学习 **Linux/macOS 网络编程、Reactor 模型、TCP 应用层协议、C++ 工程化与服务端可观测性**。

---

## 特性

- C++17 + CMake 工程化构建
- 非阻塞 socket
- Main Reactor + 多 Sub Reactor 事件驱动模型
- Poller 抽象：Linux 支持 `epoll`，其他平台使用 `poll`
- 自定义帧协议：4 字节网络序长度头 + 文本 payload
- 正确处理 TCP 粘包、半包、pipeline、部分读和部分写
- 连接级读缓冲和偏移式写缓冲
- 写侧背压：高水位暂停读、低水位恢复、硬上限关闭慢客户端
- Reactor 公平性预算：限制单轮 accept 数量和单次 write 字节数
- 内存 KV 存储：`SET` / `GET` / `DEL` / `EXPIRE` / `TTL`
- TTL：惰性删除 + 最小堆过期索引 + 后台 sweeper
- 分级日志：debug / info / warn / error / off
- 服务端指标：连接、流量、命令、错误、背压、慢客户端、TTL 清理和延迟直方图
- Graceful shutdown：停止 accept 和读新请求，尽量发送已排队响应，超时后强制关闭
- TOML 配置文件 + 命令行覆盖
- 单元测试、集成测试、Sanitizer 构建选项和 CI

---

## 架构

```text
                      +----------------------+
                      |      tinykv_server   |
                      +----------+-----------+
                                 |
                         Main Reactor
              listen fd / stop wakeup / accept budget
                                 |
                    round-robin 分发新连接
                                 |
          +----------------------+----------------------+
          |                      |                      |
   Sub Reactor #0         Sub Reactor #1         Sub Reactor #N
   client fds             client fds             client fds
   read/write             read/write             read/write
          |                      |                      |
          +----------+-----------+-----------+----------+
                     |
                FrameCodec
                     |
              CommandParser
                     |
             CommandExecutor
                     |
                  KVStore
          unordered_map + mutex + TTL heap
                     |
          +----------+-----------+
          |                      |
   ServerMetrics           TTL Sweeper
   atomic counters         background thread
```

核心模块：

| 模块 | 职责 |
| --- | --- |
| `TcpServer` | Main Reactor、监听 socket、accept、Sub Reactor 管理、优雅退出协调 |
| `SubReactor` | 客户端连接事件循环、读写缓冲、协议处理、背压、连接生命周期 |
| `Poller` | IO 多路复用统一接口 |
| `PollPoller` | 基于 POSIX `poll` 的跨平台后端 |
| `EpollPoller` | Linux `epoll` 后端 |
| `WakeupChannel` | 基于 `socketpair` 的跨线程唤醒 |
| `TerminationSignalHandler` | SIGINT/SIGTERM 信号处理，handler 内只写 fd |
| `FrameCodec` | 4 字节长度头帧编解码 |
| `CommandParser` | 文本命令解析，命令名大小写不敏感 |
| `CommandExecutor` | 执行命令并生成响应 |
| `KVStore` | 线程安全内存 KV，支持 TTL 和过期清理 |
| `OutputBuffer` | 偏移式写缓冲，减少部分写后的内存搬移 |
| `ServerMetrics` | 原子指标和命令延迟直方图 |
| `Logger` | 全局分级日志 |

---

## 协议

TinyKVCache 使用二进制帧包裹文本命令：

```text
+----------------------+----------------+
| 4 bytes payload size | payload bytes  |
+----------------------+----------------+
```

- 长度字段为 4 字节无符号整数，使用网络字节序（big endian）。
- payload 是文本命令，例如 `SET name tinykv`。
- 单个 payload 最大为 `1 MiB`，超过会被判定为协议错误。
- 请求和响应都使用同样的帧格式。

示例：`SET name tinykv`

```text
00 00 00 0f 53 45 54 20 6e 61 6d 65 20 74 69 6e 79 6b 76
```

其中 `00 00 00 0f` 表示 payload 长度为 15。

---

## 命令

| 命令 | 说明 | 示例 |
| --- | --- | --- |
| `PING` | 心跳检测 | `PING` |
| `SET key value` | 写入键值，覆盖时清除原 TTL，value 可包含空格 | `SET msg hello tiny kv` |
| `GET key` | 查询 key | `GET msg` |
| `DEL key` | 删除 key | `DEL msg` |
| `EXPIRE key seconds` | 设置正整数秒级过期时间 | `EXPIRE msg 10` |
| `TTL key` | 查询剩余过期秒数 | `TTL msg` |
| `STATS` | 查看存储统计和服务端运行指标 | `STATS` |
| `HELP` | 查看支持的命令 | `HELP` |
| `QUIT` | 服务端发送 `+BYE` 后关闭当前连接 | `QUIT` |

响应约定：

| 响应 | 含义 |
| --- | --- |
| `+OK` | 操作成功 |
| `+PONG` | `PING` 响应 |
| `+BYE` | `QUIT` 响应 |
| `$value` | 查询类命令返回值 |
| `$nil` | key 不存在、已过期，或删除/设置过期失败 |
| `-ERR ...` | 未知命令、参数错误或协议错误 |

`TTL` 返回值与 Redis 风格接近：

- `>= 0`：key 剩余过期秒数；
- `-1`：key 存在但没有 TTL；
- `-2`：key 不存在或已过期。

`STATS` 返回一行 `+key=value,...` 格式文本，包含：

- 存储统计：`keys`、`persistent`、`expiring`
- 连接统计：`connections_active`、`connections_accepted`、`connections_closed`
- 流量统计：`bytes_received`、`bytes_sent`、`frames_received`
- 命令统计：`commands`、`command_errors`、`protocol_errors`
- 背压统计：`slow_clients`、`read_pauses`、`read_resumes`、`max_pending_write_bytes`
- TTL 统计：`expired_removed`
- 延迟统计：`latency_avg_us`、`latency_max_us`、`latency_p95_upper_us`、`latency_p99_upper_us`

---

## 构建

依赖：

- CMake >= 3.16
- 支持 C++17 的编译器
- Python 3，用于测试和压测脚本
- CMake FetchContent 会拉取 `fmt`、`toml++`，开启测试时会拉取 GoogleTest

开发构建：

```bash
./scripts/build.sh
```

Release 构建并运行 CTest：

```bash
./scripts/build_release.sh
```

手动构建：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DTINYKV_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

构建产物：

```text
build/tinykv_server
build/tinykv_client
build-release/tinykv_server
build-release/tinykv_client
```

CMake 选项：

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `TINYKV_BUILD_TESTS` | `ON` | 构建单元测试 |
| `TINYKV_BUILD_INTEGRATION_TESTS` | `ON` | 注册 Python 集成测试 |
| `TINYKV_WARNINGS_AS_ERRORS` | `ON` | 告警视为错误 |
| `TINYKV_ENABLE_ASAN` | `OFF` | AddressSanitizer |
| `TINYKV_ENABLE_UBSAN` | `OFF` | UndefinedBehaviorSanitizer |
| `TINYKV_ENABLE_TSAN` | `OFF` | ThreadSanitizer |

Sanitizer 示例：

```bash
cmake -S . -B build-asan \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DTINYKV_ENABLE_ASAN=ON \
  -DTINYKV_ENABLE_UBSAN=ON
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure
```

---

## 配置

推荐使用配置文件启动：

```bash
./build-release/tinykv_server --config config/tinykv.toml
```

也可以用脚本启动 Release 产物：

```bash
./scripts/run_server.sh --config config/tinykv.toml
```

命令行参数会覆盖配置文件，配置优先级为：

```text
内置默认值 < TOML 配置文件 < 命令行参数
```

支持的命令行参数：

```text
-c, --config <path>       Configuration file
    --host <address>      Listen address
    --port <port>         Listen port
    --poller <backend>    auto|poll|epoll
    --sub-reactors <n>    0=auto, 1-16=explicit
    --log-level <level>   debug|info|warn|error|off
-h, --help                Show help
```

示例：

```bash
./scripts/run_server.sh \
  --config config/tinykv.toml \
  --host 127.0.0.1 \
  --port 7777 \
  --poller auto \
  --sub-reactors 4 \
  --log-level info
```

完整配置结构：

```toml
[server]
host = "0.0.0.0"
port = 7777

[reactor]
poller = "auto"
sub_reactors = 0

[network]
max_read_buffer_bytes = 1048580
write_high_watermark_bytes = 1048576
write_low_watermark_bytes = 524288
write_hard_limit_bytes = 4194304
max_write_bytes_per_event = 65536
max_accepts_per_event = 64

[shutdown]
graceful_timeout_ms = 5000

[ttl]
sweep_interval_ms = 5000

[logging]
level = "info"
```

配置说明：

| 配置 | 说明 |
| --- | --- |
| `server.host` | 监听地址，公网暴露前请注意当前项目没有认证和 TLS |
| `server.port` | 监听端口，范围 1-65535 |
| `reactor.poller` | `auto`、`poll` 或 `epoll` |
| `reactor.sub_reactors` | `0` 表示自动按 CPU 核心数估算并限制在 1-8；显式值范围为 1-16 |
| `network.max_read_buffer_bytes` | 单连接最大读缓冲 |
| `network.write_high_watermark_bytes` | 写缓冲达到该值后暂停读 |
| `network.write_low_watermark_bytes` | 写缓冲下降到该值后恢复读 |
| `network.write_hard_limit_bytes` | 写缓冲超过该上限时关闭慢客户端 |
| `network.max_write_bytes_per_event` | 单次可写事件最多发送的字节数 |
| `network.max_accepts_per_event` | 单次 listen 可读事件最多 accept 的连接数 |
| `shutdown.graceful_timeout_ms` | 优雅退出最大等待时间 |
| `ttl.sweep_interval_ms` | 后台 TTL sweeper 执行周期 |
| `logging.level` | 日志级别 |

---

## 使用

启动服务端：

```bash
./scripts/run_server.sh --config config/tinykv.toml
```

启动客户端：

```bash
./build-release/tinykv_client 127.0.0.1 7777
```

交互示例：

```text
> PING
+PONG
> SET name tinykv
+OK
> GET name
$tinykv
> SET sentence hello tiny kv cache
+OK
> GET sentence
$hello tiny kv cache
> EXPIRE name 10
+OK
> TTL name
$10
> DEL name
+OK
> GET name
$nil
> STATS
+keys=1,persistent=1,expiring=0,...
> QUIT
+BYE
```

用 Python 手工发帧：

```python
import socket
import struct

def encode(payload: str) -> bytes:
    data = payload.encode("utf-8")
    return struct.pack("!I", len(data)) + data

def recv_exact(sock: socket.socket, size: int) -> bytes:
    chunks = []
    while size > 0:
        chunk = sock.recv(size)
        if not chunk:
            raise RuntimeError("connection closed")
        chunks.append(chunk)
        size -= len(chunk)
    return b"".join(chunks)

with socket.create_connection(("127.0.0.1", 7777), timeout=3) as sock:
    sock.sendall(encode("PING"))
    header = recv_exact(sock, 4)
    length = struct.unpack("!I", header)[0]
    print(recv_exact(sock, length).decode("utf-8"))
```

用 `nc` 快速看原始响应帧：

```bash
printf '\x00\x00\x00\x04PING' | nc 127.0.0.1 7777 | xxd -g 1
```

响应 payload 前也会带 4 字节长度头。

---

## 测试

运行全部 CTest：

```bash
ctest --test-dir build --output-on-failure
```

运行 Release 构建脚本时会自动执行 CTest：

```bash
./scripts/build_release.sh
```

主要测试覆盖：

| 测试 | 内容 |
| --- | --- |
| `tests/test_frame_codec.cpp` | 帧编码、半包、粘包、超大帧 |
| `tests/test_command_parser.cpp` | 命令解析、大小写、参数校验、SET value 空格 |
| `tests/test_kv_store.cpp` | SET/GET/DEL/TTL/过期清理 |
| `tests/test_command_executor.cpp` | 命令执行响应 |
| `tests/test_output_buffer.cpp` | 部分写消费和 compact |
| `tests/test_poller.cpp` | poller add/modify/remove/wait |
| `tests/test_wakeup_channel.cpp` | socketpair 唤醒和 drain |
| `tests/test_server_metrics.cpp` | 指标统计和延迟桶 |
| `tests/integration/test_server_e2e.py` | 服务端端到端、半包、pipeline、多客户端共享 KVStore |
| `tests/integration/test_graceful_shutdown.py` | SIGTERM graceful shutdown |
| `tests/integration/test_config_loading.py` | TOML、CLI override、错误配置路径 |

单独运行集成测试：

```bash
python3 tests/integration/test_server_e2e.py \
  --server ./build/tinykv_server

python3 tests/integration/test_graceful_shutdown.py \
  --server ./build/tinykv_server \
  --poller auto

python3 tests/integration/test_config_loading.py \
  --server ./build/tinykv_server
```

冒烟测试不会自动启动服务端，需要先启动 TinyKVCache：

```bash
./scripts/run_server.sh --config config/tinykv.toml
```

然后执行：

```bash
python3 scripts/smoke_test.py --host 127.0.0.1 --port 7777
```

慢客户端隔离测试会自行启动服务端，并强制 `--sub-reactors 1`，验证同一个 Sub Reactor 内慢客户端不会明显阻塞正常客户端：

```bash
python3 scripts/slow_client_test.py \
  --server ./build-release/tinykv_server \
  --poller auto
```

---

## Benchmark

`scripts/benchmark.py` 不会自动启动服务端。建议使用 Release 构建，并降低日志量：

```bash
./scripts/run_server.sh \
  --config config/tinykv.toml \
  --sub-reactors 4 \
  --log-level warn
```

运行压测：

```bash
python3 scripts/benchmark.py \
  --host 127.0.0.1 \
  --port 7777 \
  --connections 100 \
  --requests 1000 \
  --value-size 128
```

输出 JSON：

```bash
python3 scripts/benchmark.py \
  --host 127.0.0.1 \
  --port 7777 \
  --connections 100 \
  --requests 1000 \
  --output-json benchmark.json
```

每个 worker 使用一个 TCP 连接，每轮顺序执行：

```text
PING
SET
GET
```

理论请求数：

```text
connections * requests * 3
```

输出指标包括 QPS、平均延迟、P50、P95、P99、最大延迟、总错误数、完成/失败 worker 数。

可以固定 benchmark 参数，对比不同 Sub Reactor 数量：

```bash
--sub-reactors 1
--sub-reactors 2
--sub-reactors 4
--sub-reactors 8
--sub-reactors 16
```

由于当前多个 Sub Reactor 共享同一个 `KVStore` mutex，Reactor 数量增加后可能会出现锁竞争，这正好可以用 benchmark 观察吞吐量和尾延迟变化。

---

## 部署到 Linux

仓库提供了一个简单的 rsync + remote build 脚本：

```bash
./scripts/deploy_linux.sh user@host
./scripts/deploy_linux.sh user@host 22 ~/tinykv-cache
```

脚本会同步源码到远端，排除本地构建目录和 `.git`，然后在远端执行：

```bash
bash scripts/build_release.sh
```

远端启动：

```bash
./scripts/run_server.sh --config config/tinykv.toml
```

---

## 目录结构

```text
tiny-kv-cache/
├── app/
│   ├── server_main.cpp
│   └── client_main.cpp
├── cmake/
│   ├── compiler_warnings.cmake
│   └── sanitizers.cmake
├── config/
│   ├── tinykv.toml
│   └── tinykv.example.toml
├── include/tinykv/
│   ├── common/
│   ├── config/
│   ├── core/
│   ├── net/
│   │   ├── poll/
│   │   └── wakeup/
│   └── observability/
├── scripts/
│   ├── benchmark.py
│   ├── build.sh
│   ├── build_release.sh
│   ├── deploy_linux.sh
│   ├── run_server.sh
│   ├── slow_client_test.py
│   └── smoke_test.py
├── src/
│   ├── common/
│   ├── config/
│   ├── core/
│   ├── net/
│   └── observability/
├── tests/
│   ├── integration/
│   └── test_*.cpp
├── CMakeLists.txt
└── README.md
```

---

## 实现要点

### 1. Main/Sub Reactor

`TcpServer` 作为 Main Reactor，只处理监听 fd 和停止通知。新连接通过 round-robin 分配给 Sub Reactor，避免 Main Reactor 同时承担命令处理工作。

`SubReactor` 内部维护自己的 Poller、连接表、控制队列和 wakeup channel。Main Reactor 不直接跨线程修改 Sub Reactor 的连接表，而是把 fd 移入 pending 队列后唤醒对应线程。

### 2. Poller 抽象

`Poller` 屏蔽 `poll` 和 `epoll` 的差异，向上提供统一的注册、更新、移除和等待接口，以及 `IoEvent` 事件模型。

`auto` 后端在 Linux 选择 `epoll`，在其他平台选择 `poll`。连接对象缓存已注册事件，只有兴趣事件变化时才更新 Poller。

### 3. 读写缓冲与背压

每个连接拥有独立 `readBuffer` 和 `OutputBuffer`：

- `readBuffer` 保存尚未组成完整 frame 的字节；
- `OutputBuffer` 保存尚未发送完成的响应 frame；
- 部分写只推进读偏移，到达阈值后再 compact，减少 `erase(0, n)` 造成的内存搬移。

写缓冲达到高水位后暂停该连接读事件，下降到低水位后恢复。如果继续膨胀到 hard limit，则认为客户端消费过慢并关闭连接。

### 4. TTL

`KVStore` 使用 `std::unordered_map` 保存 key/value，使用 `std::mutex` 保证多个 Sub Reactor 和 sweeper 线程并发访问时的数据安全。

TTL 过期索引用最小堆维护，过期记录带 generation。重复 `EXPIRE` 或 `SET` 覆盖后，旧堆记录会自然变成 stale record，清理时通过 generation 过滤。堆内 stale record 过多时会触发 rebuild。

### 5. 优雅退出

收到 SIGINT/SIGTERM 后：

1. 信号 handler 向 wakeup fd 写入 1 字节；
2. Main Reactor 从 `Wait` 返回，进入 Draining；
3. Main Reactor 移除 listen fd，不再 accept；
4. Sub Reactor 停止读取新请求；
5. 已经排队的响应继续发送；
6. 所有连接排空或到达 deadline 后进入 Stopped；
7. sweeper 线程被唤醒并 join。

### 6. 可观测性

热路径指标使用原子计数，避免额外锁竞争。`STATS` 从 `KVStore` 和 `ServerMetrics` 读取快照并返回一行文本。

命令延迟使用固定桶估算百分位：

```text
<=10us <=50us <=100us <=500us <=1ms <=5ms >5ms
```

---

## 当前限制

- 数据仅保存在内存中，没有 AOF/RDB 持久化。
- 当前没有认证、ACL、TLS，不建议直接暴露到不可信公网。
- 命令协议是学习用途的简化文本协议，不支持 Redis RESP。
- 多个 Sub Reactor 共享一个 `KVStore` mutex，高并发写入下可能出现锁竞争。
- 当前没有 key 分片、连接空闲超时、内存淘汰策略和最大 key/value 数量控制。
- 指标只通过 `STATS` 暴露，没有 Prometheus exporter。
- macOS 当前使用 `poll`，尚未实现 `kqueue` 后端。

---

## 学习价值

这个项目把一个后端 TCP 服务拆成了可以逐层理解的工程问题：

1. 如何用 RAII 管理 fd 等系统资源；
2. TCP 字节流如何通过应用层 frame 解决粘包和半包；
3. 非阻塞读写和部分写为什么需要连接级缓冲；
4. Reactor 如何用 IO 多路复用管理大量连接；
5. 多 Reactor 之间如何安全移交连接；
6. 慢客户端如何通过背压和 hard limit 隔离；
7. TTL 如何结合惰性删除、后台清理和过期索引；
8. 信号安全的优雅退出如何落到代码；
9. 日志、指标、测试、Sanitizer 和 CI 如何组成一个更接近真实服务的质量闭环。
