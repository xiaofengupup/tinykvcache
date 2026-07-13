# 阶段 8：TcpServer 类封装

## 1. 阶段目标

本阶段将阶段 7 中写在 server_main.cpp 里的阻塞服务端逻辑抽取为 TcpServer 类。

server_main.cpp 只负责解析命令行参数和启动服务。

## 2. 当前架构

请求处理流程：

```text
client
  ↓
TCP
  ↓
TcpServer::handle_client
  ↓
FrameCodec::decode
  ↓
CommandParser
  ↓
CommandExecutor
  ↓
KVStore
  ↓
FrameCodec::encode
  ↓
send
```

## 3. TcpServer 的职责
`TcpServer` 负责：
1. 创建监听 socket
2. accept 客户端连接
3. recv 客户端数据
4. 使用 `FrameCodec` 解析 payload
5. 使用 `CommandParser` 解析命令
6. 使用 `CommandExecutor` 执行业务
7. 使用 `FrameCodec` 编码响应数据
8. send 响应给客户端

## 4. server_main 的职责
server_main 只负责：

1. 解析 host 和 port；
2. 创建 `TcpServer`；
3. 调用 `server.Run()`；
4. 捕获异常并打印错误。

##  5. 为什么要抽象出 TcpServer 类
如果所有的逻辑都写在 main 方法中，会导致：
1. 网络逻辑和启动逻辑混合；
2. 后续无法平滑升级为 poll reactor；
3. 不方便维护和测试；

抽出 TcpServer 后，server_main 更清晰，服务端逻辑也更容易演进。

## 6. 当前版本限制

当前 `TcpServer` 仍然是阻塞模型，也就是说：
```text
accept 一个客户端
  ↓
阻塞处理这个客户端
  ↓
客户端断开
  ↓
再 accept 下一个客户端
```

如果一个客户端长时间连接但不发送数据，服务端会阻塞在 recv，其他客户端无法被处理。

这个问题会在后续阶段通过 poll Reactor 解决。


## 7. QUIT 的处理

QUIT 只关闭当前客户端连接，不关闭整个服务端。

这样服务端可以继续接受新的客户端连接。

## 8. 面试表达

在这一阶段中，我把阻塞服务端从 main 函数抽成了 TcpServer 类。

server_main 只保留命令行参数解析和启动逻辑，TcpServer 负责 accept、recv、decode、parse、execute 和 send。

虽然当前仍然是阻塞模型，但这个类封装为后续升级 poll Reactor 做了准备。下一步只需要把 handle_client 的阻塞 recv 逻辑拆成连接状态和事件处理函数，就可以演进为多客户端非阻塞模型。