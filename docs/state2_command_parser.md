# 阶段 2：CommandParser 命令解析模块

## 1. 阶段目标
本阶段实现命令解析模块。

- `FrameCodec` 负责从 TCP 字节流中解析出完整 payload。
- `CommandParser` 负责把 payload 文本解析成结构化的 `Command` 对象。

## 2.模块职责

`CommandParser` 只做命令解析，不执行命令。
例如：

```text
SET name xiaofeng
```

会被解析成：
```
type  = Set
key   = name
value = xiaofeng
```
但它不会真正把 name 写入 KVStore。

## 3.支持的命令
| 命令     | 格式                 | 说明           |
| ------ | ------------------ | ------------ |
| PING   | PING               | 心跳命令         |
| SET    | SET key value      | 设置 key-value |
| GET    | GET key            | 获取 key       |
| DEL    | DEL key            | 删除 key       |
| EXPIRE | EXPIRE key seconds | 设置过期时间       |
| TTL    | TTL key            | 查询剩余过期时间     |
| STATS  | STATS              | 查询状态         |
| QUIT   | QUIT               | 关闭连接         |


## 4.命令大小写
- 命令名大小写不敏感。
- key 和 value 保留原始大小写

## 5.set 的特殊处理
SET 的 value 允许包含空格。

例如：
```
SET sentence hello tiny kv cache
```
解析结果是：
```
key   = sentence
value = hello tiny kv cache
```
因此 SET 不能简单地只按空格切成固定 3 段。

本项目的处理方式是：

1. 先识别第一个单词作为命令名；
2. 第二个单词作为 key；
3. key 后面的剩余文本整体作为 value；
4. value 首尾多余空白会被 trim；
5. value 内部连续空格会保留。

## 6.EXPIRE 的 seconds 校验
EXPIRE 的 seconds 必须是正整数。

非法示例：
```
EXPIRE name 0
EXPIRE name -1
EXPIRE name abc
EXPIRE name 10 extra
```

## 7.Unknown 命令

以下情况会返回 Unknown：

- 空命令；
- 未知命令；
- 参数数量不正确；
- EXPIRE seconds 不合法；
- SET 缺少 key 或 value；
- PING / STATS / QUIT 携带额外参数。

## 8.本阶段测试覆盖

本阶段测试覆盖：

1. 空命令；
2. 空白命令；
3. 命令大小写；
4. GET / DEL / TTL 参数校验；
5. PING / STATS / QUIT 不能携带额外参数；
6. SET value 支持空格；
7. SET value 内部连续空格保留；
8. EXPIRE seconds 校验；
9. raw 原始命令保留；
10. Unknown 命令。

## 9.面试表达
这个项目中，我把协议解析和命令解析做了分层。

FrameCodec 负责把 TCP 字节流还原成完整 payload，解决粘包和半包问题。

CommandParser 负责把 payload 字符串解析成结构化 Command 对象。

例如 SET 命令比较特殊，因为 value 可能包含空格，所以不能简单按照空格切成三个 token。我采用的方式是把第二个单词作为 key，key 后面的剩余文本整体作为 value。

对于 EXPIRE，我会校验 seconds 必须是正整数，避免非法过期时间进入后续业务逻辑