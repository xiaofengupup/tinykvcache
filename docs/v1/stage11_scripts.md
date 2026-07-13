# 阶段 12：部署与压测脚本

## 1. 阶段目标

本阶段为 TinyKVCache 增加工程化脚本，使项目可以完成：

1. Release 构建；
2. 本地启动；
3. 远程 Linux 部署；
4. 冒烟测试；
5. 简单压测。

## 2. 新增脚本

| 脚本 | 作用 |
|---|---|
| build_release.sh | Release 构建并运行测试 |
| run_server.sh | 启动服务端 |
| deploy_linux.sh | 同步代码到 Linux 服务器并构建 |
| smoke_test.py | 自动验证基础功能 |
| benchmark.py | 简单并发压测 |

## 3. Release 构建

执行：

```bash
./scripts/build_release.sh
```

构建产物：

```
build-release/tinykv_server
build-release/tinykv_client
```

## 4. 本地启动
```
./scripts/run_server.sh 127.0.0.1 7777
```

## 5. Linux 部署
```
./scripts/deploy_linux.sh user@server_ip 22 ~/tinykv-cache
```

服务器上启动：
```
cd ~/tinykv-cache
./scripts/run_server.sh 0.0.0.0 7777
```

## 6. 冒烟测试
```
python3 scripts/smoke_test.py --host 127.0.0.1 --port 7777
```

冒烟测试会验证：
```
PING；
SET；
GET；
EXPIRE；
TTL；
STATS；
QUIT。
```

## 7. 压测
```
python3 scripts/benchmark.py --host 127.0.0.1 --port 7777 --connections 10 --requests 1000
```

压测输出：
```
总请求数；
错误数；
总耗时；
QPS；
平均延迟。
```

## 8. 注意事项

如果远程连接失败，需要检查：

1. 服务端是否监听 0.0.0.0；
2. 云服务器安全组是否开放端口；
3. Linux 防火墙是否放行端口；
4. SSH 端口是否正确；
5. 服务器进程是否正在运行。


## 9. 面试表达

这一阶段中，我为项目补充了工程化脚本。

build_release.sh 用于 Release 构建和运行测试，deploy_linux.sh 用于把项目同步到 Linux 服务器并远程构建，smoke_test.py 用于自动验证服务端基础功能，benchmark.py 用于做简单并发压测。

这样项目不仅能本地运行，也可以部署到真实 Linux 服务器上，并通过脚本验证功能和基本吞吐。