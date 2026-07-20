# TinyKVCache

TinyKVCache 是一个使用 **C++17** 从零实现的轻量级 TCP 内存 KV 缓存服务。

项目最初用于系统学习 C++ 网络编程，第一阶段完成了自定义协议、命令解析、内存 KV、TTL、RAII、非阻塞 IO、`poll` Reactor、多客户端连接管理、测试、部署和压测。第二阶段在此基础上进行了完整的准生产化改造，重点解决工程质量、慢客户端、资源边界、事件后端抽象、优雅退出、日志、运行指标和性能验证等问题。

当前版本不仅是一个可以运行的网络 Demo，也是一套结构完整、可测试、可部署、可观测、可继续演进的 C++ 网络服务工程。

---

## 1. 项目目标

TinyKVCache 主要用于训练和展示以下能力：

- C++17 工程组织与 CMake 构建；
- POSIX TCP socket 编程；
- TCP 粘包、半包和消息边界处理；
- 非阻塞 IO 与单线程 Reactor；
- `poll` / `epoll` 多路复用；
- RAII 与文件描述符所有权管理；
- 连接级读写缓冲区；
- 部分写、慢客户端保护和背压；
- TTL 与后台过期清理；
- 跨线程停止通知与优雅退出；
- Sanitizer、CI 和端到端测试；
- 日志、运行指标和延迟统计；
- macOS 开发与 Linux 部署。

---

## 2. 核心能力

### 2.1 网络与协议

- 使用 TCP 长连接；
- 自定义应用层协议：`4 字节大端长度字段 + payload`；
- 每个连接维护独立 `readBuffer`；
- 正确处理半包、粘包和连续多帧；
- 单帧大小限制，防止异常长度字段造成内存压力；
- 非阻塞 `recv()` / `send()`；
- 正确处理 `EINTR`、`EAGAIN`、`EWOULDBLOCK`、连接关闭和错误事件。

### 2.2 Reactor

- 单线程 Reactor 管理监听 fd、客户端 fd 和内部唤醒 fd；
- `Poller` 抽象统一事件接口；
- macOS 默认使用 `PollPoller`；
- Linux 默认使用 `EpollPoller`；
- Linux 可强制切换回 `poll` 进行性能对照；
- 当前 `epoll` 使用 level-triggered 模式；
- 连接事件根据运行状态动态切换：
  - `Read`
  - `Read | Write`
  - `Write`
  - 关闭并移除

### 2.3 内存 KV 与 TTL

支持以下命令：

| 命令 | 示例 | 说明 |
|---|---|---|
| `PING` | `PING` | 服务存活检查 |
| `SET` | `SET name xiaofeng` | 写入或覆盖 key |
| `GET` | `GET name` | 读取 value |
| `DEL` | `DEL name` | 删除 key |
| `EXPIRE` | `EXPIRE name 10` | 设置 TTL |
| `TTL` | `TTL name` | 查询剩余 TTL |
| `STATS` | `STATS` | 查询存储和服务指标 |
| `QUIT` | `QUIT` | 返回响应后关闭当前连接 |

TTL 返回值约定：

```text
-2：key 不存在或已经过期
-1：key 存在，但没有设置 TTL
>=0：剩余过期秒数
```

过期清理采用：

```text
访问时惰性删除 + 后台 sweeper 周期清理
```

后台 sweeper 使用：

- `std::thread`
- `std::mutex`
- `std::condition_variable`
- 原子运行标志

服务停止时会主动唤醒并 `join()` 后台线程。

### 2.4 连接资源治理

第二阶段增加了明确的连接资源边界：

- `OutputBuffer` 使用读偏移，避免每次部分写后执行 `erase(0, n)`；
- 输出缓冲区达到高水位时暂停该连接的读事件；
- 输出缓冲区下降到低水位时恢复读事件；
- 超过硬上限时关闭慢客户端；
- 限制单次读事件的最大读取量；
- 限制单次写事件的最大发送量；
- 限制单次监听事件的最大 `accept()` 数量；
- 避免单个繁忙连接长期占用 Reactor；
- 避免慢客户端造成服务端内存无限增长。

默认限制可通过 `TcpServerOptions` 调整，典型配置为：

```text
最大读缓冲区：约 1 MiB
写低水位：512 KiB
写高水位：1 MiB
写硬上限：4 MiB
单次写预算：64 KiB
单次 accept 上限：64
```

---

## 3. 第二阶段优化内容

