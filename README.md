# TinyKVCache

TinyKVCache 是一个基于 **C++17** 实现的轻量级 TCP 内存 KV 缓存服务，目标是帮助理解一个真实的后端服务是如何从 socket、事件循环、协议解析一路构建到可观测、可优雅退出的准生产形态。

它从最小可运行的阻塞式 server 起步，逐步演进为：

- 支持多客户端并发连接的 **Reactor 事件驱动模型**；
- 通过 **Poller 抽象**解耦事件循环与底层 IO 多路复用，Linux 使用 **epoll**，macOS 使用 **poll**；
- 支持**连接级读写缓冲、部分读写处理、背压保护与慢客户端治理**；
- 支持 **TTL 过期机制**（惰性删除 + 后台 sweeper 线程）；
- 支持**跨线程可唤醒的事件循环与优雅退出**（SIGINT/SIGTERM → Draining → 超时强关）；
- 内置**分级日志与运行指标**（连接、流量、背压、延迟直方图），STATS 命令实时查看。

项目配套提供客户端、压测脚本、烟雾测试、慢客户端测试与集成测试，适合作为 **Linux 网络编程 / Reactor 模型 / TCP 应用层协议设计 / C++ 工程化** 的学习项目。

---

## 项目特性

- 基于 C++17
- 非阻塞 socket + Reactor 事件循环
- Poller 抽象：`epoll`（Linux）/ `poll`（macOS）双后端，按平台自动选择
- 自定义二进制协议（4 字节长度字段 + Payload）
- 连接级读写缓冲区
- 正确处理 TCP 粘包 / 半包 / 部分读 / 部分写
- 连接背压：写缓冲高水位暂停读、低水位恢复、硬上限关闭慢客户端
- IO 公平性预算：限制单次事件循环的读写字节数与 accept 数量，防止饿死
- TTL 过期机制：惰性删除 + 后台 sweeper 线程周期清理
- 优雅退出：socketpair 唤醒事件循环，信号安全的 SIGINT/SIGTERM 处理，Draining 排空写缓冲后退出
- 可观测性：分级日志 + 原子指标统计 + 延迟直方图，STATS 命令暴露
- 支持多客户端并发连接
- 客户端 / 服务端分离
- 支持单元测试、集成测试、烟雾测试与慢客户端测试
- ASan / UBSan / TSan 质量门禁，警告视为错误（-Werror）

---

## 技术栈

- **语言**：C++17
- **网络编程**：Linux / macOS Socket API
- **IO 模型**：非阻塞 IO + Reactor 事件驱动
- **IO 多路复用**：`epoll`（Linux）/ `poll`（macOS），统一 Poller 接口抽象
- **并发控制**：`std::mutex`、`std::condition_variable`、`std::atomic`
- **资源管理**：RAII（ScopedFd 管理 fd 生命周期）
- **构建工具**：CMake（选项化构建：测试 / Sanitizer / 集成测试开关）
- **脚本**：Python（压测、烟雾测试、慢客户端测试、集成测试）

---

## 项目架构

整体采用 **单线程 Reactor + 后台 TTL 清理线程** 的结构：

```text
                  +----------------------+
                  |      TcpServer       |
                  |  (Reactor 事件循环)   |
                  +----------+-----------+
                             |
              +--------------+-----------------+
              |                                |
       listen fd 就绪                     client fd 就绪
              |                                |
        accept 新连接                    读取请求 / 发送响应
              |                                |
              v                                v
        Connection 队列 <----------> 核心处理链路：
                                     readBuffer
                                         |
                                     FrameCodec
                                         |
                                   CommandParser
                                         |
                                  CommandExecutor
                                         |
                              KVStore（mutex 线程安全）
                                         |
                                   writeBuffer
                                         |
                                  注册/更新可写事件

  辅助组件：
  +------------------+  +-----------------------+  +------------------+
  |   WakeupChannel  |  | TtlSweeper（后台线程） |  |  ServerMetrics   |
  | socketpair 唤醒   |  | 周期清理过期 key       |  |  原子指标+直方图  |
  +------------------+  +-----------------------+  +------------------+
```

核心模块：

