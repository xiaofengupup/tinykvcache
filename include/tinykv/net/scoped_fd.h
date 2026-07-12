/**
 * C++ RAII 管理 fd
 */

#pragma once

namespace tinykv {

/**
 * ScopedFd 是一个 RAII 类，用于管理文件描述符（fd）的生命周期。
 * 
 * 当 ScopedFd 对象被销毁时，它会自动关闭所管理的文件描述符，确保资源的正确释放，避免文件描述符泄漏。
 */
class ScopedFd {
public:
    /**
     * Unix/Linux/macOS 中，合法 fd 一般是非负整数：
     * 0：标准输入
     * 1：标准输出
     * 2：标准错误
     * 3 及以后：普通文件、socket、pipe 等
     */
    static constexpr int INVALID_FD = -1; // 无效的文件描述符

    ScopedFd() noexcept = default;

    /**
     * 接管一个已有的 fd
     * 
     * 注意：调用者把 fd 传递进来之后，就不应该在手动关闭这个 fd
     */
    explicit ScopedFd(int fd) noexcept;

    /**
     * 禁止拷贝构造和拷贝赋值
     * 
     * 如果允许拷贝，就可能出现两个 ScopedFd 管理同一个 fd
     * 两个对象析构时都会 close，导致 double close。
     */
    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;

    /**
     * 允许移动
     * 
     * 移动表示 fd 所有权从一个对象转移到另一个对象。
     */
    ScopedFd(ScopedFd&& other) noexcept;
    ScopedFd& operator=(ScopedFd&& other) noexcept;

    /**
     * 析构时自动关闭当前持有的 fd
     */
    ~ScopedFd();

public:
    int Get() const noexcept; // 获取当前持有的 fd
    bool Valid() const noexcept; // 判断当前 fd 是否有效
    explicit operator bool() const noexcept; // 判断当前 fd 是否有效

    /**
     * 释放 fd 所有权。
     * 
     * 返回当前 fd，并把对象内部 fd 置为无效。
     * 调用 release 后，调用者需要自己负责 close 返回的 fd。
     */
    int Release() noexcept;

    /**
     * 关闭当前 fd
     * 
     * 如果 new_fd 是 -1，表示只关闭当前 fd，不接管新 fd。
     */
    void Reset(int newFd = INVALID_FD) noexcept;

private:
    int m_fd = INVALID_FD; // 当前持有的文件描述符，初始为无效值
};

} // end tinykv