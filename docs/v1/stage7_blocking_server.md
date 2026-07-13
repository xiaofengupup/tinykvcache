# 阶段 7：阻塞版 TCP Server


## 1. 阶段目标

本阶段第一次将前面的模块串联起来，实现完整 TCP 服务端。

数据流程：
```
client

↓

TCP

↓

FrameCodec

↓

CommandParser

↓

CommandExecutor

↓

KVStore

↓

response
```

## 2. 服务端流程

server 启动：
```
socket

↓

bind

↓

listen

等待客户端：

accept

↓

recv

↓

decode frame

↓

parse command

↓

execute

↓

encode response

↓

send
```


## 3. 模块职责

- SocketUtil:负责创建 socket。
- ScopedFd:负责 fd 生命周期。
- FrameCodec:负责解决 TCP 消息边界。
- CommandParser:负责解析业务命令。
- CommandExecutor:负责执行命令。
- KVStore:负责保存数据。


## 4. 当前版本限制

当前 Server 是阻塞模型。

一次只能处理一个客户端：
```
client A

↓

处理完成

↓

client B
```

如果一个客户端长期占用连接，会阻塞其他客户端。

后续阶段会使用：poll Reactor 解决多客户端并发问题。


## 5. 面试表达

第一版服务端采用阻塞模型，目的是验证完整业务链路。

请求从 TCP 收到后，首先经过 FrameCodec 解决消息边界，然后 CommandParser 转换成结构化命令，CommandExecutor 执行业务逻辑，最终通过 KVStore 完成数据操作。

下一步会把这个模型升级为非阻塞 Reactor，通过 poll 同时管理多个客户端连接。