| 模块 | 职责 |
| --- | --- |
| `TcpServer` | Reactor 事件循环、连接生命周期管理、背压与优雅退出状态机 |
| `Poller` / `PollPoller` / `EpollPoller` | IO 多路复用抽象与平台后端 |
| `Connection` | 单个连接状态（读写缓冲、背压标记、已注册事件） |
| `OutputBuffer` | 偏移式写缓冲，避免部分写后的 O(n) 内存移动 |
| `ScopedFd` | RAII 封装 fd，防止泄漏与 double close |
| `FrameCodec` | 应用层帧编解码（4 字节长度字段 + Payload） |
| `CommandParser` | 文本命令解析 |
| `CommandExecutor` | 命令执行与响应生成 |
| `KVStore` | 线程安全的内存 KV 存储，支持 TTL |
| `TtlSweeper` | 后台线程周期清理过期 key |
| `WakeupChannel` | 基于 socketpair 的跨线程 / 信号唤醒通道 |
| `TerminationSignalHandler` | 信号安全的 SIGINT/SIGTERM 处理（handler 内只 write） |
| `ServerMetrics` | 原子指标与延迟直方图 |
| `Logger` | 分级日志（Debug/Info/Warn/Error/Off） |

---

## 应用层协议

TinyKVCache 使用简单的二进制帧协议解决 TCP 粘包/半包问题：

```text
+-------------+----------------------+
| 4 字节长度   | Payload（文本命令）   |
+-------------+----------------------+
```

### 帧格式

- 前 4 字节：payload 长度
- 使用**大端序**（网络字节序）
- 后续 N 字节：文本命令内容
- 单条 Payload 最大长度：**1 MiB**（防止恶意长度字段导致内存暴涨）

### 示例

客户端发送：

```text
SET name tinykv
```

实际帧内容：

```text
00 00 00 0F 53 45 54 20 6E 61 6D 65 20 74 69 6E 79 6B 76
```

其中：

- `00 00 00 0F` 表示 payload 长度为 15
- 后面的字节是 `"SET name tinykv"` 的 ASCII 内容

---

## 支持的命令

| 命令 | 说明 | 示例 |
| --- | --- | --- |
| `PING` | 心跳检测 | `PING` |
| `SET key value` | 写入键值（覆盖写，并清除原 TTL） | `SET name tinykv` |
| `GET key` | 查询键 | `GET name` |
| `DEL key` | 删除键 | `DEL name` |
| `EXPIRE key seconds` | 设置过期时间 | `EXPIRE name 10` |
| `TTL key` | 查询剩余过期时间 | `TTL name` |
| `STATS` | 查看存储与服务端运行指标 | `STATS` |
| `QUIT` | 请求服务端关闭当前连接 | `QUIT` |

### 返回约定

- `+OK`：命令执行成功
- `+PONG`：PING 响应
- `+BYE`：QUIT 响应
- `$value`：GET / TTL 的返回内容
- `$nil`：key 不存在或已过期
- `-ERR ...`：协议错误、未知命令或内部错误

`TTL` 返回值约定与 Redis 对齐：

- `>= 0`：剩余秒数
- `-1`：key 存在但没有设置过期时间
- `-2`：key 不存在或已过期

`STATS` 返回存储统计（keys/persistent/expiring）与服务端运行指标（连接数、收发字节、命令数、错误数、背压切换次数、慢客户端关闭数、过期清理数、延迟分位数等）。

---

## 构建与依赖

### 依赖要求

- CMake >= 3.16
- 支持 C++17 的编译器
  - Linux: GCC / Clang
  - macOS: Clang
- Python 3（用于压测 / 烟雾测试 / 集成测试）

### Linux

```bash
sudo apt update
sudo apt install -y build-essential cmake python3
```

### macOS

```bash
brew install cmake python3
```

### 编译

Release 构建（推荐，含测试）：

```bash
./scripts/build_release.sh
```

或手动构建：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

构建产物：

```text
build/tinykv_server
build/tinykv_client
```

### 常用构建选项

