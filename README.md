# TinyKVCache

TinyKVCache 是一个使用 **C++17** 从零实现的轻量级 TCP 内存 KV 缓存服务。项目以 C++ 后端面试和网络编程训练为目标，覆盖了从应用层协议设计、命令解析、内存存储、RAII 资源管理，到非阻塞 socket、`poll` Reactor、多客户端连接管理、TTL 后台清理、Linux 部署和简单压测的完整链路。

本项目不是简单的 Echo Server，而是一个具备完整请求-响应协议、业务命令、连接状态管理和工程化脚本的小型 C++ 网络服务。

---

## 1. 项目特性

- 基于 C++17 实现，使用 CMake 构建；
- 支持 macOS 本地开发和 Linux 服务器部署；
- 使用自定义应用层协议：`4 字节大端长度字段 + payload`；
- 解决 TCP 粘包、半包、多包连续到达问题；
- 实现 `PING / SET / GET / DEL / EXPIRE / TTL / STATS / QUIT` 命令；
- 使用 `std::unordered_map` 实现内存 KV 存储；
- 支持 TTL 过期机制；
- 采用 lazy delete + 后台 sweeper 线程清理过期 key；
- 使用 `ScopedFd` 通过 RAII 管理 Unix 文件描述符；
- 基于非阻塞 socket + `poll()` 实现单线程 Reactor；
- 每个连接维护独立 `read_buffer` 和 `write_buffer`；
- 支持多个客户端同时连接；
- 提供单元测试、冒烟测试、压测脚本和 Linux 部署脚本。

---

## 2. 技术栈

| 类别 | 技术 |
|---|---|
| 编程语言 | C++17 |
| 构建系统 | CMake |
| 网络模型 | TCP、非阻塞 socket、poll Reactor |
| 系统 API | POSIX socket、fcntl、poll、pthread |
| 存储结构 | `std::unordered_map` |
| 并发机制 | `std::thread`、`std::mutex`、`std::condition_variable` |
| 测试 | ctest、自定义 assert 测试 |
| 部署 | shell、rsync / tar + ssh |
| 压测 | Python socket 多连接脚本 |

---

## 3. 项目架构

```text
tinykv_client
    |
    |  TCP frame: 4-byte length + payload
    v
tinykv_server
    |
    v
TcpServer
    |
    |-- SocketUtil        创建 socket、bind、listen、connect、设置非阻塞
    |-- ScopedFd          RAII 管理 fd 生命周期
    |-- FrameCodec        应用层协议编解码，处理粘包和半包
    |-- CommandParser     将 payload 解析成结构化命令
    |-- CommandExecutor   执行业务命令并生成响应
    |-- KVStore           内存 key-value 存储与 TTL 管理
```

服务端主流程：

```text
poll()
  |
  |-- listen fd 可读
  |       |
  |       v
  |     accept 新连接
  |
  |-- client fd 可读
  |       |
  |       v
  |     recv -> read_buffer -> FrameCodec::decode
  |       |
  |       v
  |     parse_command -> CommandExecutor -> KVStore
  |       |
  |       v
  |     response -> FrameCodec::encode -> write_buffer
  |
  |-- client fd 可写
          |
          v
        flush write_buffer
```

---

## 4. 目录结构

```text
tiny-kv-cache/
├── CMakeLists.txt
├── README.md
├── app/
│   ├── client_main.cpp
│   └── server_main.cpp
├── include/
│   └── tinykv/
│       ├── core/
│       │   ├── Command.h
│       │   ├── CommandExecutor.h
│       │   ├── FrameCodec.h
│       │   └── KVStore.h
│       └── net/
│           ├── ScopedFd.h
│           ├── SocketUtil.h
│           └── TcpServer.h
├── src/
│   ├── core/
│   │   ├── Command.cpp
│   │   ├── CommandExecutor.cpp
│   │   ├── FrameCodec.cpp
│   │   └── KVStore.cpp
│   └── net/
│       ├── ScopedFd.cpp
│       ├── SocketUtil.cpp
│       └── TcpServer.cpp
├── tests/
│   ├── test_command.cpp
│   ├── test_command_executor.cpp
│   ├── test_frame_codec.cpp
│   ├── test_kv_store.cpp
│   ├── test_scoped_fd.cpp
│   ├── test_socket_util.cpp
│   └── test_smoke.cpp
├── scripts/
│   ├── build_release.sh
│   ├── run_server.sh
│   ├── deploy_linux.sh
│   ├── deploy_linux_tar.sh
│   ├── smoke_test.py
│   └── benchmark.py
└── docs/
    ├── v1/
    │   ├── stage1_frame_codec.md
    │   ├── stage2_command_parser.md
    │   ├── stage3_kv_store.md
    │   ├── stage4_scoped_fd.md
    │   ├── stage5_socket_util.md
    │   ├── stage6_client.md
    │   ├── stage7_blocking_server.md
    │   ├── stage8_tcp_server.md
    │   ├── stage9_poll_reactor.md
    |   ├── stage10_sweep_thread.md
    │   └── stage11_scripts.md
    └── v2/
        └── stage1_quality_gate.md
```

