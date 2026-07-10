# TinyKVCache Scratch

TinyKVCache is a small C++17 TCP key-value cache server project.

This repository is written from scratch for practicing:

- CMake project organization
- C++17 coding
- TCP socket programming
- application-layer protocol design
- non-blocking IO
- poll-based Reactor
- in-memory key-value storage
- TTL expiration
- Linux deployment

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DTINYKV_BUILD_TESTS=ON

cmake --build build -j

ctest --test-dir build --output-on-failure
```

`cmake -S . -B build` 含义说明：
- -S .            表示源码构建目录是当前目录
- -B build        表示构建目录是 build

`cmake --build build -j` 含义说明：
- 使用 build 目录中的构建配置来编译项目
- `-j` 表示并行编译

`ctest` 含义说明：
- `ctest` 是 CMake 配套的测试运行工具，当前在 CMakeLists.txt 中写了 `add_test(NAME smoke COMMAND test_smoke)`。所以，可以使用 `ctest --test-dir build --output-on-failure` 统一运行测试。