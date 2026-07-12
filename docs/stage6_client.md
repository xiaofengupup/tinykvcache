# 阶段 6：TinyKVClient 客户端实现


## 1. 阶段目标

本阶段实现命令行客户端。

客户端负责：

1. 建立 TCP 连接；
2. 读取用户输入；
3. 使用 FrameCodec 编码；
4. 发送 TCP 数据；
5. 接收服务器响应；
6. 解码并打印。

## 2. 客户端流程

用户输入

↓

FrameCodec.encode()

↓

TCP send()

↓

TCP recv()

↓

FrameCodec.decode()

↓

打印响应

## 3. 为什么客户端不使用 CommandParser

CommandParser 属于服务端业务逻辑。

客户端只负责发送用户输入。

例如：

用户输入：SET name xiaofeng

客户端只知道：这是一个字符串。

真正解析为：SET key value 应该由服务端完成。


## 4. 为什么客户端使用阻塞 IO

客户端一次只处理一个请求：

输入命令

↓

发送

↓

等待响应

不需要处理多个连接。因此阻塞 socket 足够。


## 5. 为什么需要 send_all


TCP send 不保证一次发送完整数据。如果 send 只发送部分数据，需要继续发送剩余部分。

因此客户端实现 send_all。


## 6. 为什么 recv 需要 buffer


TCP 是字节流。一次 recv 可能：

1. 半条消息；
2. 一条消息；
3. 多条消息。

因此客户端也需要使用 FrameCodec 和 buffer 处理边界。


## 7. 面试表达


客户端实现比较简单，主要用于验证整个 TCP 通信链路。

客户端使用 SocketUtil 创建连接，使用 ScopedFd 管理 socket 生命周期。

发送数据前通过 FrameCodec 增加长度头，接收数据后通过 FrameCodec 解析完整 frame。

由于客户端只需要同步执行：

输入命令 -> 发送 -> 等待响应，

所以采用阻塞 IO。

服务端面对多连接时，再采用非阻塞 IO 和 Reactor 模型。