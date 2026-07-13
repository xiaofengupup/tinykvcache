# Day 1：工程质量门禁

## 1. 优化目标

建立统一的编译、测试和运行时错误检测流程。

## 2. 新增检查

- 完整编译警告
- Warnings-as-Errors
- AddressSanitizer
- UndefinedBehaviorSanitizer
- ThreadSanitizer
- 端到端 TCP 集成测试
- GitHub Actions CI

## 3. 构建矩阵

| 构建 | 作用 |
|---|---|
| Debug + Werror | 编译质量 |
| ASan + UBSan | 内存错误和未定义行为 |
| TSan | 数据竞争 |
| Release | 部署构建验证 |

## 4. 集成测试覆盖

- 真实服务端进程
- 两个 TCP 客户端
- 多客户端共享 KVStore
- 半包
- 粘包
- QUIT 仅关闭当前连接

## 5. 发现并修复的问题

记录今天实际发现的每一个 warning 和 sanitizer 问题。

## 6. 最终结果

记录四种构建和 CI 的执行结果。