---

## 5. 快速开始

### 5.1 环境要求

macOS 或 Linux 环境下需要：

- C++17 编译器：`clang++` 或 `g++`
- CMake 3.14+
- Python 3，用于冒烟测试和压测脚本
- Linux 部署时建议安装 `rsync`

macOS 可以使用：

```bash
brew install cmake rsync
```

Ubuntu / Debian 可以使用：

```bash
sudo apt update
sudo apt install -y build-essential cmake rsync python3
```

CentOS / Rocky / AlmaLinux 可以使用：

```bash
sudo yum install -y gcc gcc-c++ cmake rsync python3
```

或者：

```bash
sudo dnf install -y gcc gcc-c++ cmake rsync python3
```

---

### 5.2 Debug 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DTINYKV_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

构建完成后会生成：

```text
build/tinykv_server
build/tinykv_client
```

---

### 5.3 Release 构建

推荐使用项目脚本：

```bash
./scripts/build_release.sh
```

构建产物位于：

```text
build-release/tinykv_server
build-release/tinykv_client
```

---

## 6. 本地运行

### 6.1 启动服务端

Debug 版本：

```bash
./build/tinykv_server 127.0.0.1 7777
```

Release 版本：

```bash
./build-release/tinykv_server 127.0.0.1 7777
```

或者使用脚本：

```bash
./scripts/run_server.sh 127.0.0.1 7777
```

如果希望允许远程机器连接，需要监听：

```bash
./scripts/run_server.sh 0.0.0.0 7777
```

---

### 6.2 启动客户端

```bash
./build/tinykv_client 127.0.0.1 7777
```

示例交互：

```text
connected to 127.0.0.1:7777
> PING
+PONG
> SET name xiaofeng
+OK
> GET name
$xiaofeng
> STATS
+keys=1,persistent=1,expiring=0
> QUIT
+BYE
```

---

## 7. 协议设计

TinyKVCache 使用自定义二进制帧协议：

```text
4 字节 payload 长度 + payload 内容
```

其中：

```text
前 4 字节：uint32_t，网络字节序，大端序
后 N 字节：payload 原始内容
```

例如 payload 为：

```text
PING
```

编码后为：

```text
00 00 00 04 50 49 4E 47
```

该协议可以处理：

- 半包：一次 `recv()` 只收到一部分消息；
- 粘包：一次 `recv()` 收到多条消息；
- 多包连续到达；
- payload 中包含空格或其他普通字符。

服务端为每个连接维护独立的 `read_buffer`。每次收到数据后，先追加到 `read_buffer`，再调用 `FrameCodec::decode()` 尽可能解析出完整 payload。

---

## 8. 命令说明

| 命令 | 示例 | 响应 | 说明 |
|---|---|---|---|
| `PING` | `PING` | `+PONG` | 心跳命令 |
| `SET` | `SET name xiaofeng` | `+OK` | 设置 key-value |
| `GET` | `GET name` | `$xiaofeng` / `$nil` | 获取 value |
| `DEL` | `DEL name` | `+OK` / `$nil` | 删除 key |
| `EXPIRE` | `EXPIRE name 10` | `+OK` / `$nil` | 设置过期时间 |
| `TTL` | `TTL name` | `$10` / `$-1` / `$-2` | 查看剩余过期时间 |
| `STATS` | `STATS` | `+keys=1,persistent=1,expiring=0` | 查看统计信息 |
| `QUIT` | `QUIT` | `+BYE` | 关闭当前连接 |