### Day 1：工程质量门禁

- 完整编译警告；
- Warnings-as-Errors；
- AddressSanitizer；
- UndefinedBehaviorSanitizer；
- ThreadSanitizer；
- Debug / Release / RelWithDebInfo 构建；
- 真实 TCP 端到端测试；
- 多客户端、半包、粘包和 QUIT 语义测试；
- macOS 与 Linux CI。

### Day 2：背压与事件公平性

- `OutputBuffer` 读偏移；
- 延迟 compact，减少内存搬移；
- 高低水位背压；
- 慢客户端硬上限；
- 单次读写预算；
- 单次 accept 预算；
- 慢客户端隔离测试。

### Day 3：Poller 抽象

- 统一 `IoEvent`；
- `Poller::Add / Modify / Remove / Wait`；
- 跨平台 `PollPoller`；
- Linux `EpollPoller`；
- 自动后端选择；
- 公共 Poller 契约测试；
- `TcpServer` 与具体系统调用解耦。

### Day 4：优雅退出

- 基于 `socketpair()` 的 `WakeupChannel`；
- 跨线程立即唤醒事件循环；
- `SIGINT` / `SIGTERM`；
- 信号处理函数只执行 async-signal-safe 的 `write()`；
- 忽略 `SIGPIPE`；
- 生命周期状态机：
  - `Created`
  - `Running`
  - `Draining`
  - `Stopped`
- 停止 accept；
- 停止读取新请求；
- 尽量排空已经生成的响应；
- 退出超时后强制关闭慢连接；
- 正常停止和回收 sweeper。

### Day 5：可观测性

- 线程安全分级日志；
- `Debug / Info / Warn / Error / Off`；
- 连接数和流量指标；
- 协议错误与命令错误；
- 背压切换次数；
- 慢客户端断开次数；
- TTL 清理指标；
- 强制退出连接数量；
- 命令处理平均和最大延迟；
- 延迟直方图；
- STATS 扩展；
- benchmark P50 / P95 / P99；
- JSON 性能报告。

---

## 4. 总体架构

```text
                           ┌─────────────────────┐
                           │   tinykv_client     │
                           └──────────┬──────────┘
                                      │
                     4-byte length + payload
                                      │
                           ┌──────────▼──────────┐
                           │   tinykv_server     │
                           └──────────┬──────────┘
                                      │
                           ┌──────────▼──────────┐
                           │     TcpServer       │
                           │  Reactor / 生命周期 │
                           └──────────┬──────────┘
                                      │
             ┌────────────────────────┼─────────────────────────┐
             │                        │                         │
   ┌─────────▼─────────┐    ┌─────────▼─────────┐     ┌────────▼────────┐
   │      Poller       │    │    Connection     │     │ WakeupChannel   │
   │ poll / epoll      │    │ read/write buffer │     │ Stop / Signal   │
   └─────────┬─────────┘    │ backpressure      │     └─────────────────┘
             │              └─────────┬─────────┘
             │                        │
             │              ┌─────────▼─────────┐
             │              │    FrameCodec     │
             │              └─────────┬─────────┘
             │                        │
             │              ┌─────────▼─────────┐
             │              │ CommandParser     │
             │              └─────────┬─────────┘
             │                        │
             │              ┌─────────▼─────────┐
             │              │ CommandExecutor   │
             │              └─────────┬─────────┘
             │                        │
             │              ┌─────────▼─────────┐
             │              │      KVStore      │
             │              │ TTL / Sweeper     │
             │              └───────────────────┘
             │
   ┌─────────▼─────────┐
   │ Logger / Metrics  │
   └───────────────────┘
```

---

## 5. Reactor 请求流程

以 `PING` 为例：

```text
客户端发送 PING frame
        ↓
内核接收缓冲区变为可读
        ↓
Poller 返回 Read 事件
        ↓
TcpServer::HandleClientRead
        ↓
recv()
        ↓
FrameCodec::Decode
        ↓
CommandParser
        ↓
CommandExecutor
        ↓
生成 +PONG
        ↓
编码并追加到 Connection::writeBuffer
        ↓
Poller::Modify(Read | Write)
        ↓
Poller 返回 Write 事件
        ↓
TcpServer::HandleClientWrite
        ↓
send()
        ↓
OutputBuffer::Consume
        ↓
writeBuffer 清空
        ↓
Poller::Modify(Read)
```

Reactor 接收到的是 **IO 就绪通知**，不是 IO 完成通知：