| 选项 | 默认 | 说明 |
| --- | --- | --- |
| `TINYKV_BUILD_TESTS` | ON | 构建单元测试 |
| `TINYKV_BUILD_INTEGRATION_TESTS` | ON | 构建并运行端到端集成测试 |
| `TINYKV_WARNINGS_AS_ERRORS` | ON | 编译告警视为错误 |
| `TINYKV_ENABLE_ASAN` | OFF | 启用 AddressSanitizer |
| `TINYKV_ENABLE_UBSAN` | OFF | 启用 UndefinedBehaviorSanitizer |
| `TINYKV_ENABLE_TSAN` | OFF | 启用 ThreadSanitizer（需独立构建目录） |

示例（ASan+UBSan 构建）：

```bash
cmake -S . -B build-asan -DTINYKV_ENABLE_ASAN=ON -DTINYKV_ENABLE_UBSAN=ON
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure
```

---

## 运行服务端

### 方式一：直接运行二进制

```bash
./build-release/tinykv_server 0.0.0.0 7777
```

完整参数：

```bash
./tinykv_server <host> <port> [auto|poll|epoll] [debug|info|warn|error|off]
```

- 第三个参数：Poller 后端，`auto` 表示 Linux 用 epoll、macOS 用 poll（Linux 上可强制 `poll` 做对照）
- 第四个参数：日志等级，默认不记录每条命令以免影响性能

### 方式二：使用脚本启动

```bash
./scripts/run_server.sh
```

默认监听：

```text
0.0.0.0:7777
```

也可以显式指定：

```bash
./build-release/tinykv_server \
  0.0.0.0 \
  7777 \
  auto \
  info
```

### 优雅退出

- 按 `Ctrl+C`（SIGINT）或发送 SIGTERM，服务端进入 Draining 状态：停止 accept 与新请求读取，继续发送各连接写缓冲中的残留响应，超时（默认 5 秒）后强制关闭剩余连接并退出
- 后台 sweeper 线程会被正常唤醒并 join

---

## 使用自带客户端

启动客户端：

```bash
./build-release/tinykv_client 127.0.0.1 7777
```

示例：

```text
PING
+PONG
SET name tinykv
+OK
GET name
$tinykv
EXPIRE name 10
+OK
TTL name
$10
DEL name
+OK
GET name
$nil
QUIT
+BYE
```

---

## 使用 netcat 测试（快速体验）

如果只是快速体验，也可以直接通过 `nc` 发送一个简单帧。

下面这个例子向服务端发送 `GET name` 请求：

```bash
printf '\x00\x00\x00\x08GET name' | nc 127.0.0.1 7777
```

正常情况下会看到类似：

```text
$nil
```

---

## 使用 Python 手工发帧

```python
import socket
import struct

payload = b"PING"
frame = struct.pack("!I", len(payload)) + payload

with socket.create_connection(("127.0.0.1", 7777), timeout=3) as s:
    s.sendall(frame)

    header = s.recv(4)
    length = struct.unpack("!I", header)[0]

    body = b""
    while len(body) < length:
        chunk = s.recv(length - len(body))
        if not chunk:
            break
        body += chunk

    print(body.decode())
```

---

## 运行测试

构建后运行全部测试：

```bash
ctest \
  --test-dir build-quality \
  --output-on-failure
```

测试分层：

- **单元测试**：`test_frame_codec`、`test_command_parser`、`test_kv_store`、`test_command_executor`、`test_output_buffer`、`test_socket_util`、`test_scoped_fd`、`test_poller`、`test_wakeup_channel`、`test_server_metrics`
- **集成测试**：`tests/integration/test_server_e2e.py`（真实进程 + 真实 socket 全链路）、`tests/integration/test_graceful_shutdown.py`（SIGTERM 优雅退出验证）
- **烟雾测试**：

```bash
python3 scripts/smoke_test.py
```

- **慢客户端测试**（验证背压与硬上限保护）：

```bash
python3 scripts/slow_client_test.py
```

---

## 压测

项目提供了一个简单的 Python 压测脚本，用于快速观察服务端基础吞吐能力：

```bash
python3 scripts/benchmark.py --host 127.0.0.1 --port 7777 --connections 10 --requests 1000
```

压测脚本特点：

- 支持多 TCP 连接并发
- 每个连接串行发送多组 `PING / SET / GET`
- 统计总请求数、错误数、总耗时、QPS、平均延迟与 P50 / P95 / P99 / Max 分位数
- 支持 `--output-json <path>` 导出 JSON 报告

