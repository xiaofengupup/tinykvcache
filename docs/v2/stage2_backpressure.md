# Day 2：连接缓冲区与背压

## 1. 优化目标

解决部分写内存移动、慢客户端内存增长和事件循环公平性问题。

## 2. OutputBuffer

记录 storage 和 readPosition，部分写后只移动偏移量。

达到整理阈值后才执行一次 compact。

## 3. 背压策略

- write buffer 达到高水位：暂停 POLLIN
- 下降到低水位：恢复 POLLIN
- 超过硬上限：关闭慢客户端

## 4. 公平性限制

- 每次 POLLIN 最多读取 16 KiB
- 每次 POLLOUT 最多发送 64 KiB
- 每次 listen 事件最多 accept 64 个连接

## 5. 资源边界

记录：

- 最大 read buffer
- write 低水位
- write 高水位
- write 硬上限

## 6. 测试

记录：

- 单元测试
- e2e 测试
- 慢客户端测试
- ASan/UBSan
- TSan

## 7. 优化结果

记录优化前后压测数据和慢客户端行为。