- `Read`：当前调用 `recv()` 很可能取得进展；
- `Write`：当前调用 `send()` 很可能取得进展；
- 实际读写仍由应用程序执行；
- 一次 `recv()` 不保证得到完整消息；
- 一次 `send()` 不保证写完完整响应。

---

## 6. 项目目录

```text
tiny-kv-cache/
├── CMakeLists.txt
├── README.md
├── app/
│   ├── client_main.cpp
│   └── server_main.cpp
├── cmake/
│   ├── compiler_warnings.cmake
│   └── sanitizers.cmake
├── include/
│   └── tinykv/
│       ├── core/
│       │   ├── command.h
│       │   ├── command_executor.h
│       │   ├── frame_codec.h
│       │   └── kv_store.h
│       ├── net/
│       │   ├── output_buffer.h
│       │   ├── scoped_fd.h
│       │   ├── socket_util.h
│       │   ├── tcp_server.h
│       │   ├── termination_signal_handler.h
│       │   ├── wakeup_channel.h
│       │   └── poll/
│       │       ├── io_event.h
│       │       ├── poller.h
│       │       ├── poller_factory.h
│       │       ├── poll_poller.h
│       │       └── epoll_poller.h
│       └── observability/
│           ├── logger.h
│           └── server_metrics.h
├── src/
│   ├── core/
│   ├── net/
│   │   └── poll/
│   └── observability/
├── tests/
│   ├── integration/
│   ├── test_command.cpp
│   ├── test_command_executor.cpp
│   ├── test_frame_codec.cpp
│   ├── test_kv_store.cpp
│   ├── test_output_buffer.cpp
│   ├── test_poller.cpp
│   ├── test_scoped_fd.cpp
│   ├── test_server_metrics.cpp
│   ├── test_socket_util.cpp
│   └── test_wakeup_channel.cpp
├── scripts/
│   ├── benchmark.py
│   ├── build_release.sh
│   ├── deploy_linux.sh
│   ├── deploy_linux_tar.sh
│   ├── run_server.sh
│   ├── slow_client_test.py
│   └── smoke_test.py
└── docs/
    ├── optimization/
    └── stage*.md
```

实际文件名以当前仓库为准。

---

## 7. 环境要求

### macOS

```bash
brew install cmake rsync
```

使用系统 AppleClang 编译。

### Ubuntu / Debian

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  clang \
  rsync \
  python3
```

### CentOS / Rocky / AlmaLinux

```bash
sudo dnf install -y \
  gcc \
  gcc-c++ \
  clang \
  cmake \
  rsync \
  python3
```

---

## 8. 构建

### 8.1 Debug + 质量门禁

```bash
cmake -S . -B build-quality \
  -DCMAKE_BUILD_TYPE=Debug \
  -DTINYKV_BUILD_TESTS=ON \
  -DTINYKV_BUILD_INTEGRATION_TESTS=ON \
  -DTINYKV_WARNINGS_AS_ERRORS=ON

cmake --build build-quality -j

ctest \
  --test-dir build-quality \
  --output-on-failure
```

### 8.2 Release

```bash
./scripts/build_release.sh
```

### 8.3 ASan + UBSan

```bash
cmake -S . -B build-asan \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DTINYKV_BUILD_TESTS=ON \
  -DTINYKV_BUILD_INTEGRATION_TESTS=ON \
  -DTINYKV_WARNINGS_AS_ERRORS=ON \
  -DTINYKV_ENABLE_ASAN=ON \
  -DTINYKV_ENABLE_UBSAN=ON

cmake --build build-asan -j

ASAN_OPTIONS=halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest \
  --test-dir build-asan \
  --output-on-failure
```

### 8.4 TSan

```bash
cmake -S . -B build-tsan \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DTINYKV_BUILD_TESTS=ON \
  -DTINYKV_BUILD_INTEGRATION_TESTS=ON \
  -DTINYKV_WARNINGS_AS_ERRORS=ON \
  -DTINYKV_ENABLE_TSAN=ON

cmake --build build-tsan -j

TSAN_OPTIONS=halt_on_error=1 \
ctest \
  --test-dir build-tsan \
  --output-on-failure
```

TSan 建议优先在 Linux 环境运行。

---

## 9. 运行

### 9.1 服务端

```bash
./build-quality/tinykv_server \
  127.0.0.1 \
  7777 \
  auto \
  info