示例输出：

```text
benchmark result
----------------
total_requests : 30000
total_errors   : 0
elapsed_sec    : 2.730
qps            : 10989.01
avg_latency_ms : 0.273
latency_p50_ms  : 0.250
latency_p95_ms  : 0.410
latency_p99_ms  : 0.620
latency_max_ms  : 3.105
```

> 说明：该脚本只是开发阶段的轻量压测工具，适合验证协议、并发连接与服务端基本吞吐，不等价于工业级压测结论。

---

## 目录结构

```text
tiny-kv-cache/
├── app/
│   ├── server_main.cpp          # 服务端入口
│   └── client_main.cpp          # 客户端入口
├── include/
│   └── tinykv/
│       ├── core/
│       │   ├── command.h
│       │   ├── command_executor.h
│       │   ├── command_parser.h
│       │   ├── frame_codec.h
│       │   └── kv_store.h
│       ├── net/
│       │   ├── poll/
│       │   │   ├── poller.h            # Poller 抽象接口
│       │   │   ├── io_event.h          # 统一事件模型
│       │   │   ├── poll_poller.h       # poll 后端（macOS/Linux）
│       │   │   ├── epoll_poller.h      # epoll 后端（Linux）
│       │   │   └── poller_factory.h    # 按平台自动选择后端
│       │   ├── wakeup/
│       │   │   ├── wakeup_channel.h            # socketpair 唤醒通道
│       │   │   └── termination_signal_handler.h # 信号安全退出
│       │   ├── connection.h
│       │   ├── output_buffer.h         # 偏移式写缓冲
│       │   ├── scoped_fd.h             # RAII fd
│       │   ├── socket_util.h
│       │   └── tcp_server.h            # Reactor 事件循环与连接管理
│       └── observability/
│           ├── logger.h                # 分级日志
│           └── server_metrics.h        # 原子指标与延迟直方图
├── src/
│   ├── core/
│   ├── net/
│   └── observability/
├── tests/
│   ├── test_*.cpp                      # 单元测试
│   └── integration/                    # E2E / 优雅退出集成测试
├── scripts/
│   ├── benchmark.py                    # 压测脚本
│   ├── smoke_test.py                   # 烟雾测试
│   ├── slow_client_test.py             # 慢客户端背压测试
│   ├── build_release.sh                # Release 构建 + 测试
│   └── run_server.sh                   # 服务端启动脚本
├── docs/
│   ├── v1/                             # v1 各阶段设计与调试验收文档
│   └── v2/                             # v2 准生产化优化记录
├── CMakeLists.txt
└── README.md
```

---

## 实现细节

### 1. Socket 与资源管理

- 使用 `ScopedFd` 对 fd 做 RAII 封装：禁止拷贝、允许移动、析构自动 close，避免泄漏与 double close
- 监听 socket 与连接 socket 均设置为非阻塞

### 2. Poller 抽象与多路复用后端

- `Poller` 定义统一接口：`Add / Modify / Remove / Wait`，统一事件模型（Read/Write/Error/Hangup）
- `PollPoller`：每次 Wait 根据注册表构建 `pollfd` 数组，跨平台兜底
- `EpollPoller`：Linux 专用，基于 `epoll_create1 / epoll_ctl / epoll_wait`，当前使用 LT（水平触发）模式
- 后端由工厂按平台自动选择；`Connection` 缓存 `registeredEvents`，仅在兴趣事件变化时才调用 `Modify`，避免重复系统调用

### 3. Reactor 事件循环

- `TcpServer` 运行主事件循环，只依赖 `Poller` 接口，不直接依赖具体系统调用
- listen fd 可读 → accept 新连接（单次循环最多 accept 64 个，保证公平性）
- client fd 可读 → 非阻塞读取（单次最多 16 KiB）→ 写满背压则跳过读取
- client fd 可写 → 非阻塞发送（单次最多 64 KiB）→ 缓冲排空则取消可写监听

### 4. 连接级缓冲区与背压