### TTL 返回语义

```text
-2：key 不存在，或者 key 已经过期
-1：key 存在，但没有设置过期时间
>=0：key 剩余过期秒数
```

### SET 与 TTL

本项目约定：

```text
SET 会覆盖旧 value，并清除原有 TTL。
```

例如：

```text
SET name xiaofeng
EXPIRE name 10
SET name han
TTL name
```

此时返回：

```text
$-1
```

表示 key 存在，但没有过期时间。

---

## 9. 响应格式

TinyKVCache 使用简单文本响应格式：

```text
+xxx       普通成功响应
$xxx       字符串或数值响应
$nil       空结果
-ERR xxx   错误响应
```

示例：

```text
PING            -> +PONG
SET a 1         -> +OK
GET a           -> $1
GET missing     -> $nil
UNKNOWN         -> -ERR unknown command
STATS           -> +keys=1,persistent=1,expiring=0
```

---

## 10. 测试

运行全部测试：

```bash
ctest --test-dir build --output-on-failure
```

单独运行测试：

```bash
./build/test_frame_codec
./build/test_command
./build/test_command_executor
./build/test_kv_store
./build/test_scoped_fd
./build/test_socket_util
```

测试覆盖内容：

| 测试 | 覆盖内容 |
|---|---|
| `test_frame_codec` | 协议编码、解码、粘包、半包、异常长度 |
| `test_command` | 命令解析、参数校验、大小写处理 |
| `test_command_executor` | 命令执行、响应格式、业务语义 |
| `test_kv_store` | SET/GET/DEL/EXPIRE/TTL/STATS |
| `test_scoped_fd` | RAII、移动语义、release/reset |
| `test_socket_util` | socket 创建、监听、非阻塞设置 |
| `test_smoke` | 基础构建验证 |

---

## 11. 冒烟测试

先启动服务端：

```bash
./build-release/tinykv_server 127.0.0.1 7777
```

另一个终端执行：

```bash
python3 scripts/smoke_test.py --host 127.0.0.1 --port 7777
```

冒烟测试会自动验证：

- `PING`
- `SET`
- `GET`
- `EXPIRE`
- `TTL`
- `STATS`
- `QUIT`

---

## 12. 压测

启动服务端后执行：

```bash
python3 scripts/benchmark.py --host 127.0.0.1 --port 7777 --connections 10 --requests 1000
```

示例输出：

```text
benchmark result
----------------
total_requests : 30000
total_errors   : 0
elapsed_sec    : 1.234
qps            : 24311.12
avg_latency_ms : 0.411
```

说明：

- `connections` 表示并发 TCP 连接数；
- `requests` 表示每个连接的循环次数；
- 每轮会执行 `PING / SET / GET` 三条命令；
- 该压测脚本用于功能和粗略吞吐验证，不等价于专业压测工具。

---

## 13. Linux 部署

### 13.1 使用 rsync 部署

```bash
./scripts/deploy_linux.sh user@server_ip
```

指定 SSH 端口和远程目录：

```bash
./scripts/deploy_linux.sh user@server_ip 22 ~/tinykv-cache
```

部署完成后登录服务器：

```bash
ssh user@server_ip
cd ~/tinykv-cache
./scripts/run_server.sh 0.0.0.0 7777
```

本地连接远程服务端：

```bash
./build-release/tinykv_client server_ip 7777
```

或者运行冒烟测试：

```bash
python3 scripts/smoke_test.py --host server_ip --port 7777
```

---

### 13.2 使用 tar + ssh 部署

如果远程服务器没有安装 `rsync`，可以使用备用脚本：

```bash
./scripts/deploy_linux_tar.sh user@server_ip
```

指定 SSH 端口和远程目录：

```bash
./scripts/deploy_linux_tar.sh user@server_ip 22 ~/tinykv-cache
```

---

## 14. 常见问题

### 14.1 `bash: rsync: command not found`

`rsync` 远程同步要求本地和服务器都安装 `rsync`。  
如果服务器没有安装，可以在服务器执行：

```bash
sudo apt install -y rsync
```

或改用：

```bash
./scripts/deploy_linux_tar.sh user@server_ip
```

---

### 14.2 `std::memcpy` 未声明

如果 Linux 编译时报：