```

参数格式：

```text
tinykv_server <host> <port> [auto|poll|epoll] [debug|info|warn|error|off]
```

后端选择：

```text
auto：
  macOS -> poll
  Linux -> epoll

poll：
  强制使用 poll

epoll：
  强制使用 epoll，仅 Linux 支持
```

允许远程访问：

```bash
./build-release/tinykv_server \
  0.0.0.0 \
  7777 \
  auto \
  info
```

### 9.2 客户端

```bash
./build-quality/tinykv_client \
  127.0.0.1 \
  7777
```

示例：

```text
> PING
+PONG

> SET name xiaofeng
+OK

> GET name
$xiaofeng

> EXPIRE name 10
+OK

> TTL name
$10

> QUIT
+BYE
```

---

## 10. 响应格式

```text
+xxx       普通成功响应
$xxx       字符串或数值响应
$nil       空结果
-ERR xxx   错误响应
```

---

## 11. STATS

`STATS` 会返回 KVStore 和服务运行指标，典型字段包括：

```text
keys
persistent
expiring
connections_active
connections_accepted
connections_closed
bytes_received
bytes_sent
frames_received
commands
command_errors
protocol_errors
slow_clients
read_pauses
read_resumes
expired_removed
max_pending_write_bytes
latency_avg_us
latency_max_us
latency_p95_upper_us
latency_p99_upper_us
```

服务端 P95 / P99 来自固定延迟直方图，表示对应桶的上界，不是保存全部样本后计算的精确百分位。

---

## 12. 测试

运行全部测试：

```bash
ctest \
  --test-dir build-quality \
  --output-on-failure
```

只运行单元测试：

```bash
ctest \
  --test-dir build-quality \
  -L unit \
  --output-on-failure
```

只运行集成测试：

```bash
ctest \
  --test-dir build-quality \
  -L integration \
  --output-on-failure
```

测试覆盖：

- FrameCodec 编解码；
- 半包和粘包；
- 命令解析；
- 命令执行；
- KVStore 与 TTL；
- ScopedFd 移动语义；
- SocketUtil；
- OutputBuffer；
- Poller 公共契约；
- poll / epoll；
- WakeupChannel；
- SIGTERM 唤醒；
- ServerMetrics；
- 多客户端共享 KVStore；
- QUIT 只关闭当前连接；
- 优雅退出。

---

## 13. 冒烟测试

```bash
python3 scripts/smoke_test.py \
  --host 127.0.0.1 \
  --port 7777
```

---

## 14. 慢客户端隔离测试

```bash
python3 scripts/slow_client_test.py \
  --host 127.0.0.1 \
  --port 7777
```

该测试会创建一个只发送请求但不读取响应的慢客户端，同时使用正常客户端发送 `PING`，验证：

- 正常客户端仍然可以及时收到响应；
- 慢客户端不会无限扩大服务端写缓冲区；
- 达到硬上限后服务端可以关闭慢连接。

---

## 15. 压测

```bash
python3 scripts/benchmark.py \
  --host 127.0.0.1 \
  --port 7777 \
  --connections 100 \
  --requests 200 \
  --value-size 32 \
  --output-json benchmark_result.json
```

压测输出包括：

```text
total_requests
total_errors
elapsed_seconds
qps
latency_average_ms
latency_p50_ms
latency_p95_ms
latency_p99_ms
latency_max_ms
```

建议使用 Release 构建，并将日志等级设置为 `warn` 或 `error`。

---

## 16. 优雅退出

支持：

```text
Ctrl+C
SIGINT
SIGTERM
TcpServer::Stop()
```

退出过程：

```text
收到停止请求
    ↓
WakeupChannel 唤醒 Poller
    ↓
进入 Draining
    ↓
停止接受新连接
    ↓
停止读取新请求
    ↓
继续发送已经排队的响应
    ↓
连接全部排空后退出
    ↓
或到达超时后强制关闭
    ↓
停止并 join sweeper
    ↓
释放 Poller、socket 和连接
```

信号处理器不会直接操作 `TcpServer`，只通过非阻塞 `write()` 通知事件循环。

---

## 17. Linux 部署

### rsync

```bash
./scripts/deploy_linux.sh \
  user@server_ip \
  22 \
  ~/tinykv-cache
```

### tar + ssh

```bash
./scripts/deploy_linux_tar.sh \
  user@server_ip \
  22 \
  ~/tinykv-cache
```

服务器启动：

```bash
cd ~/tinykv-cache

