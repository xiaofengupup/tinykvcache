# 阶段 4：ScopedFd RAII 文件描述符封装

## 1. 阶段目标

本阶段实现 ScopedFd，用于管理 Unix 文件描述符 fd。

socket、pipe、普通文件在 Unix/Linux/macOS 中都可以用 fd 表示。

fd 是系统资源，使用完后必须 close，否则会造成资源泄漏。

## 2. 为什么需要 ScopedFd

如果直接使用 int fd，需要在所有路径上手动 close。

例如：

```cpp
int fd = ::socket(...);
if (some_error) {
    return;
}
::close(fd);
```

如果中间提前 return，就可能忘记 close。

ScopedFd 使用 RAII 管理 fd：

```text
构造时接管 fd
析构时自动 close
```

这样可以减少资源泄漏风险。

## 3. 什么是 RAII

RAII 全称是 Resorce Acquisition Is Initialization。含义是：
1. 资源获取即初始化。
2. 对象生命周期绑定资源生命周期。

在本项目中：
1. `ScopedFd` 对象存在时，表示持有 fd
2. `ScopedFd` 对象析构时，自动释放 fd

## 4. 为什么禁止拷贝

如果 `ScopedFd` 允许拷贝：
```cpp
ScopedFd a(fd);
ScopedFd b = a;
```
那么 a 和 b 就会同时管理同一个 fd，当 a 和 b 析构时，都会调用 `::close(fd)`，这样就会导致 double close。

更严重的是，fd 数字可能会被系统复用，就有可能
1. 第一次 close 之后，同一个 fd 数字被系统分配给其它 socket 使用；
2. 第二次 close 时，可能就会误关闭另一个连接。

因此，`ScopedFd` 必须禁止拷贝。


## 5. 为什么允许移动

虽然不能拷贝 fd 所有权，但可以支持转移所有权，即移动。
```cpp
ScopedFd a(fd);
ScopedFd b(std::move(a));
```

移动后：b 接管 fd，a 变成无效状态。

这可以支持函数返回 ScopedFd，或者在容器和对象之间转移 fd。

## 6. Release 和 Reset 的语义区别

Release 表示释放 fd 所有权，但不关闭 fd，其返回值是 fd 数字。因此，调用者需要手动 close fd。

Reset 表示关闭当前 fd，并接管新的 fd。

## 7. 析构函数为什么调用 reset

析构函数中调用：
```cpp
reset();
```
可以复用 Reset 里的关闭逻辑。

这样只要对象生命周期结束，就会自动关闭当前 fd。

## 8. 为什么 close 失败不抛异常

`ScopedFd` 的析构函数不应该抛异常。

如果析构函数抛异常，尤其是在栈展开过程中，可能导致程序终止。

因此本项目在 Reset 和析构中忽略 close 返回值。

对当前项目来说，关闭 fd 失败不会影响后续业务逻辑。

## 9. 本阶段测试覆盖
本阶段测试覆盖：

1. 默认构造；
2. 接管 fd；
3. 禁止拷贝；
4. 允许移动；
5. 移动构造；
6. 移动赋值；
7. release；
8. reset；
9. reset 到新 fd；
10. reset 同一个 fd。

## 10. 面试表达
`ScopedFd` 是一个 RAII 封装类，用来管理 Unix 文件描述符。

socket fd 是系统资源，必须在不用时 close。为了避免异常路径或提前 return 导致 fd 泄漏，我把 fd 封装进 ScopedFd，析构时自动 close。

这个类禁止拷贝，防止两个对象管理同一个 fd 导致 double close；允许移动，用于表达 fd 所有权转移。

move constructor 通过 release 从旧对象拿走 fd，move assignment 会先关闭当前对象已有 fd，再接管右值对象的 fd。

release 表示放弃管理但不关闭 fd，reset 表示关闭当前 fd 并接管新 fd。