```text
error: ‘memcpy’ is not a member of ‘std’
```

需要在使用 `std::memcpy` 的 `.cpp` 文件中显式包含：

```cpp
#include <cstring>
```

---

### 14.3 `undefined reference to pthread_create`

如果 Linux 链接时报：

```text
undefined reference to pthread_create
```

说明没有正确链接 pthread。  
CMake 中应包含：

```cmake
find_package(Threads REQUIRED)

target_link_libraries(tinykv PUBLIC
    tinykv_options
    Threads::Threads
)
```

---

### 14.4 远程客户端连接不上服务端

优先检查：

1. 服务端是否监听 `0.0.0.0`，而不是只监听 `127.0.0.1`；
2. 云服务器安全组是否开放端口；
3. Linux 防火墙是否放行端口；
4. 服务端进程是否正在运行；
5. 客户端使用的 IP 和端口是否正确。

---

## 15. 实现细节

### 15.1 Reactor 模型

阶段 10 后，服务端使用单线程 `poll` Reactor：

```text
listen fd:
    关注 POLLIN，用于 accept 新连接

client fd:
    默认关注 POLLIN，用于 recv 请求
    当 write_buffer 非空时，额外关注 POLLOUT，用于继续发送响应
```

所有 socket 均设置为非阻塞模式。

---

### 15.2 连接状态

每个客户端连接维护：

```cpp
struct Connection {
    ScopedFd fd;
    std::string read_buffer;
    std::string write_buffer;
    bool close_after_write;
    bool closed;
};
```

其中：

- `read_buffer` 用于保存未解析完的半包数据；
- `write_buffer` 用于保存未发送完的响应数据；
- `close_after_write` 用于处理 `QUIT` 命令；
- `closed` 表示连接需要被清理。

---

### 15.3 TTL 清理

KVStore 采用：

```text
lazy delete + background sweeper
```

访问 key 时会检查是否过期并删除；后台线程会周期性调用：

```cpp
KVStore::sweep_expired()
```

清理长期未访问的过期 key。

后台 sweeper 使用：

```cpp
std::thread
std::condition_variable
```

服务停止时可以及时唤醒并退出。

---

### 15.4 RAII fd 管理

`ScopedFd` 用于管理 Unix 文件描述符：

```text
构造时接管 fd
析构时自动 close
禁止拷贝
允许移动
```

这样可以避免：

- fd 泄漏；
- 异常路径忘记 close；
- double close；
- fd 所有权不清晰。

---

## 16. 面试亮点

这个项目适合在 C++ 后端面试中重点讲以下内容：

1. **TCP 粘包/半包处理**  
   使用 4 字节长度字段协议，每个连接维护 `read_buffer`。

2. **非阻塞 IO 与 poll Reactor**  
   使用单线程 `poll()` 同时管理 listen fd 和多个 client fd。

3. **部分写处理**  
   非阻塞 `send()` 不保证一次写完，使用连接级 `write_buffer` 保存剩余数据。

4. **RAII 资源管理**  
   使用 `ScopedFd` 管理 fd 生命周期，禁止拷贝、允许移动。

5. **模块分层清晰**  
   `FrameCodec`、`CommandParser`、`CommandExecutor`、`KVStore`、`TcpServer` 职责分离。

6. **TTL 设计**  
   使用 `steady_clock` 实现 TTL，采用 lazy delete + 后台 sweeper 清理过期 key。

7. **工程化能力**  
   提供 CMake、ctest、部署脚本、冒烟测试、压测脚本。

---

## 17. 当前限制与后续优化

当前版本是教学和面试项目，仍有进一步优化空间：

- 使用 `epoll` 替换 `poll`，提高大量连接下的性能；
- 抽象 `Poller` 接口，支持 Linux `epoll` 和 macOS `kqueue`；
- 使用多 Reactor 或线程池处理耗时任务；
- 使用 `write_offset` 优化 `write_buffer.erase()` 的内存移动；
- 使用小根堆或时间轮优化 TTL 清理；
- 支持配置文件和日志级别；
- 支持更多命令，例如 `MGET`、`MSET`、`INCR`；
- 增加集成测试和 CI；
- 支持优雅退出和信号处理。

---

## 18. License

This project is intended for learning and interview preparation. You may adapt it for personal study and demonstration.