- 每个连接持有独立 `readBuffer` 与 `writeBuffer`
- `OutputBuffer` 采用「读偏移 + 达到阈值再 compact」设计，避免部分写后 `erase(0, n)` 的 O(n) 内存移动
- 背压三档水位：
  - 写缓冲 ≥ **1 MiB**（高水位）：暂停监听该连接的可读事件，利用 TCP 滑动窗口反向施压
  - 写缓冲 ≤ **512 KiB**（低水位）：恢复读取
  - 写缓冲 ≥ **4 MiB**（硬上限）：判定为慢客户端，主动关闭，保护服务端内存
- 背压状态切换计入 `ServerMetrics`

### 5. 应用层协议解析

- `FrameCodec` 按「4 字节长度 + Payload」解码，支持一次 TCP 读包含多个帧、一个帧被拆成多次到达
- 长度字段超过 1 MiB 直接判定协议错误并关闭连接
- `CommandParser` 将 Payload 解析为命令对象，`CommandExecutor` 执行并生成响应

### 6. KVStore 与 TTL

- `KVStore` 基于 `std::unordered_map`，内部以 `std::mutex` 保证线程安全（Reactor 主线程与 sweeper 线程并发访问）
- TTL 采用 **惰性删除 + 后台清理** 结合：
  - `GET / TTL / DEL / EXPIRE / STATS` 时顺带判断是否过期并删除
  - 后台 `TtlSweeper` 线程周期性扫描清理，避免冷数据长期占用内存
- 过期时间使用 `std::chrono::steady_clock`（单调时钟），不受系统时间回拨影响
- 约定：`SET` 覆盖写会清除原有过期时间
- sweeper 使用条件变量等待，退出时立即唤醒并 join，不拖延关闭流程

### 7. 优雅退出

- 服务端状态机：`Created → Running → Draining → Stopped`
- `WakeupChannel` 基于 `socketpair` 创建内部通知通道：读端注册到 Poller，写端用于跨线程 / 信号唤醒，让阻塞在 `Wait` 的事件循环立即返回
- `TerminationSignalHandler` 信号处理器内只调用 `write`（async-signal-safe），不做日志、不加锁
- 收到 SIGINT/SIGTERM 后进入 Draining：停止 accept、停止读新请求、继续发送写缓冲残留响应，超过 deadline（默认 5s）强制关闭剩余连接
- 退出前正常唤醒并 join sweeper 线程

### 8. 可观测性

- `Logger`：Debug/Info/Warn/Error/Off 五级，默认不在热路径记录每条命令
- `ServerMetrics`：全原子计数，覆盖连接数、收发字节、帧数、命令数、错误数、慢客户端关闭数、背压切换次数、sweeper 运行次数、强制关闭数、最大待写字节、命令延迟直方图（7 档）
- `STATS` 命令实时返回存储统计与服务端指标，含延迟分位数估算

---

## 当前限制与后续优化

当前版本仍然存在一些明确的限制：

- 单线程 Reactor，未利用多核（可演进为 one loop per thread 多 Reactor）
- 无持久化（AOF / RDB）
- 无认证与访问控制
- 无 TLS
- TTL 清理为全量扫描，过期 key 很多时有优化空间（如时间轮）
- 指标仅通过 STATS 暴露，未对接外部监控系统（如 Prometheus exporter）
- 无连接空闲超时
- 配置文件支持缺失

这些限制也适合作为后续继续迭代的方向，例如：

- kqueue 后端（macOS）
- 定时器 / 时间轮优化 TTL 扫描
- 接入线程池处理重命令
- Prometheus 指标导出
- 配置文件与命令行参数完善
- CI 中固化 benchmark 结果

---

## 项目意义

这个项目并不追求替代 Redis，而是把以下几个核心问题拆开实现一遍：

1. TCP 服务端如何处理并发连接
2. Reactor 事件驱动模型如何落地，poll 与 epoll 如何抽象切换
3. 应用层协议如何解决粘包 / 半包
4. 部分读 / 部分写场景下缓冲区如何设计，背压如何保护服务端
5. fd 等系统资源如何用 RAII 管理
6. 内存 KV 的过期机制如何设计，多线程下如何保证安全
7. 服务端如何做到信号安全的优雅退出
8. 日志与指标如何嵌入热路径而不显著影响性能
9. 一个 C++ 项目如何通过测试与 Sanitizer 构建质量门禁

如果把这条链路真正走完，会对后端服务的底层机制有更扎实的理解。