./scripts/run_server.sh \
  0.0.0.0 \
  7777
```

---

## 18. 日志

支持以下等级：

```text
debug
info
warn
error
off
```

建议：

```text
开发调试：debug
普通运行：info
性能测试：warn / error
```

日志覆盖：

- 服务启动和停止；
- Poller 后端；
- 连接建立和断开；
- 协议错误；
- 慢客户端；
- 背压暂停和恢复；
- TTL 清理；
- 强制退出。

信号处理函数中禁止使用 Logger。

---

## 19. 工程亮点

### 19.1 应用层协议

TCP 是字节流，不保存消息边界。项目使用 4 字节长度字段协议和连接级 `readBuffer`，处理半包、粘包和多帧连续到达。

### 19.2 RAII

`ScopedFd`：

- 析构自动 `close()`；
- 禁止拷贝；
- 支持移动；
- 支持 `Release()` 和 `Reset()`；
- 避免 fd 泄漏和 double close。

### 19.3 部分写

非阻塞 `send()` 不保证写完全部响应。未写完数据保存在 `OutputBuffer`，后续 Write 事件继续发送。

### 19.4 背压

响应积压达到高水位后暂停读取，降到低水位后恢复。超过硬上限时关闭慢客户端。

### 19.5 Poller 解耦

`TcpServer` 不直接依赖 `pollfd` 或 `epoll_event`，只依赖统一 `Poller` 接口。

### 19.6 生命周期

通过 WakeupChannel 将跨线程停止和信号转换为普通 IO 事件，所有连接和 Poller 状态仍由 Reactor 线程统一修改。

### 19.7 可观测性

日志回答“发生了什么”，指标回答“发生了多少次以及当前状态如何”。

### 19.8 工程质量

- 零警告构建；
- Werror；
- ASan；
- UBSan；
- TSan；
- 单元测试；
- 集成测试；
- CI；
- Release 压测。

---

## 20. 已解决的典型问题

- Mac 和 Linux 间接 include 差异；
- `std::memcpy` 缺失 `<cstring>`；
- pthread 链接缺失；
- CMake 非法宏参数；
- `assert` 在 RelWithDebInfo 中被 `NDEBUG` 移除；
- 移动构造和 deleted copy constructor；
- `read()` 写入 const 缓冲区；
- `POLLOUT` 兴趣未更新；
- Read/Write 事件使用 `else if` 导致写事件丢失；
- 输出缓冲区频繁 `erase()`；
- 慢客户端内存增长；
- Reactor 单连接饥饿；
- Poller 等待无法及时停止；
- 信号处理安全；
- SIGPIPE；
- sweeper 线程生命周期。

---

## 21. 当前限制

- 单线程 Reactor；
- 业务命令在 Reactor 线程中执行；
- 无磁盘持久化；
- 无主从复制；
- 无认证和 TLS；
- TTL sweeper 仍然是全量扫描；
- 没有连接空闲超时；
- 没有外部 Prometheus / HTTP 指标接口；
- 没有 kqueue 后端；
- 没有线程池和任务队列；
- 没有正式 Redis RESP 兼容。

---

## 22. 后续演进方向

1. macOS `kqueue` 后端；
2. 空闲连接超时和定时器；
3. 时间轮或最小堆 TTL；
4. Reactor + worker 线程池；
5. 跨线程任务队列和 wakeup；
6. 配置文件；
7. Prometheus 指标导出；
8. 结构化 JSON 日志；
9. AOF 持久化；
10. Redis RESP 子集；
11. TLS；
12. fuzz testing；
13. benchmark 回归门禁；
14. 容器化与 systemd 部署。

---

## 23. 面试说明

可以将项目概括为：

> TinyKVCache 是一个使用 C++17 实现的轻量级 TCP 内存 KV 服务。服务端采用非阻塞 socket 和单线程 Reactor，抽象了 poll/epoll 后端；使用长度字段协议处理 TCP 半包和粘包；为每个连接维护读写缓冲区，并通过高低水位和硬上限实现慢客户端背压。项目还实现了 TTL、后台过期清理、WakeupChannel、SIGINT/SIGTERM 优雅退出、RAII fd 管理、日志、运行指标、Sanitizer、端到端测试、Linux 部署和性能压测。

---

## 24. License

本项目主要用于 C++ 网络编程学习、工程实践和面试展示。可根据个人学习和项目演示需求进行修改与扩展。
